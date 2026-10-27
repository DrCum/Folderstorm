// SPDX-License-Identifier: LGPL-2.1-or-later
using Folderstorm.Theme;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Xml.Linq;

if (args.Length is < 1 or > 2 || args.Length == 2 && args[1] is not ("--fonts-only" or "--xui-only")) { Console.Error.WriteLine("Pass the repository or matching viewer root, optionally --fonts-only or --xui-only"); return 2; }
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
    Assert(TextureFamilies.Describe("PushButton_On_Selected") == ("PushButton", "Selected"), "Selected alias escaped its family");
    Assert(TextureFamilies.Describe("PushButton_Login_Over") == ("PushButton_Login", "Hover"), "Family grouping included a separate button family");
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

Test("image edits preserve source pixels and bound crop adjustments and generation", () =>
{
    byte[] bytes = [255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 0, 255, 0];
    var source = RasterImage.FromRgba(2, 2, bytes); bytes[0] = 0;
    Assert(source.Pixel(0, 0) == new RgbaPixel(255, 0, 0, 255), "Source aliases caller bytes");
    var crop = source.Crop(1, 0, 1, 2);
    Assert(crop.Pixel(0, 0) == new RgbaPixel(0, 255, 0, 255) && crop.Pixel(0, 1) == new RgbaPixel(255, 0, 255, 0), "Crop rows or hidden RGB changed");
    var tinted = RasterImage.FromRgba(1, 1, new byte[] { 128, 64, 32, 128 }).Tint(ThemeColor.Create(.5, 1, .25, .5));
    Assert(tinted.Pixel(0, 0) == new RgbaPixel(64, 64, 8, 64), "Tint channels or alpha differ");
    Assert(source.Adjust().ToRgba().SequenceEqual(source.ToRgba()), "Neutral adjustment changed bytes");
    Assert(source.Adjust(brightness: 1).Pixel(0, 0) == new RgbaPixel(255, 255, 255, 255), "Brightness clamping");
    var border = RasterImage.Fill(4, 4, ThemeColor.Parse("#0000")).Border(1, ThemeColor.Parse("#f00"));
    Assert(border.Pixel(0, 3) == new RgbaPixel(255, 0, 0, 255) && border.Pixel(1, 1).A == 0, "Border damaged the interior");
    var gradient = RasterImage.Gradient(3, 1, ThemeColor.Parse("#f00"), ThemeColor.Parse("#00f0"), false);
    Assert(gradient.Pixel(1, 0) == new RgbaPixel(255, 0, 0, 128) && gradient.Pixel(2, 0) == new RgbaPixel(0, 0, 255, 0), "Gradient introduced color halos or lost endpoint RGB");
    Assert(RasterImage.Fill(1, 1, ThemeColor.Parse("#f000")).Pixel(0, 0) == new RgbaPixel(255, 0, 0, 0), "Transparent fill lost the requested hidden RGB");
    Reject("image", () => source.Crop(-1, 0, 1, 1));
    Reject("image", () => source.Adjust(gamma: double.NaN));
    Reject("image", () => RasterImage.Fill(16384, 16384, ThemeColor.Parse("#fff")));
    Assert(source.Pixel(0, 0) == new RgbaPixel(255, 0, 0, 255) && source.Pixel(1, 1) == new RgbaPixel(255, 0, 255, 0), "An edit mutated the original");
});
Test("resize blends premultiplied alpha and asymmetric stretch keeps fixed borders", () =>
{
    var edge = RasterImage.FromRgba(2, 1, new byte[] { 255, 0, 0, 255, 0, 0, 255, 0 });
    Assert(edge.Resize(1, 1).Pixel(0, 0) == new RgbaPixel(255, 0, 0, 128), "Bilinear resize has transparent blue fringes");
    Assert(edge.Resize(2, 1).ToRgba().SequenceEqual(edge.ToRgba()), "Identity resize discarded hidden RGB");
    var pixels = new byte[5 * 4 * 4];
    for (var y = 0; y < 4; ++y)
        for (var x = 0; x < 5; ++x)
        { var index = (y * 5 + x) * 4; pixels[index] = (byte)(x * 30); pixels[index + 1] = (byte)(y * 50); pixels[index + 2] = (byte)(x + y); pixels[index + 3] = 255; }
    var source = RasterImage.FromRgba(5, 4, pixels); var margins = new SliceMargins(1, 1, 2, 2);
    Assert(source.Stretch(5, 4, margins).ToRgba().SequenceEqual(pixels), "Identity stretch moved guide pixels");
    foreach (var (width, height) in new[] { (9, 7), (14, 10), (3, 3) })
    {
        var stretched = source.Stretch(width, height, margins);
        Assert(stretched.Pixel(0, 0) == source.Pixel(0, 0), "Top-left corner changed");
        Assert(stretched.Pixel(width - 2, height - 2) == source.Pixel(3, 2) && stretched.Pixel(width - 1, height - 1) == source.Pixel(4, 3), "Asymmetric right/bottom margins were reversed or resampled");
    }
    Reject("image", () => source.Stretch(2, 2, margins));
});

