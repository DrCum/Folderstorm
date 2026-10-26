// SPDX-License-Identifier: LGPL-2.1-or-later
using Folderstorm.Theme;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Xml.Linq;

if (args.Length != 1) { Console.Error.WriteLine("Pass the repository or matching viewer root"); return 2; }
var tests = new List<(string Name, Action Run)>();
var root = Path.Combine(Environment.CurrentDirectory, "artifacts", "tests-" + Guid.NewGuid().ToString("N")); Directory.CreateDirectory(root);
var catalog = new BaseCatalog(args[0]);
var projectDirectory = Path.Combine(root, "project");
ThemeProject? project = null; ValidatedTheme? noOp = null;
void Assert(bool condition, string message) { if (!condition) throw new Exception(message); }
void Reject(string code, Action action)
{
    try { action(); throw new Exception("Expected rejection: " + code); }
    catch (ThemeException e) { Assert(e.Code == code, $"Expected {code}, got {e.Code}: {e.Message}"); }
}
void Test(string name, Action run) => tests.Add((name, run));

Test("base catalog preserves case and installed schemes", () =>
{
    Assert(catalog.Schemes.Length == 6 && catalog.Schemes.Any(s => s.Id == "folderstorm"), "Scheme catalog");
    Assert(catalog.Colors.ContainsKey("White") && catalog.Colors.ContainsKey("white"), "Case-sensitive IDs lost");
    Assert(catalog.Colors.Count == 488 && catalog.Textures.Count == 969, "Pinned baseline catalog changed");
});
Test("project save/reopen and no-op deterministic export", () =>
{
    project = ProjectStore.Create(catalog, "Gate A baseline"); ProjectStore.Save(projectDirectory, project);
    var reopened = ProjectStore.Open(projectDirectory);
    Assert(Contract.Bytes(project).SequenceEqual(Contract.Bytes(reopened)), "Project round trip changed state");
    noOp = PackageStore.Compile(projectDirectory, reopened, catalog);
    Assert(noOp.Files.Count == 2 && !noOp.Files.Keys.Any(p => p.StartsWith("viewer/", StringComparison.Ordinal)), "No-op flattened inherited resources");
    var first = Path.Combine(root, "first.fstheme"); var second = Path.Combine(root, "second.fstheme");
    PackageStore.Export(first, noOp); PackageStore.Export(second, noOp);
    Assert(File.ReadAllBytes(first).SequenceEqual(File.ReadAllBytes(second)), "ZIP export is nondeterministic");
    Assert(PackageStore.ReadArchive(first, catalog).Revision == noOp.Revision, "Package round trip changed revision");
});
Test("accent aliases and alpha variants compile together", () =>
{
    var colors = catalog.CompileColors(new Dictionary<string, string> { ["ThemeAccent"] = "0.2 0.8 0.5 1" });
    Assert(colors["AccentColor"] == "0.2 0.8 0.5 1" && colors["AnsaStorm_HtmlLink"] == "0.2 0.8 0.5 1", "Accent fan-out");
    Assert(colors["SchemeLightest_35"] == "0.2 0.8 0.5 0.35", "Alpha variant");
    Assert(colors["MapPickRadiusColor"] == "0.2 0.8 0.5 0.35", "An alias of an alpha variant did not follow the accent");
    var layered = catalog.CompileColors(new Dictionary<string, string> { ["ThemeSelected"] = "0.1 0.2 0.3 1", ["ThemePanel"] = "0.11 0.22 0.33 1" });
    Assert(layered["PieMenuSelectedColor"] == "0.1 0.2 0.3 0.66" && layered["UploadDialogBackground"] == "0.11 0.22 0.33 0.66", "Indirect role alpha fan-out");
    Reject("color", () => catalog.CompileColors(new Dictionary<string, string> { ["ThemeAccent"] = "NaN 0 0 1" }));
});
Test("undo and imported originals persist without flattening recipes", () =>
{
    var p = project!; p.Checkpoint("Mint accent"); p.Edits.Colors["ThemeAccent"] = "0.2 0.8 0.5 1";
    p.Edits.Recipes["button"] = new("solid-fill", new Dictionary<string, JsonElement> { ["color"] = JsonSerializer.SerializeToElement("#32cc80") });
    var texture = catalog.Textures.Values.First(t => t.Source != null && t.Source.EndsWith(".png", StringComparison.Ordinal));
    var payload = "viewer/textures/" + texture.Attributes["file_name"];
    p.Edits.Assets.Add(ProjectStore.ImportAsset(projectDirectory, Path.Combine(catalog.Root, texture.Source!), payload, "image"));
    ProjectStore.Save(projectDirectory, p); var reopened = ProjectStore.Open(projectDirectory);
    Assert(reopened.Edits.Recipes.ContainsKey("button") && reopened.Undo.Count == 1, "Authoring metadata lost");
    Assert(reopened.UndoEdit() && reopened.Edits.Colors.Count == 0, "Undo failed");
    Assert(reopened.RedoEdit() && reopened.Edits.Recipes.ContainsKey("button"), "Redo failed");
});
Test("package import starts independent history and preserves payload bytes", () =>
{
    var theme = PackageStore.Compile(projectDirectory, project!, catalog);
    var sourceBefore = File.ReadAllBytes(Path.Combine(projectDirectory, ProjectStore.FileName));
    var directory = Path.Combine(root, "imported"); var imported = PackageStore.ImportProject(directory, theme);
    Assert(imported.Origin == "compiled-package" && imported.Undo.Count == 0 && imported.Edits.Recipes.Count == 0, "Invented authoring history");
    var reexported = PackageStore.Compile(directory, imported, catalog);
    Assert(theme.Files.Keys.All(k => theme.Files[k].SequenceEqual(reexported.Files[k])), "Compiled payload changed on import/export");
    Assert(sourceBefore.SequenceEqual(File.ReadAllBytes(Path.Combine(projectDirectory, ProjectStore.FileName))), "Import changed source project");
});
Test("immutable library publication is idempotent and does not select profiles", () =>
{
    var library = Path.Combine(root, "library"); var path = ThemeLibrary.Publish(noOp!, library);
    Assert(ThemeLibrary.Publish(noOp!, library) == path, "Publication not idempotent");
    Assert(PackageStore.ReadRevision(path, catalog).Revision == noOp!.Revision, "Retained revision invalid");
    Assert(!Directory.EnumerateFiles(library, "custom-theme.json", SearchOption.AllDirectories).Any(), "Import selected a profile");
    if (OperatingSystem.IsWindows())
    {
        var concurrentLibrary = Path.Combine(root, "concurrent-library");
        var writers = Enumerable.Range(0, 3).Select(_ => Task.Run(() => ThemeLibrary.Publish(noOp!, concurrentLibrary))).ToArray();
        Task.WaitAll(writers);
        Assert(writers.Select(writer => writer.Result).Distinct(StringComparer.Ordinal).Count() == 1, "Concurrent imports did not converge on one immutable revision");
        Assert(!Directory.EnumerateDirectories(concurrentLibrary, ".staging-*").Any(), "Completed concurrent imports left staging data");
    }
    File.AppendAllText(Path.Combine(path, "base-dependencies.json"), " "); Reject("integrity", () => PackageStore.ReadRevision(path, catalog));
});
Test("paths, duplicate keys, capabilities and XUI behavior are rejected", () =>
{
    foreach (var path in new[] { "../bad", "C:/bad", "//server/file", "x\\y", "x/file:stream", "x/CON.txt", "x/file.", "x/../file" }) Reject("path", () => Contract.PathName(path));
    Reject("duplicate_key", () => Contract.Parse(Encoding.UTF8.GetBytes("{\"files\":[],\"f\\u0069les\":[]}")));
    var manifest = noOp!.Manifest with { RequiredCapabilities = ["unknown.v9"] };
    Reject("capability", () => PackageStore.Manifest(Contract.Bytes(manifest)));
    var widget = Contract.Xml(catalog.ResolveBytes("xui/en/widgets/floater.xml")); widget.Root!.SetAttributeValue("name", "changed-behavior");
    Reject("xui_attribute", () => XuiPolicy.Validate("xui/en/widgets/floater.xml", Contract.XmlBytes(widget), catalog));
    var tint = Contract.Xml(catalog.ResolveBytes("xui/en/widgets/floater.xml"));
    tint.Root!.SetAttributeValue("bg_alpha_image_overlay", "ThemeFloaterAlphaTint");
    XuiPolicy.Validate("xui/en/widgets/floater.xml", Contract.XmlBytes(tint), catalog);
    tint.Root.SetAttributeValue("bg_alpha_image_overlay", "MissingOverlayColor");
    Reject("reference", () => XuiPolicy.Validate("xui/en/widgets/floater.xml", Contract.XmlBytes(tint), catalog));
    Reject("xui_path", () => XuiPolicy.Validate("xui/en/panel_login.xml", [], catalog));
    Reject("xml", () => Contract.Xml(Encoding.UTF8.GetBytes("<!DOCTYPE colors [<!ENTITY file SYSTEM 'file:///secret'>]><colors>&file;</colors>")));
});
Test("changed inherited bytes fail compatibility without a version change", () =>
{
    var recorded = project!.BaseDependencies.Files.First(f => f.Present && f.Path.EndsWith(".png", StringComparison.Ordinal));
    var dependencies = project.BaseDependencies with { Files = project.BaseDependencies.Files.Select(file => file.Path == recorded.Path ? file with { Sha256 = new string('0', 64) } : file).ToArray() };
    Reject("base_changed", () => catalog.Verify(dependencies));
    Reject("base_changed", () => catalog.Verify(project.BaseDependencies with { Files = [] }));
});

