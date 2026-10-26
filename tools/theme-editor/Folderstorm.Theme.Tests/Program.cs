// SPDX-License-Identifier: LGPL-2.1-or-later
using Folderstorm.Theme;
using System.Text;
using System.Text.Json;
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

var failed = 0;
foreach (var (name, run) in tests)
{
    try { run(); Console.WriteLine("PASS " + name); }
    catch (Exception error) { ++failed; Console.Error.WriteLine("FAIL " + name + "\n" + error); }
}
Console.WriteLine($"{tests.Count - failed}/{tests.Count} passed. Evidence workspace: {root}");
return failed == 0 ? 0 : 1;