Test("palette batches preview without mutation and commit links, values and resets as one durable step", () =>
{
    var p = ProjectStore.Create(catalog, "Palette batches"); p.Edits.Colors["ButtonImageColor"] = "0.2 0.3 0.4 1";
    var changes = new[] { new PaletteChange("ThemeAccent", "#64D8B280"), new PaletteChange("ButtonLabelColor", Link: "ThemeAccent") };
    var before = Contract.Bytes(p); var preview = PaletteProject.Preview(p, catalog, changes);
    Assert(preview.DirectChanges == 2 && preview.EffectiveChanges.ContainsKey("ThemeAccent") && Contract.Bytes(p).SequenceEqual(before), "Palette preview changed project or omitted selected values");
    Assert(PaletteProject.Apply(p, catalog, changes, "Batch palette") == 2 && p.Undo.Count == 1 && p.Edits.Colors["ButtonImageColor"] == "0.2 0.3 0.4 1", "Palette batch lost unrelated values or made extra checkpoints");
    Assert(PaletteProject.Apply(p, catalog, changes, "Repeat") == 0 && p.Undo.Count == 1, "Repeated palette batch created history");
    var changed = Contract.Bytes(p);
    Reject("color_cycle", () => PaletteProject.Apply(p, catalog, [new("ThemeAccent", Link: "ButtonLabelColor")], "Cycle"));
    Reject("palette_batch", () => PaletteProject.Apply(p, catalog, [new("ThemeText", "#FFF"), new("ThemeText", "#000")], "Duplicate"));
    Assert(Contract.Bytes(p).SequenceEqual(changed), "Rejected palette batch changed history");
    var directory = Path.Combine(root, "palette-batch"); ProjectStore.Save(directory, p); p = ProjectStore.Open(directory);
    Assert(p.UndoEdit() && Contract.Bytes(p.Edits).SequenceEqual(Contract.Bytes(Contract.Read<ThemeProject>(before).Edits)), "Palette batch Undo lost original edits");
    Assert(p.RedoEdit() && Contract.Bytes(p).SequenceEqual(changed), "Palette batch Redo failed after reopen");
    var reset = new[] { new PaletteChange("ThemeAccent"), new PaletteChange("ButtonLabelColor") };
    var resetPreview = PaletteProject.Preview(p, catalog, reset);
    Assert(resetPreview.EffectiveChanges.ContainsKey("ButtonLabelColor"), "Reset preview omitted restored dependent colors");
    Assert(PaletteProject.Apply(p, catalog, reset, "Reset selected") == 2 && !p.Edits.Colors.ContainsKey("ThemeAccent") && !p.Edits.ColorLinks.ContainsKey("ButtonLabelColor") && p.Undo.Count == 2, "Selected reset failed");
    PaletteProject.Apply(p, catalog, [new("ChicletFlashColor", "#FFF")], "Override legacy color");
    var legacy = PaletteProject.Preview(p, catalog, [new("ChicletFlashColor")]);
    Assert(legacy.NativeOnly.Contains("ChicletFlashColor") && PaletteProject.Apply(p, catalog, [new("ChicletFlashColor")], "Restore legacy color") == 1, "Native-only inherited color could not be restored");
});

Test("Violet Mint and Paper presets compile across every AnsaStorm scheme and retain unrelated overrides", () =>
{
    foreach (var scheme in catalog.Schemes)
    {
        var selected = new BaseCatalog(catalog.Root, scheme: scheme.Id);
        foreach (var preset in PalettePreset.Choices)
        {
            var p = ProjectStore.Create(selected, preset.Name); p.Edits.Colors["ButtonImageColor"] = "0.2 0.3 0.4 1";
            var changes = preset.Changes(selected); var before = Contract.Bytes(p);
            var preview = PaletteProject.Preview(p, selected, changes);
            Assert(changes.Length >= 10 && preview.DirectChanges > 0 && Contract.Bytes(p).SequenceEqual(before), "Preset did not stage a coordinated palette");
            PaletteProject.Apply(p, selected, changes, "Preset " + preset.Name);
            Assert(p.Undo.Count == 1 && p.Edits.Colors["ButtonImageColor"] == "0.2 0.3 0.4 1", "Preset lost unrelated artwork tint/history");
            ThemeColor Value(string role, string fallback) => selected.InspectColor(selected.Colors.ContainsKey(role) ? role : fallback, p.Edits.Colors, p.Edits.ColorLinks).Value;
            Assert(ThemeColor.Contrast(Value("ThemeText", "TextFgColor"), Value("ThemePanel", "SL-Background"), ThemeColor.Parse("#000")) >= 4.5, "Preset text/panel contrast is too low");
            Assert(p.UndoEdit() && Contract.Bytes(p.Edits).SequenceEqual(Contract.Bytes(Contract.Read<ThemeProject>(before).Edits)), "Preset Undo lost original palette");
        }
    }
    var exportRoot = Path.Combine(root, "palette-exports"); var exports = new List<object>();
    foreach (var preset in PalettePreset.Choices)
    {
        var p = ProjectStore.Create(catalog, preset.Name); var directory = Path.Combine(exportRoot, preset.Name);
        PaletteProject.Apply(p, catalog, preset.Changes(catalog), "Preset " + preset.Name); ProjectStore.Save(directory, p);
        var package = PackageStore.Compile(directory, p, catalog); var revision = ThemeLibrary.Publish(package, Path.Combine(exportRoot, "library"), catalog);
        exports.Add(new { preset.Name, package.Revision, RevisionDirectory = revision });
    }
    File.WriteAllBytes(Path.Combine(exportRoot, "result.json"), Contract.Bytes(new { BaseRoot = catalog.Root, Exports = exports, NativeVisualAcceptance = "not tested by this probe" }));
    Console.WriteLine("Palette export receipt: " + Path.Combine(exportRoot, "result.json"));
});