Test("hex channels, alpha compositing and contrast use displayed colors", () =>
{
    Assert(ThemeColor.Parse("#3c8").Hex() == "#33CC88FF", "Short hex");
    Assert(ThemeColor.Parse("#1234").Hex() == "#11223344", "Short alpha must be last");
    Assert(ThemeColor.Parse("0.2 0.8 0.5 1").Hex(false) == "#33CC80", "Channel rounding");
    var white = ThemeColor.Parse("#fff"); var black = ThemeColor.Parse("#000");
    Assert(Math.Abs(ThemeColor.Contrast(black, white, white) - 21) < .000001, "Opaque contrast");
    Assert(ThemeColor.Contrast(ThemeColor.Parse("#0000"), white, black) == 1, "Transparent foreground contrast");
    Assert(ThemeColor.Parse("1 0 0 0.5").Over(black).NativeText == "0.5 0 0 1", "Straight-alpha composition");
    Reject("color", () => ThemeColor.Parse("#notacolor"));
    Reject("color", () => ThemeColor.Contrast(black, white, ThemeColor.Parse("#0000")));
});
Test("asymmetric slice guides convert native bottom origin without reversal", () =>
{
    var native = new NativeSlice(3, 21, 26, 7);
    var margins = SliceMargins.FromNative(32, 24, native);
    Assert(margins == new SliceMargins(3, 3, 6, 7) && margins.ToNative(32, 24) == native, "Native guide conversion");
    Assert(SliceMargins.FromAttributes(32, 24, new Dictionary<string, string>()) == default, "Default full stretch region");
    Assert(new SliceMargins(1, 1, 1, 1).Resize(2, 2, 1, 1) == new SliceMargins(1, 1, 0, 0), "Rounding must not invert a small stretch region");
    Reject("texture", () => new SliceMargins(3, 3, 30, 7).Validate(32, 24));
    Reject("texture", () => SliceMargins.FromNative(32, 24, new NativeSlice(3, 4, 26, 7)));
    Reject("texture", () => SliceMargins.FromNative(32, 24, new NativeSlice(3, int.MinValue, 26, 7)));
});
Test("asset families retain every logical ID and distinguish compound states", () =>
{
    Assert(TextureFamilies.Describe("PushButton_Selected_Disabled") == ("PushButton", "Selected disabled"), "Compound state split");
    Assert(TextureFamilies.Describe("PushButton_Off") == ("PushButton", "Normal"), "Normal state split");
    var families = TextureFamilies.Build(catalog.Textures.Values);
    Assert(families.Sum(family => family.Members.Length) == catalog.Textures.Count, "Catalog assets lost");
    Assert(families.SelectMany(family => family.Members).Select(member => member.Texture.Name).Distinct(StringComparer.Ordinal).Count() == catalog.Textures.Count, "Logical IDs collided");
});
Test("role alpha derivatives propagate to dependent aliases and inspection detects cycles", () =>
{
    catalog.Colors["AuthoringAlphaAlias"] = new("AuthoringAlphaAlias", null, "EmphasisColor_35", "fixture");
    catalog.Colors["AuthoringCycleA"] = new("AuthoringCycleA", null, "AuthoringCycleB", "fixture");
    catalog.Colors["AuthoringCycleB"] = new("AuthoringCycleB", null, "AuthoringCycleA", "fixture");
    try
    {
        var edits = new Dictionary<string, string> { ["ThemeAccent"] = "0.2 0.8 0.5 1" };
        Assert(catalog.CompileColors(edits)["AuthoringAlphaAlias"] == "0.2 0.8 0.5 0.35", "Alpha fan-out stopped early");
        var inspection = catalog.InspectColor("AuthoringAlphaAlias", edits);
        Assert(inspection.AffectedByOverride && !inspection.DirectOverride, "Inherited/derived distinction");
        Assert(inspection.ReferenceChain.SequenceEqual(new[] { "AuthoringAlphaAlias", "EmphasisColor_35", "ThemeAccent" }), "Role provenance stopped at a compiled alias");
        Reject("color_cycle", () => catalog.InspectColor("AuthoringCycleA", new Dictionary<string, string>()));
    }
    finally
    {
        catalog.Colors.Remove("AuthoringAlphaAlias"); catalog.Colors.Remove("AuthoringCycleA"); catalog.Colors.Remove("AuthoringCycleB");
    }
});

