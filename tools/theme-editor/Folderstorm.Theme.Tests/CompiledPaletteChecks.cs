// SPDX-License-Identifier: LGPL-2.1-or-later
using Folderstorm.Theme;
using System.Text;
using System.Text.Json;
using System.Xml.Linq;

internal static class CompiledPaletteChecks
{
    public static void Register(Action<string, Action> test, string root, BaseCatalog catalog)
    {
        void Assert(bool value, string message) { if (!value) throw new Exception(message); }
        void Reject(string code, Action action) { try { action(); throw new Exception("Expected " + code); } catch (ThemeException error) { Assert(error.Code == code, error.Message); } }
        var folder = Path.Combine(root, "compiled-color-project"); ThemeProject? imported = null; ValidatedTheme? original = null;
        test("compiled palette preserves imported values, exact no-op bytes and explicit overrides through staged edits", () =>
        {
            var source = Path.Combine(root, "compiled-color-source"); var project = ProjectStore.Create(catalog, "Mint package");
            ColorAuthoring.SetLiteral(project, catalog, "ThemeAccent", "#64D8B2");
            var license = ProjectAssets.Prepare(source, "licenses/source.txt", Encoding.UTF8.GetBytes("Keep this text")); project.Edits.Assets.Add(license);
            original = PackageStore.Compile(source, project, catalog); imported = PackageStore.ImportProject(folder, original, catalog);
            var state = PaletteProject.Read(folder, imported, catalog);
            Assert(state.Inspect("ThemeAccent").Value.Hex(false) == "#64D8B2" && state.Inspect("AccentColor").Value.Hex(false) == "#64D8B2" && state.Imported.ContainsKey("AccentColor"), "Imported colors/provenance missing");
            Assert(imported.Undo.Count == 0 && imported.Origin == "compiled-package" && imported.Edits.CompiledManifestPath != null, "Import invented history or lost compiled source");
            var before = Contract.Bytes(imported); var files = Directory.EnumerateFiles(folder, "*", SearchOption.AllDirectories).ToDictionary(path => path, Contract.HashFile);
            Assert(!ColorAuthoring.SetLiteral(imported, catalog, "ThemeAccent", "#64D8B2", folder) && Contract.Bytes(imported).SequenceEqual(before), "Matching imported value created an edit");
            var copy = PackageStore.Compile(folder, imported, catalog); Assert(copy.Files.All(file => original.Files[file.Key].SequenceEqual(file.Value)), "No-op import changed runtime bytes");
            PaletteProject.Preview(imported, catalog, [new("ThemeAccent", "#FF0000")], folder);
            Assert(Contract.Bytes(imported).SequenceEqual(before) && files.Count == Directory.EnumerateFiles(folder, "*", SearchOption.AllDirectories).Count() && files.All(file => Contract.HashFile(file.Key) == file.Value), "Preview mutated project/files");
            Assert(ColorAuthoring.SetLiteral(imported, catalog, "ThemeAccent", "#FF0000", folder) && imported.Undo.Count == 1, "Staged edit did not create one checkpoint");
            state = PaletteProject.Read(folder, imported, catalog);
            Assert(state.Inspect("ThemeAccent").Value.Hex(false) == "#FF0000" && state.Inspect("AccentColor").Value.Hex(false) == "#64D8B2", "Edit inferred links over explicit compiled literals");
            var edited = PackageStore.Compile(folder, imported, catalog); Assert(edited.Files["licenses/source.txt"].SequenceEqual(original.Files["licenses/source.txt"]), "Palette edit changed unrelated payload");
            Assert(imported.UndoEdit() && PackageStore.Compile(folder, imported, catalog).Revision == original.Revision && imported.RedoEdit(), "Undo/Redo lost exact imported revision");
            ProjectStore.Save(folder, imported); imported = ProjectStore.Open(folder);
            Assert(PaletteProject.Read(folder, imported, catalog).Inspect("ThemeAccent").Value.Hex(false) == "#FF0000" && imported.Undo.Count == 1, "Save/reopen lost palette/history");
            var raw = imported.Edits.Assets.Single(asset => asset.PayloadPath == PaletteProject.PayloadPath);
            Assert(File.ReadAllBytes(Contract.Contained(folder, raw.OriginalPath)).SequenceEqual(original.Files[PaletteProject.PayloadPath]), "Original palette bytes changed");
        });
        test("compiled palette reset removes only selected raw overrides; failed cycles and validation retain document and history", () =>
        {
            var directory = Path.Combine(root, "raw-reference-project"); var project = ProjectStore.Create(catalog, "Reference package");
            var xml = Encoding.UTF8.GetBytes("<colors><!--Keep this comment--><color name=\"ThemeAccent\" value=\"0.1 0.7 0.4 1\"/><color name=\"AnsaStorm_HtmlLink\" reference=\"ThemeAccent\"/><color name=\"ThemeBg\" value=\"0.1 0.2 0.3 1\"/><color name=\"ButtonImageColor\" reference=\"ChicletFlashColor\"/></colors>\n");
            project.Edits.Assets.Add(ProjectAssets.Prepare(directory, PaletteProject.PayloadPath, xml));
            var baseline = PaletteProject.Read(directory, project, catalog);
            Assert(baseline.Inspect("AnsaStorm_HtmlLink").ReferenceChain.SequenceEqual(new[] { "AnsaStorm_HtmlLink", "ThemeAccent" }), "Imported reference chain missing");
            Assert(baseline.Inspect("AccentColor").Value.NativeText == catalog.InspectColor("AccentColor", new Dictionary<string, string>()).Value.NativeText, "Import retroactively re-resolved a previously loaded base alias");
            var before = Contract.Bytes(project); var paths = Directory.EnumerateFiles(directory, "*", SearchOption.AllDirectories).ToDictionary(path => path, Contract.HashFile);
            Reject("color_cycle", () => ColorAuthoring.SetLink(project, catalog, "ThemeAccent", "AnsaStorm_HtmlLink", directory));
            Assert(Contract.Bytes(project).SequenceEqual(before), "Rejected imported cycle changed history");
            var preview = PaletteProject.Preview(project, catalog, [new("ThemeAccent")], directory);
            Assert(preview.ImportedChanged && paths.Count == Directory.EnumerateFiles(directory, "*", SearchOption.AllDirectories).Count(), "Raw reset preview wrote files");
            ColorAuthoring.SetLiteral(project, catalog, "ThemeBg", "#123456", directory);
            var compiled = PackageStore.Compile(directory, project, catalog); var raw = Contract.Xml(compiled.Files[PaletteProject.PayloadPath]);
            Assert((string?)raw.Root!.Elements("color").Single(node => (string?)node.Attribute("name") == "ButtonImageColor").Attribute("reference") == "ChicletFlashColor", "Unrelated native-only reference was flattened");
            var history = project.Undo.Count; Assert(ColorAuthoring.Reset(project, catalog, "ThemeAccent", directory) && project.Undo.Count == history + 1, "Raw reset checkpoint missing");
            var state = PaletteProject.Read(directory, project, catalog);
            Assert(!state.Imported.ContainsKey("ThemeAccent") && state.Imported.ContainsKey("ThemeBg") && state.Inspect("AnsaStorm_HtmlLink").Value.NativeText == catalog.InspectColor("ThemeAccent", new Dictionary<string, string>()).Value.NativeText, "Scoped reset lost unrelated overrides or inherited target");
            Assert(project.UndoEdit() && PaletteProject.Read(directory, project, catalog).Imported.ContainsKey("ThemeAccent"), "Raw reset Undo lost imported node");
            project.PackageVersion = "invalid"; before = Contract.Bytes(project);
            Reject("identity", () => ColorAuthoring.SetLiteral(project, catalog, "ThemeBg", "#654321", directory)); Assert(Contract.Bytes(project).SequenceEqual(before), "Failed full compilation changed palette/history"); project.PackageVersion = "1.0.0";
            Assert(PaletteProject.ResetAll(directory, project, catalog) && project.Edits.Colors.Count == 0 && project.Edits.ColorLinks.Count == 0 && project.Edits.Assets.All(asset => asset.PayloadPath != PaletteProject.PayloadPath), "Reset all retained active raw palette");
            Assert(project.UndoEdit() && PaletteProject.Read(directory, project, catalog).Imported.Count == 4 && paths.All(file => Contract.HashFile(file.Key) == file.Value), "Palette reset erased originals");
            var emptyDirectory = Path.Combine(root, "empty-raw-palette"); var empty = ProjectStore.Create(catalog, "Empty palette");
            empty.Edits.Assets.Add(ProjectAssets.Prepare(emptyDirectory, PaletteProject.PayloadPath, [])); before = Contract.Bytes(empty);
            Reject("xml", () => PackageStore.Compile(emptyDirectory, empty, catalog)); Assert(Contract.Bytes(empty).SequenceEqual(before), "Empty palette was silently dropped or changed the project");
        });
        test("compiled import preserves manifest formatting, optional capability and font metadata through no-op and metadata edits", () =>
        {
            var theme = original!;
            var font = Path.GetFileName(Directory.EnumerateFiles(Path.Combine(catalog.Root, "fonts"), "*.ttf").First());
            var header = theme.Manifest with { RequiredCapabilities = [.. theme.Manifest.RequiredCapabilities, "viewer.preview.v1"], FontDependencies = [new("installed", font, 2)] };
            using var parsed = JsonDocument.Parse(Contract.Bytes(header)); var bytes = Encoding.UTF8.GetBytes(JsonSerializer.Serialize(parsed.RootElement) + "\r\n  ");
            var files = new Dictionary<string, byte[]>(theme.Files, StringComparer.Ordinal) { ["theme.json"] = bytes }; var input = PackageStore.Validate(files, catalog);
            var directory = Path.Combine(root, "manifest-fidelity-project"); var project = PackageStore.ImportProject(directory, input, catalog);
            Assert(PackageStore.Compile(directory, project, catalog).Files["theme.json"].SequenceEqual(bytes), "Import normalized external manifest bytes");
            project.DisplayName = "Renamed package"; var renamed = PackageStore.Compile(directory, project, catalog);
            Assert(renamed.Manifest.FontDependencies.Single().Face == 2 && renamed.Manifest.RequiredCapabilities.Contains("viewer.preview.v1"), "Metadata edit lost declared font/capability data");
            ColorAuthoring.SetLiteral(project, catalog, "ThemeBg", "#123456", directory); var changed = PackageStore.Compile(directory, project, catalog);
            Assert(changed.Manifest.FontDependencies.Single().Face == 2 && changed.Manifest.RequiredCapabilities.Contains("viewer.preview.v1"), "Unrelated payload edit lost declared metadata");
            var revision = Path.Combine(directory, "native-revision"); Directory.CreateDirectory(revision);
            foreach (var file in changed.Files) Contract.AtomicWrite(Contract.Contained(revision, file.Key), file.Value);
            Contract.AtomicWrite(Path.Combine(directory, "result.json"), Contract.Bytes(new { Path = directory, Revision = changed.Revision, NativeRevision = revision }));
        });
        test("compiled import rejects overwrite, library nesting and mutated packages before creating the final destination", () =>
        {
            var library = Path.Combine(root, "protected-library"); Directory.CreateDirectory(library);
            Reject("project_overlap", () => PackageStore.ImportProject(Path.Combine(library, "new-project"), original!, catalog, library));
            var file = Path.Combine(root, "occupied-project"); File.WriteAllText(file, "Keep me"); Reject("project_exists", () => PackageStore.ImportProject(file, original!, catalog)); Assert(File.ReadAllText(file) == "Keep me", "Existing destination changed");
            Reject("project_exists", () => PackageStore.ImportProject(folder, original!, catalog));
            var destination = Path.Combine(root, "invalid-import"); var invalid = original! with { Revision = new string('f', 64) };
            var stages = Directory.EnumerateDirectories(root, ".theme-import-*").ToArray(); Reject("integrity", () => PackageStore.ImportProject(destination, invalid, catalog));
            Assert(!Directory.Exists(destination) && stages.SequenceEqual(Directory.EnumerateDirectories(root, ".theme-import-*")), "Rejected import exposed a destination/stage");
            Assert(!Directory.EnumerateFiles(root, "custom-theme.json", SearchOption.AllDirectories).Any(), "Import selected a viewer profile");
        });
        test("compiled import failure after staging leaves the final destination absent and retains recoverable bytes", () =>
        {
            var files = new Dictionary<string, byte[]>(original!.Files, StringComparer.Ordinal); var payload = Encoding.UTF8.GetBytes("L");
            for (var index = 0; index < 8000; ++index) files.Add("licenses/" + index.ToString("D4") + "-" + new string('l', 160) + ".txt", payload);
            var header = original.Manifest with { Files = files.Where(file => file.Key != "theme.json").OrderBy(file => file.Key, StringComparer.Ordinal).Select(file => new Payload(file.Key, file.Value.Length, Contract.Hash(file.Value), PackageStore.Kind(file.Key))).ToArray() };
            files["theme.json"] = Contract.Bytes(header); var input = PackageStore.Validate(files, catalog);
            var directory = Path.Combine(root, "failed-stage-parent"); Directory.CreateDirectory(directory); var destination = Path.Combine(directory, "final-project");
            Reject("project_import", () => PackageStore.ImportProject(destination, input, catalog));
            var stages = Directory.EnumerateDirectories(directory, ".theme-import-*").ToArray();
            Assert(!Directory.Exists(destination) && stages.Length == 1 && Directory.EnumerateFiles(stages[0], "*.json", SearchOption.AllDirectories).Any(), "Failed large project became visible or lost its recovery stage");
            Assert(Contract.Hash(input.Files["theme.json"]) == input.Revision, "Failed import changed source bytes");
        });
    }
}