Test("image batches preserve individual originals and recipes with one durable checkpoint", () =>
{
    var directory = Path.Combine(root, "image-batch"); var p = ProjectStore.Create(catalog, "Image batch"); ProjectStore.Save(directory, p);
    p.Edits.Colors["ThemeAccent"] = "0.2 0.8 0.5 1";
    var bytes = catalog.ResolveBytes("textures/" + catalog.Textures["PushButton_On"].Attributes["file_name"]);
    var width = (int)System.Buffers.Binary.BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(16, 4)); var height = (int)System.Buffers.Binary.BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(20, 4));
    var names = new[] { "PushButton_Off", "PushButton_Disabled" };
    var before = names.ToDictionary(name => name, name => ImageProject.Read(directory, p, catalog, name));
    var edits = names.Select(name => new ImageEdit(name, bytes, TextureGeometry.FromAttributes(width, height, catalog.Textures[name].Attributes), new("tint", new() { ["color"] = System.Text.Json.JsonSerializer.SerializeToElement("0.2 0.8 0.5 1") }))).ToArray();
    Assert(ImageProject.SetPngBatch(directory, p, catalog, edits, "Tint states") == 2 && p.Undo.Count == 1 && p.Undo[0].Label == "Tint states", "Batch did not create one named checkpoint");
    foreach (var name in names)
    {
        var texture = ImageProject.Resolve(directory, p, catalog, name); var asset = p.Edits.Assets.Single(item => item.PayloadPath == "viewer/textures/" + texture.Attributes["file_name"]);
        Assert(File.ReadAllBytes(Path.Combine(directory, asset.OriginalPath)).SequenceEqual(before[name]) && p.Edits.Recipes[name].Operation == "tint", "Batch lost an individual original or recipe");
    }
    Assert(ImageProject.SetPngBatch(directory, p, catalog, edits, "Repeat") == 0 && p.Undo.Count == 1, "Repeated batch created history");
    ProjectStore.Save(directory, p); p = ProjectStore.Open(directory);
    Assert(p.UndoEdit() && names.All(name => ImageProject.Read(directory, p, catalog, name).SequenceEqual(before[name])) && p.Edits.Colors.ContainsKey("ThemeAccent"), "Batch Undo lost originals or unrelated colors");
    Assert(p.RedoEdit() && names.All(name => ImageProject.Read(directory, p, catalog, name).SequenceEqual(bytes)), "Batch Redo lost a state");
});

Test("failed image batches leave edits and history unchanged after partial asset preparation", () =>
{
    var directory = Path.Combine(root, "image-batch-conflict"); var p = ProjectStore.Create(catalog, "Batch conflict"); ProjectStore.Save(directory, p);
    ImageEdit Edit(string name, string source, string operation)
    {
        var bytes = catalog.ResolveBytes("textures/" + catalog.Textures[source].Attributes["file_name"]);
        var width = (int)System.Buffers.Binary.BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(16, 4)); var height = (int)System.Buffers.Binary.BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(20, 4));
        return new(name, bytes, TextureGeometry.FromAttributes(width, height, catalog.Textures[name].Attributes), new(operation, new()));
    }
    var first = Edit("PushButton_Off", "PushButton_On", "tint"); var second = Edit("PushButton_On", "PushButton_Off", "future-operation");
    var before = Contract.Bytes(p);
    Reject("recipe", () => ImageProject.SetPngBatch(directory, p, catalog, [first, second], "Fail"));
    Assert(Contract.Bytes(p).SequenceEqual(before), "Rejected batch changed edits/history");
    Reject("image_batch", () => ImageProject.SetPngBatch(directory, p, catalog, [first, first], "Duplicate"));
    var unchanged = Edit("PushButton_Off", "PushButton_Off", "tint");
    Reject("image", () => ImageProject.SetPngBatch(directory, p, catalog, [unchanged with { Geometry = unchanged.Geometry with { Width = unchanged.Geometry.Width + 1 } }], "Bad output"));
    Assert(Contract.Bytes(p).SequenceEqual(before), "Invalid batch input changed document/history");
});