Test("version-1 migration is in memory until save and retains original checkpoints", () =>
{
    var directory = Path.Combine(root, "migration"); Directory.CreateDirectory(directory);
    var p = ProjectStore.Create(catalog, "Old project"); p.Checkpoint("Old edit");
    var json = JsonNode.Parse(Contract.Bytes(p))!.AsObject(); json["project_version"] = 1;
    json["edits"]!.AsObject().Remove("color_links");
    json["undo"]![0]!.AsObject().Remove("metadata");
    json["custom_note"] = new JsonObject { ["nested"] = "keep me" };
    var original = Encoding.UTF8.GetBytes(json.ToJsonString());
    File.WriteAllBytes(Path.Combine(directory, ProjectStore.FileName), original);
    var document = ProjectStore.OpenDocument(directory);
    Assert(document.CanEdit && document.WasMigrated && document.SourceVersion == 1 && document.Project!.ProjectVersion == 2, "Migration state");
    Assert(document.OriginalBytes.SequenceEqual(original) && File.ReadAllBytes(Path.Combine(directory, ProjectStore.FileName)).SequenceEqual(original), "Open rewrote the source");
    var migrated = document.Project!;
    Assert(migrated.UndoEdit() && migrated.RedoEdit(), "Old history cannot be replayed");
    ProjectStore.Save(directory, migrated);
    Assert(File.ReadAllBytes(Path.Combine(directory, "checkpoints", Contract.Hash(original) + ".fsthemeproj")).SequenceEqual(original), "Original v1 bytes not retained");
    Assert(ProjectStore.Open(directory).UnknownMetadata!["custom_note"].GetProperty("nested").GetString() == "keep me", "Unknown metadata lost");
});
Test("future versions and unknown operations stay read-only without rewriting bytes", () =>
{
    var futureDirectory = Path.Combine(root, "future"); Directory.CreateDirectory(futureDirectory);
    var future = Encoding.UTF8.GetBytes("{\"project_version\":3,\"future_edits\":[{\"opaque\":true}]}\n");
    File.WriteAllBytes(Path.Combine(futureDirectory, ProjectStore.FileName), future);
    var document = ProjectStore.OpenDocument(futureDirectory);
    Assert(!document.CanEdit && document.Project == null && document.OriginalBytes.SequenceEqual(future), "Future bytes not preserved");
    Reject("project_readonly", () => ProjectStore.Open(futureDirectory));
    Reject("project_readonly", () => ProjectStore.Save(futureDirectory, ProjectStore.Create(catalog, "Overwrite")));
    Assert(File.ReadAllBytes(Path.Combine(futureDirectory, ProjectStore.FileName)).SequenceEqual(future), "Future project overwritten");
    foreach (var inHistory in new[] { false, true })
    {
        var directory = Path.Combine(root, "unknown-operation-" + inHistory); Directory.CreateDirectory(directory);
        var p = ProjectStore.Create(catalog, "Unknown operation");
        var edits = inHistory ? new ProjectEdits() : p.Edits;
        edits.Recipes["button"] = new("future-filter.v9", new() { ["anything"] = JsonSerializer.SerializeToElement(new[] { 1, 2, 3 }) });
        if (inHistory) p.Redo.Add(new("Future redo", edits));
        var bytes = Contract.Bytes(p); File.WriteAllBytes(Path.Combine(directory, ProjectStore.FileName), bytes);
        var opened = ProjectStore.OpenDocument(directory);
        Assert(!opened.CanEdit && opened.Project != null && opened.ReadOnlyReasons.Any(reason => reason.Contains("future-filter.v9", StringComparison.Ordinal)), "Unknown operation became editable");
        Reject("project_readonly", () => ProjectStore.Save(directory, opened.Project!));
        Reject("project_readonly", () => PackageStore.Compile(directory, opened.Project!, catalog));
        Assert(File.ReadAllBytes(Path.Combine(directory, ProjectStore.FileName)).SequenceEqual(bytes), "Unsupported operation changed on disk");
    }
    var oversizedDirectory = Path.Combine(root, "oversized"); Directory.CreateDirectory(oversizedDirectory);
    File.WriteAllBytes(Path.Combine(oversizedDirectory, ProjectStore.FileName), new byte[Contract.ManifestLimit + 1]);
    Reject("size", () => ProjectStore.OpenDocument(oversizedDirectory));
});
Test("metadata and color links share durable undo and redo history", () =>
{
    var directory = Path.Combine(root, "metadata-history"); var p = ProjectStore.Create(catalog, "Original name");
    p.UnknownMetadata = new() { ["note"] = JsonSerializer.SerializeToElement("before") };
    p.Checkpoint("Edit identity"); p.DisplayName = "Changed name"; p.Author = "Marty"; p.Description = "Mint"; p.PackageVersion = "2.0.0";
    p.UnknownMetadata["note"] = JsonSerializer.SerializeToElement("after");
    p.Extensions["custom"] = JsonSerializer.SerializeToElement(42);
    ColorAuthoring.SetLink(p, catalog, "AnsaStorm_HtmlLink", "ThemePanel");
    ProjectStore.Save(directory, p); p = ProjectStore.Open(directory);
    Assert(p.UndoEdit() && p.Edits.ColorLinks.Count == 0 && p.DisplayName == "Changed name", "Link undo affected the wrong checkpoint");
    Assert(p.UndoEdit() && p.DisplayName == "Original name" && p.Author == "" && p.PackageVersion == "1.0.0" && p.Extensions.Count == 0, "Metadata undo failed");
    Assert(p.UnknownMetadata!["note"].GetString() == "before", "Metadata undo lost an extension");
    ProjectStore.Save(directory, p); p = ProjectStore.Open(directory);
    Assert(p.RedoEdit() && p.UnknownMetadata!["note"].GetString() == "after" && p.Extensions["custom"].GetInt32() == 42, "Metadata redo failed after reopen");
    Assert(p.RedoEdit() && p.Edits.ColorLinks["AnsaStorm_HtmlLink"] == "ThemePanel", "Link redo failed after reopen");
});
Test("authoring links flatten to runtime v1 and reject cycles transactionally", () =>
{
    var p = ProjectStore.Create(catalog, "Linked colors");
    ColorAuthoring.SetLiteral(p, catalog, "ThemePanel", "#234");
    ColorAuthoring.SetLink(p, catalog, "AnsaStorm_HtmlLink", "ThemePanel");
    var inspection = catalog.InspectColor("AnsaStorm_HtmlLink", p.Edits.Colors, p.Edits.ColorLinks);
    Assert(inspection.DirectOverride && inspection.Value.Hex(false) == "#223344" && inspection.ReferenceChain.SequenceEqual(new[] { "AnsaStorm_HtmlLink", "ThemePanel" }), "Custom link resolution/provenance");
    var before = Contract.Bytes(p);
    Reject("color_cycle", () => ColorAuthoring.SetLink(p, catalog, "ThemePanel", "AnsaStorm_HtmlLink"));
    Assert(Contract.Bytes(p).SequenceEqual(before), "Rejected link changed edits or history");
    var compiled = PackageStore.Compile(projectDirectory, p, catalog);
    Assert(compiled.Manifest.FormatVersion == 1 && Contract.Xml(compiled.Files["viewer/colors.xml"]).Root!.Elements("color").All(node => node.Attribute("value") != null && node.Attribute("reference") == null), "Runtime still requires authoring link support");
    Assert(ColorAuthoring.SetLiteral(p, catalog, "AnsaStorm_HtmlLink", "#abc") && !p.Edits.ColorLinks.ContainsKey("AnsaStorm_HtmlLink"), "Literal did not replace link");
    Assert(ColorAuthoring.Reset(p, catalog, "AnsaStorm_HtmlLink") && !p.Edits.Colors.ContainsKey("AnsaStorm_HtmlLink"), "Reset did not inherit");
    foreach (var scheme in catalog.Schemes)
    {
        var other = new BaseCatalog(args[0], "ansastorm", scheme.Id);
        Assert(other.CompileColors(new Dictionary<string, string>()).Count == 0, "No-op changed legacy colors in " + scheme.Id);
        Reject("color_literal", () => other.InspectColor("ChicletFlashColor", new Dictionary<string, string>()));
    }
});

Test("save-as stages all originals and history without altering source or overwriting folders", () =>
{
    var source = Path.Combine(root, "copy-source"); var destination = Path.Combine(root, "copy-destination");
    var p = ProjectStore.Create(catalog, "Original"); ProjectStore.Save(source, p);
    var original = File.ReadAllBytes(Path.Combine(source, ProjectStore.FileName));
    var image = catalog.Textures.Values.First(texture => texture.Source?.EndsWith(".png", StringComparison.Ordinal) == true);
    p.Checkpoint("Import image");
    p.Edits.Assets.Add(ProjectStore.ImportAsset(source, Path.Combine(catalog.Root, image.Source!), "viewer/textures/" + image.Attributes["file_name"], "image"));
    ProjectStore.Save(source, p);
    File.WriteAllText(Path.Combine(source, "external-editor-notes.txt"), "Do not lose this project file");
    var before = Directory.EnumerateFiles(source, "*", SearchOption.AllDirectories).ToDictionary(path => Path.GetRelativePath(source, path), Contract.HashFile);
    p.Checkpoint("Rename unsaved copy"); p.DisplayName = "Copy with unsaved edits";
    Assert(ProjectCopy.SaveAs(source, destination, p) == destination, "Copy destination differs");
    var copy = ProjectStore.Open(destination);
    Assert(copy.DisplayName == "Copy with unsaved edits" && copy.ThemeId == p.ThemeId && copy.Undo.Count == p.Undo.Count, "Copy lost current identity/history");
    Assert(File.ReadAllText(Path.Combine(destination, "external-editor-notes.txt")) == "Do not lose this project file", "External project data was lost");
    foreach (var asset in copy.Edits.Assets)
        Assert(Contract.HashFile(Path.Combine(source, asset.OriginalPath)) == Contract.HashFile(Path.Combine(destination, asset.OriginalPath)), "Imported original changed");
    Assert(File.ReadAllBytes(Path.Combine(destination, "checkpoints", Contract.Hash(original) + ".fsthemeproj")).SequenceEqual(original), "Earlier source checkpoint lost");
    Assert(before.All(entry => Contract.HashFile(Path.Combine(source, entry.Key)) == entry.Value), "Save As changed source bytes");
    Reject("project_exists", () => ProjectCopy.SaveAs(source, destination, p));
    Reject("project_overlap", () => ProjectCopy.SaveAs(source, Path.Combine(source, "inside"), p));
    Assert(!Directory.Exists(Path.Combine(source, "inside")), "Rejected copy created a folder");
});

var failed = 0;
foreach (var (name, run) in tests)
{
    try { run(); Console.WriteLine("PASS " + name); }
    catch (Exception error) { ++failed; Console.Error.WriteLine("FAIL " + name + "\n" + error); }
}
Console.WriteLine($"{tests.Count - failed}/{tests.Count} passed. Evidence workspace: {root}");
return failed == 0 ? 0 : 1;