Test("external working copies preserve project bytes, resume locally and reload one undoable image edit", () =>
{
    var directory = Path.Combine(root, "external-project"); var working = Path.Combine(root, "external-working");
    var p = ProjectStore.Create(catalog, "External copy"); ProjectStore.Save(directory, p);
    var original = catalog.ResolveBytes("textures/" + catalog.Textures["PushButton_Off"].Attributes["file_name"]);
    var replacement = catalog.ResolveBytes("textures/" + catalog.Textures["PushButton_On"].Attributes["file_name"]);
    Assert(!original.SequenceEqual(replacement), "External fixture has identical images");
    var before = Contract.Bytes(p); var files = Directory.EnumerateFiles(directory, "*", SearchOption.AllDirectories).ToDictionary(path => path, Contract.HashFile);
    var session = ExternalImageSession.Create(directory, p, catalog, "PushButton_Off", working, (_, _) => throw new Exception("PNG should not be transcoded"));
    Assert(File.ReadAllBytes(session.WorkingPath).SequenceEqual(original) && Contract.Bytes(p).SequenceEqual(before) && files.All(entry => Contract.HashFile(entry.Key) == entry.Value), "Opening an external copy changed source bytes or the project");
    var restored = ExternalImageSession.FindPrevious(directory, p, "PushButton_Off", working);
    Assert(restored != null && restored.WorkingPath == session.WorkingPath && restored.IsCurrent(directory, p, catalog), "Working copy was not resumable");
    using var signal = new ManualResetEventSlim();
    using var watcher = session.Watch(() => { try { signal.Set(); } catch (ObjectDisposedException) { } });
    Assert(watcher.Filter == "image.png" && !watcher.IncludeSubdirectories, "Watcher includes unrelated files");
    Contract.AtomicWrite(session.WorkingPath, replacement);
    Assert(signal.Wait(TimeSpan.FromSeconds(3)) && session.HasChanges, "Explicit working file change was not detected");
    p.Edits.Colors["ThemeAccent"] = "0.2 0.8 0.5 1";
    Assert(session.Reload(directory, p, catalog, _ => { }) && p.Undo.Count == 1, "External reload did not make exactly one checkpoint");
    Assert(ImageProject.Read(directory, p, catalog, "PushButton_Off").SequenceEqual(replacement) && p.Edits.Recipes["PushButton_Off"].Operation == "external-edit", "External bytes/recipe were not retained");
    var asset = p.Edits.Assets.Single(item => item.Kind == "image");
    Assert(File.ReadAllBytes(Path.Combine(directory, asset.OriginalPath)).SequenceEqual(original), "External reload lost its original");
    Assert(!session.HasChanges && !session.Reload(directory, p, catalog, _ => { }) && p.Undo.Count == 1, "Unchanged Reload created an undo entry");
    ProjectStore.Save(directory, p); p = ProjectStore.Open(directory);
    Assert(ExternalImageSession.FindPrevious(directory, p, "PushButton_Off", working)?.IsCurrent(directory, p, catalog) == true, "Saved external state did not resume");
    Assert(p.UndoEdit() && ImageProject.Read(directory, p, catalog, "PushButton_Off").SequenceEqual(original) && !session.IsCurrent(directory, p, catalog), "External Undo did not restore inheritance or invalidate stale reload");
    Assert(p.RedoEdit() && session.IsCurrent(directory, p, catalog), "External Redo did not restore the accepted snapshot");
});

Test("external reload refuses wrong scopes, stale guides and incomplete files without changing history", () =>
{
    var directory = Path.Combine(root, "external-conflicts"); var working = Path.Combine(root, "external-conflict-working");
    var p = ProjectStore.Create(catalog, "External conflicts"); ProjectStore.Save(directory, p);
    var session = ExternalImageSession.Create(directory, p, catalog, "PushButton_Off", working, (_, _) => throw new Exception("Expected PNG"));
    var before = Contract.Bytes(p);
    Reject("external_scope", () => session.Reload(directory + "-other", p, catalog, _ => { }));
    Reject("external_scope", () => session.Reload(directory, ProjectStore.Create(catalog, "Other"), catalog, _ => { }));
    Reject("external_scope", () => ExternalImageSession.Create(directory, p, catalog, "PushButton_Off", Path.Combine(directory, "working"), (_, _) => []));
    Reject("external_scope", () => ExternalImageSession.Create(Path.GetPathRoot(directory)!, p, catalog, "PushButton_Off", working, (_, _) => []));
    var malformed = Path.Combine(working, Guid.NewGuid().ToString("N")); Directory.CreateDirectory(malformed);
    File.WriteAllText(Path.Combine(malformed, "session.json"), "{\"version\":1,\"source_fingerprint\":null,\"imported_hash\":null}");
    Assert(ExternalImageSession.FindPrevious(directory, p, "PushButton_Off", working)?.WorkingPath == session.WorkingPath, "Malformed recovery record hid a usable working copy");
    File.WriteAllBytes(session.WorkingPath, [1, 2, 3]);
    Reject("image", () => session.Reload(directory, p, catalog, _ => { }));
    Assert(Contract.Bytes(p).SequenceEqual(before), "Rejected external data changed the document");
    var bytes = catalog.ResolveBytes("textures/" + catalog.Textures["PushButton_Off"].Attributes["file_name"]);
    var width = (int)System.Buffers.Binary.BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(16, 4)); var height = (int)System.Buffers.Binary.BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(20, 4));
    var geometry = TextureGeometry.FromAttributes(width, height, catalog.Textures["PushButton_Off"].Attributes);
    ImageProject.SetGeometry(directory, p, catalog, "PushButton_Off", geometry with { Margins = geometry.Margins with { Left = geometry.Margins.Left + 1 } });
    before = Contract.Bytes(p);
    Reject("external_conflict", () => session.Reload(directory, p, catalog, _ => { }));
    Assert(Contract.Bytes(p).SequenceEqual(before) && File.ReadAllBytes(session.WorkingPath).SequenceEqual(new byte[] { 1, 2, 3 }), "Stale reload changed model or external file");
    var fresh = ExternalImageSession.Create(directory, p, catalog, "PushButton_Off", working, (_, _) => []);
    File.Delete(fresh.WorkingPath);
    Reject("external_missing", () => fresh.Reload(directory, p, catalog, _ => { }));
    Assert(Contract.Bytes(p).SequenceEqual(before), "Missing file changed history");
});

Test("pixel drawing connects strokes, preserves alpha and clips circle/brush edges", () =>
{
    var source = RasterImage.Fill(9, 9, ThemeColor.Parse("#00000000")); var ink = ThemeColor.Parse("#D85BFF80"); var pixel = new RgbaPixel(216, 91, 255, 128);
    var pen = source.Draw(PixelTool.Pen, [new(1, 1), new(7, 1), new(7, 7)], ink);
    Assert(pen.Pixel(4, 1) == pixel && pen.Pixel(7, 4) == pixel && pen.Pixel(4, 4).A == 0, "Pen skipped pixels or filled the path's interior");
    var line = source.Draw(PixelTool.Line, [new(0, 8), new(8, 0)], ink);
    Assert(Enumerable.Range(0, 9).All(index => line.Pixel(index, 8 - index) == pixel) && line.Pixel(0, 0).A == 0, "Line endpoints/diagonal or exact alpha changed");
    var circle = source.Draw(PixelTool.Circle, [new(4, 4), new(6, 4)], ink);
    Assert(circle.Pixel(2, 4) == pixel && circle.Pixel(6, 4) == pixel && circle.Pixel(4, 2) == pixel && circle.Pixel(4, 6) == pixel && circle.Pixel(4, 4).A == 0, "Circle cardinal points or hollow center changed");
    var edge = source.Draw(PixelTool.Circle, [new(0, 0), new(2, 0)], ink, 2);
    Assert(edge.Pixel(2, 0) == pixel && edge.Pixel(0, 2) == pixel, "Circle brush did not clip at image edges");
    var erased = pen.Draw(PixelTool.Pen, [new(1, 1)], ThemeColor.Parse("#11663300"));
    Assert(erased.Pixel(1, 1) == new RgbaPixel(17, 102, 51, 0) && source.Pixel(1, 1).A == 0, "Transparent brush blended pixels or changed the original");
    Reject("drawing", () => source.Draw(PixelTool.Inspect, [new(0, 0)], ink));
    Reject("drawing", () => source.Draw(PixelTool.Line, [new(0, 0)], ink));
    Reject("drawing", () => source.Draw(PixelTool.Pen, [new(-1, 0)], ink));
    Reject("drawing", () => source.Draw(PixelTool.Pen, [new(0, 0)], ink, 17));
});

Test("drawing gesture previews replace shapes and retain a bounded recipe without modifying source pixels", () =>
{
    var original = RasterImage.Fill(8, 8, ThemeColor.Parse("#0000")); var stroke = new PixelStroke(original, PixelTool.Line, new(1, 1), ThemeColor.Parse("#fff"));
    stroke.Move(new(6, 1)); stroke.Move(new(1, 6));
    Assert(stroke.Preview.Pixel(1, 4).A == 255 && stroke.Preview.Pixel(4, 1).A == 0 && original.Pixel(1, 4).A == 0, "Line preview accumulated earlier positions or mutated original");
    Assert(stroke.HasChanges && stroke.Recipe.Parameters["points"].GetArrayLength() == 2 && RecipeHistory.ReadOnlyReasons(stroke.Recipe).Length == 0, "Shape recipe lost its endpoints");
    Assert(!new PixelStroke(original, PixelTool.Pen, new(0, 0), ThemeColor.Parse("#0000")).HasChanges, "Identical pixels counted as an edit");
    var pen = new PixelStroke(original, PixelTool.Pen, new(0, 0), ThemeColor.Parse("#fff"));
    for (var index = 1; index < PixelStroke.PointLimit; ++index) pen.Move(new(index % 8, index / 8 % 8));
    var before = pen.Preview.ToRgba();
    Reject("drawing", () => pen.Move(new(0, 0)));
    Assert(pen.Preview.ToRgba().SequenceEqual(before) && pen.Recipe.Parameters["points"].GetArrayLength() == PixelStroke.PointLimit, "Rejected gesture changed pixels/recipe");
});

Test("HSV color selection preserves RGBA, grayscale and the hue seam", () =>
{
    foreach (var hex in new[] { "#FF0000FF", "#00FF0080", "#0000FF01", "#F6219C7F", "#33333344", "#11663300", "#00000000", "#FFFFFFFF" })
        Assert(HsvColor.FromColor(ThemeColor.Parse(hex)).ToColor().Hex() == hex, "HSV round trip changed " + hex);
    Assert(new HsvColor(360, 1, 1, .5).ToColor().Hex() == "#FF000080", "Hue seam or alpha rounding changed");
    Reject("color", () => new HsvColor(double.NaN, 1, 1, 1).ToColor());
    Reject("color", () => new HsvColor(0, 2, 1, 1).ToColor());
});

Test("tiled samples repeat exact RGBA pixels across partial edges", () =>
{
    byte[] rgba = [255, 0, 0, 255, 17, 31, 47, 0, 0, 255, 0, 128, 0, 0, 255, 64, 70, 80, 90, 255, 1, 2, 3, 1];
    var source = RasterImage.FromRgba(2, 3, rgba); var tiled = source.Tile(5, 7);
    Assert(tiled.Pixel(2, 0) == new RgbaPixel(255, 0, 0, 255) && tiled.Pixel(3, 6) == new RgbaPixel(17, 31, 47, 0), "Tile seams changed transparent pixels");
    Assert(tiled.Pixel(4, 4) == new RgbaPixel(0, 255, 0, 128) && tiled.Pixel(1, 5) == new RgbaPixel(1, 2, 3, 1), "Partial tile edges changed row order or alpha");
    Assert(source.Tile(1, 1).Pixel(0, 0) == source.Pixel(0, 0) && source.ToRgba().SequenceEqual(rgba), "Small sample or immutable input failed");
    Reject("image", () => source.Tile(0, 8));
    Reject("image", () => source.Tile(16384, 16384));
});

Test("image recipe pipelines retain each step and reject unknown nested semantics read-only", () =>
{
    var original = new Recipe("tint", new() { ["color"] = JsonSerializer.SerializeToElement("#64D8B2") });
    var resize = new Recipe("resize", new() { ["width"] = JsonSerializer.SerializeToElement(64), ["height"] = JsonSerializer.SerializeToElement(32), ["interpolation"] = JsonSerializer.SerializeToElement("bilinear") });
    var pipeline = RecipeHistory.Append(original, resize);
    pipeline = RecipeHistory.Append(pipeline, new("border", new() { ["thickness"] = JsonSerializer.SerializeToElement(2), ["color"] = JsonSerializer.SerializeToElement("#0008") }));
    Assert(pipeline.Operation == "pipeline" && pipeline.Parameters["steps"].GetArrayLength() == 3 && original.Operation == "tint", "Pipeline lost steps or mutated the original");
    var directory = Path.Combine(root, "pipeline"); var p = ProjectStore.Create(catalog, "Recipe pipeline"); p.Edits.Recipes["PushButton_Off"] = pipeline;
    ProjectStore.Save(directory, p);
    Assert(Contract.Bytes(ProjectStore.Open(directory).Edits.Recipes["PushButton_Off"]).SequenceEqual(Contract.Bytes(pipeline)), "Pipeline changed on reopen");
    var unknown = new Recipe("pipeline", new() { ["steps"] = JsonSerializer.SerializeToElement(new[] { new Recipe("future-blend.v4", new()) }, Contract.JsonOptions) });
    p.Redo.Add(new("Future recipe", new ProjectEdits { Recipes = new() { ["PushButton_Off"] = unknown } }));
    var bytes = Contract.Bytes(p); File.WriteAllBytes(Path.Combine(directory, ProjectStore.FileName), bytes);
    var document = ProjectStore.OpenDocument(directory);
    Assert(!document.CanEdit && document.ReadOnlyReasons.Any(reason => reason.Contains("future-blend.v4", StringComparison.Ordinal)), "Unknown nested operation was ignored");
    Assert(document.OriginalBytes.SequenceEqual(bytes), "Future nested operation bytes changed");
    var futureParameter = new Recipe("tint", new() { ["future_parameter"] = JsonSerializer.SerializeToElement(true) });
    Assert(RecipeHistory.ReadOnlyReasons(futureParameter).Length > 0, "Future parameter became an editable no-op");
    Reject("project_readonly", () => RecipeHistory.Append(futureParameter, resize));
});

Test("image asset preparation preserves originals and refuses changed stored bytes", () =>
{
    var directory = Path.Combine(root, "prepared-assets");
    var png = File.ReadAllBytes(Path.Combine(catalog.Root, catalog.Textures.Values.First(texture => texture.Source?.EndsWith(".png", StringComparison.Ordinal) == true).Source!));
    var original = File.ReadAllBytes(Path.Combine(catalog.Root, catalog.Textures.Values.First(texture => texture.Source?.EndsWith(".tga", StringComparison.Ordinal) == true).Source!));
    var asset = ProjectAssets.Prepare(directory, "viewer/textures/theme/test.png", png, original, ".tga");
    Assert(asset.OriginalPath.EndsWith(".tga", StringComparison.Ordinal) && File.ReadAllBytes(Path.Combine(directory, asset.OriginalPath)).SequenceEqual(original), "Imported TGA original changed");
    var next = ProjectAssets.Prepare(directory, asset.PayloadPath, png, preservedOriginal: asset.OriginalPath);
    Assert(next == asset, "Original reference or immutable current data changed");
    var both = Path.Combine(root, "rejected-original-options");
    Reject("project_asset", () => ProjectAssets.Prepare(both, asset.PayloadPath, png, original, ".tga", asset.OriginalPath));
    Assert(!Directory.Exists(both), "Rejected preparation created a directory");
    File.WriteAllBytes(Path.Combine(directory, asset.OriginalPath), [1, 2, 3]);
    Reject("project_asset", () => ProjectAssets.Store(directory, original, ".tga"));
    Assert(File.ReadAllBytes(Path.Combine(directory, asset.OriginalPath)).SequenceEqual(new byte[] { 1, 2, 3 }), "Changed external bytes were overwritten");
});
Test("clip rectangles and stretch guides keep native origins through resize and crop", () =>
{
    var top = TextureGeometry.FromAttributes(64, 32, catalog.Textures["Rounded_Rect_Top"].Attributes);
    var bottom = TextureGeometry.FromAttributes(64, 32, catalog.Textures["Rounded_Rect_Bottom"].Attributes);
    Assert(top.Clip == new NativeSlice(0, 32, 64, 16) && top.Margins == new SliceMargins(6, 8, 6, 0), "Top clip or relative stretch guide origin");
    Assert(bottom.Clip == new NativeSlice(0, 16, 64, 0) && bottom.Margins == new SliceMargins(6, 0, 6, 8), "Bottom clip or relative stretch guide origin");
    var source = RasterImage.Gradient(64, 32, ThemeColor.Parse("#f00"), ThemeColor.Parse("#00f"), true);
    Assert(top.VisibleImage(source).Pixel(0, 0) == source.Pixel(0, 0) && bottom.VisibleImage(source).Pixel(0, 0) == source.Pixel(0, 16), "Clip selected wrong physical rows");
    var resized = top.Resize(128, 64);
    Assert(resized.Clip == new NativeSlice(0, 64, 128, 32) && resized.Margins == new SliceMargins(12, 16, 12, 0), "Resize did not preserve clipped border geometry");
    var cropped = top.Crop(16, 0, 32, 16);
    Assert(cropped.Clip == new NativeSlice(0, 16, 32, 0) && cropped.Margins == new SliceMargins(0, 8, 0, 0), "Crop did not remove clipped-away fixed borders");
    Assert(TextureGeometry.FromAttributes(32, 16, cropped.ApplyTo(catalog.Textures["Rounded_Rect_Top"].Attributes)) == cropped, "Edited guide serialization changed geometry");
    Reject("texture", () => top.Crop(0, 20, 64, 12));
});

Test("image reset restores inheritance and history without deleting originals or unrelated edits", () =>
{
    var directory = Path.Combine(root, "image-reset"); var p = ProjectStore.Create(catalog, "Image reset");
    var names = new[] { "Rounded_Rect_Top", "Rounded_Rect_Bottom" };
    var images = names.Select(name => catalog.Textures[name]).ToArray();
    Assert(!ImageProject.Reset(directory, p, catalog, names) && p.Undo.Count == 0 && !Directory.Exists(directory), "No-op reset created files or history");
    p.Edits.Colors["ThemeAccent"] = "0.2 0.8 0.5 1";
    var license = ProjectAssets.Prepare(directory, "licenses/artwork.txt", System.Text.Encoding.UTF8.GetBytes("Retained artwork license"));
    p.Edits.Assets.Add(license);
    foreach (var image in images)
    {
        var bytes = catalog.ResolveBytes("textures/" + image.Attributes["file_name"]);
        var width = (int)System.Buffers.Binary.BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(16, 4));
        var height = (int)System.Buffers.Binary.BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(20, 4));
        ImageProject.SetPng(directory, p, catalog, image.Name, bytes, TextureGeometry.FromAttributes(width, height, image.Attributes),
            new("tint", new() { ["color"] = JsonSerializer.SerializeToElement("#fff") }));
    }
    var before = Contract.Bytes(p.Edits); var history = p.Undo.Count;
    var files = Directory.EnumerateFiles(directory, "*", SearchOption.AllDirectories).ToDictionary(path => path, Contract.HashFile);
    Assert(ImageProject.Reset(directory, p, catalog, [names[0]]) && p.Undo.Count == history + 1, "Reset did not make exactly one checkpoint");
    Assert(ImageProject.Resolve(directory, p, catalog, names[0]).Attributes["file_name"] == images[0].Attributes["file_name"] && !p.Edits.Recipes.ContainsKey(names[0]), "Reset did not restore inherited registration/recipe");
    Assert(ImageProject.Resolve(directory, p, catalog, names[1]).Attributes["file_name"].StartsWith("theme/", StringComparison.Ordinal) && p.Edits.Recipes.ContainsKey(names[1]), "Reset changed the shared-file sibling");
    Assert(files.All(entry => File.Exists(entry.Key) && Contract.HashFile(entry.Key) == entry.Value), "Reset deleted or rewrote original/history files");
    Assert(p.UndoEdit() && Contract.Bytes(p.Edits).SequenceEqual(before), "Undo failed to restore the prior image state");
    Assert(p.RedoEdit() && ImageProject.Reset(directory, p, catalog, names), "Group reset failed after redo");
    Assert(!p.Edits.Assets.Any(asset => asset.PayloadPath.StartsWith("viewer/textures/", StringComparison.Ordinal)) && p.Edits.Assets.Contains(license) && p.Edits.Colors.Count == 1, "Group reset flattened inherited textures or removed unrelated edits");
    history = p.Undo.Count;
    Assert(!ImageProject.Reset(directory, p, catalog, names) && p.Undo.Count == history, "Repeated reset created an empty undo entry");
    Reject("texture", () => ImageProject.Reset(directory, p, catalog, ["MissingImageId"]));
    Assert(!ImageProject.ResetAll(p, catalog), "Reset all reported changes without image edits");
});

Test("image preview binds only the native sample button and leaves the source project intact", () =>
{
    var before = Contract.Bytes(project!);
    var paths = Directory.EnumerateFiles(projectDirectory, "*", SearchOption.AllDirectories).ToDictionary(path => path, Contract.HashFile);
    var theme = PackageStore.CompileImagePreview(projectDirectory, project!, catalog, "Inv_FolderClosed");
    var sample = Contract.Xml(theme.Files["viewer/xui/en/floater_test_widgets.xml"]);
    var button = sample.Descendants("button").Single(node => (string?)node.Attribute("name") == "test_button");
    Assert((string?)button.Attribute("image_unselected") == "Inv_FolderClosed" && (string?)button.Attribute("image_pressed") == "Inv_FolderClosed" && (string?)button.Attribute("label") == "", "Native preview image bindings");
    Assert(!theme.Files.ContainsKey("viewer/xui/en/widgets/button.xml") && theme.Manifest.RequiredCapabilities.Contains("viewer.xui.v1"), "Preview changed global button defaults or missed capability");
    Assert(Contract.Bytes(project!).SequenceEqual(before) && paths.Count == Directory.EnumerateFiles(projectDirectory, "*", SearchOption.AllDirectories).Count() && paths.All(entry => Contract.HashFile(entry.Key) == entry.Value), "Preview changed source project files or history");
    Reject("texture", () => PackageStore.CompileImagePreview(projectDirectory, project!, catalog, "MissingImageId"));
});

FontChecks.Register(Test, root, catalog);
FontLicenseChecks.Register(Test, root, catalog);
XuiProjectChecks.Register(Test, root, catalog);
if (args.Length == 2) tests = tests.Where(test => test.Name.StartsWith(args[1] == "--fonts-only" ? "font" : "xui", StringComparison.Ordinal)).ToList();
var failed = 0;
foreach (var (name, run) in tests)
{
    try { run(); Console.WriteLine("PASS " + name); }
    catch (Exception error) { ++failed; Console.Error.WriteLine("FAIL " + name + "\n" + error); }
}
Console.WriteLine($"{tests.Count - failed}/{tests.Count} passed. Evidence workspace: {root}");
return failed == 0 ? 0 : 1;
