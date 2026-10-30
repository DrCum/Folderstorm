// SPDX-License-Identifier: LGPL-2.1-or-later
using Folderstorm.Theme;
using System.Text;

internal static class HostProjectChecks
{
    public static void Register(Action<string, Action> test, string root, BaseCatalog catalog)
    {
        void Assert(bool value, string message) { if (!value) throw new Exception(message); }
        void Reject(string code, Action action) { try { action(); throw new Exception("Expected " + code); } catch (ThemeException error) { Assert(error.Code == code, error.Message); } }
        var directory = Path.Combine(root, "host-project"); ThemeProject? source = null; ValidatedTheme? theme = null;
        var importedDirectory = Path.Combine(root, "host-import"); ThemeProject? imported = null;
        test("host project stages one edit with feature marker, derived capability and durable history", () =>
        {
            source = ProjectStore.Create(catalog, "Host transaction"); source.Edits.Colors["ThemeAccent"] = ThemeColor.Parse("#DD44AA").NativeText;
            source.Edits.Assets.Add(ProjectAssets.Prepare(directory, "licenses/source.txt", Encoding.UTF8.GetBytes("Preserve source license")));
            Assert(HostProject.SetColor(directory, source, catalog, "accent", "#64D8B2") && source.Undo.Count == 1 && source.Edits.HostPaletteVersion == 1, "Host edit was not one checkpoint");
            theme = PackageStore.Compile(directory, source, catalog);
            Assert(theme.Manifest.RequiredCapabilities.Contains("host.palette.v1") && HostPalette.Read(theme.Files[HostProject.PayloadPath]).Colors["accent"] == "#64D8B2", "Host payload/capability missing");
            Assert(source.Edits.Colors["ThemeAccent"] == ThemeColor.Parse("#DD44AA").NativeText && theme.Files.ContainsKey("licenses/source.txt"), "Host edit changed viewer/unrelated data");
            var before = Contract.Bytes(source); Assert(!HostProject.SetColor(directory, source, catalog, "accent", "#64d8b2FF") && Contract.Bytes(source).SequenceEqual(before), "Equivalent RGB added history");
            ProjectStore.Save(directory, source); source = ProjectStore.Open(directory);
            Assert(source.UndoEdit() && source.Edits.HostPaletteVersion == null && HostProject.Read(directory, source) == null && source.RedoEdit() && source.Edits.HostPaletteVersion == 1, "Durable host Undo/Redo lost marker or data");
            ProjectStore.Save(directory, source);
        });
        test("host project import keeps exact compiled bytes and original palette through subsequent edits", () =>
        {
            var files = theme!.Files.ToDictionary(file => file.Key, file => file.Value.ToArray(), StringComparer.Ordinal);
            var raw = Encoding.UTF8.GetBytes("\n" + Encoding.UTF8.GetString(files[HostProject.PayloadPath]).Replace("#64D8B2", "#64d8b2", StringComparison.Ordinal)); files[HostProject.PayloadPath] = raw;
            var header = theme.Manifest with { Files = theme.Manifest.Files.Select(file => file.Path == HostProject.PayloadPath ? file with { Size = raw.Length, Sha256 = Contract.Hash(raw) } : file).ToArray() };
            files["theme.json"] = Contract.Bytes(header); var exact = PackageStore.Validate(files, catalog);
            imported = PackageStore.ImportProject(importedDirectory, exact, catalog);
            Assert(imported.Edits.HostPaletteVersion == 1 && imported.Undo.Count == 0 && PackageStore.Compile(importedDirectory, imported, catalog).Revision == exact.Revision, "Host import lost exact revision or invented history");
            var before = Contract.Bytes(imported); Assert(!HostProject.SetColor(importedDirectory, imported, catalog, "accent", "#64D8B2") && Contract.Bytes(imported).SequenceEqual(before), "Imported equivalent input changed bytes/history");
            Assert(HostProject.SetColor(importedDirectory, imported, catalog, "panel", "#123456") && imported.Undo.Count == 1, "Imported host edit failed");
            var asset = imported.Edits.Assets.Single(asset => asset.PayloadPath == HostProject.PayloadPath);
            Assert(File.ReadAllBytes(Contract.Contained(importedDirectory, asset.OriginalPath)).SequenceEqual(raw), "Imported host original changed");
            Assert(imported.UndoEdit() && PackageStore.Compile(importedDirectory, imported, catalog).Revision == exact.Revision && imported.RedoEdit(), "Host import Undo lost exact compiled bytes");
            ProjectStore.Save(importedDirectory, imported);
            var revision = ThemeLibrary.Publish(PackageStore.Compile(importedDirectory, imported, catalog), Path.Combine(root, "host-native-library"));
            Contract.AtomicWrite(Path.Combine(root, "host-native-result.json"), Contract.Bytes(new { path = revision, revision = Path.GetFileName(revision), project = importedDirectory }));
        });
        test("host project resets clear only host overrides and capability while preserving originals and history", () =>
        {
            var project = imported!; var before = Contract.Bytes(project.Edits); var count = project.Undo.Count;
            var paths = Directory.EnumerateFiles(importedDirectory, "*", SearchOption.AllDirectories).ToDictionary(path => path, Contract.HashFile);
            Assert(HostProject.Reset(importedDirectory, project, catalog) && project.Undo.Count == count + 1 && project.Edits.HostPaletteVersion == null, "Host reset did not clear its marker once");
            var reset = PackageStore.Compile(importedDirectory, project, catalog);
            Assert(!reset.Files.ContainsKey(HostProject.PayloadPath) && !reset.Manifest.RequiredCapabilities.Contains("host.palette.v1") && reset.Files.ContainsKey("viewer/colors.xml") && reset.Files.ContainsKey("licenses/source.txt"), "Host reset retained a derived capability or removed unrelated resources");
            Assert(project.UndoEdit() && Contract.Bytes(project.Edits).SequenceEqual(before), "Host reset Undo lost data");
            var review = ProjectChanges.Inspect(importedDirectory, project, catalog);
            Assert(ProjectChanges.Reset(importedDirectory, project, catalog, new(review.EditsHash, "file:host/palette.json")) && project.Edits.HostPaletteVersion == null && project.UndoEdit(), "Changes selected-host reset left a stale marker");
            review = ProjectChanges.Inspect(importedDirectory, project, catalog);
            Assert(ProjectChanges.Reset(importedDirectory, project, catalog, new(review.EditsHash, Group: ChangeGroup.Host)) && project.Edits.HostPaletteVersion == null && project.UndoEdit(), "Changes host-group reset failed");
            Assert(paths.All(path => File.Exists(path.Key) && Contract.HashFile(path.Key) == path.Value), "Reset removed/replaced originals");
            var future = Path.Combine(root, "host-future"); Directory.CreateDirectory(future);
            var bytes = Contract.Bytes(project with { Edits = project.Edits with { HostPaletteVersion = 2 } }); File.WriteAllBytes(Path.Combine(future, ProjectStore.FileName), bytes);
            var document = ProjectStore.OpenDocument(future); Assert(!document.CanEdit && document.OriginalBytes.SequenceEqual(bytes), "Unsupported host edit opened writable");
            Reject("project_readonly", () => ProjectStore.Save(future, document.Project!));
        });
        test("host project invalid edits and old preview capabilities fail before document or library changes", () =>
        {
            var project = source!; var before = Contract.Bytes(project);
            Reject("host_palette", () => HostProject.SetColor(directory, project, catalog, "accent", "#12345680"));
            Assert(Contract.Bytes(project).SequenceEqual(before), "Rejected alpha changed document");
            project.PackageVersion = "invalid"; before = Contract.Bytes(project);
            Reject("identity", () => HostProject.SetColor(directory, project, catalog, "accent", "#123456"));
            Assert(Contract.Bytes(project).SequenceEqual(before), "Failed compilation changed document/history"); project.PackageVersion = "1.0.0";
            var fake = Path.Combine(root, "host-old-viewer"); Directory.CreateDirectory(fake); var executable = Path.Combine(fake, "viewer.exe"); File.WriteAllBytes(executable, []);
            var capabilities = Contract.Bytes(new { preview_version = 1, source_commit = new string('a', 40) }); File.WriteAllBytes(Path.Combine(fake, "folderstorm-theme-capabilities.json"), capabilities);
            Reject("preview_unsupported", () => PreviewSession.ValidateCapabilities(capabilities, false, requiresHostPalette: true));
            PreviewSession.ValidateCapabilities(Contract.Bytes(new { preview_version = 1, source_commit = new string('a', 40), host_palette_version = 1 }), false, requiresHostPalette: true);
            var target = Path.Combine(ThemeLibrary.DefaultRoot, theme!.Manifest.ThemeId, theme.Revision); var existed = Directory.Exists(target); var preview = new PreviewSession();
            try { Reject("preview_unsupported", () => preview.StartAsync(executable, theme, catalog).GetAwaiter().GetResult()); Assert(preview.ProcessId == null && preview.DirectoryPath.Length == 0 && Directory.Exists(target) == existed, "Unsupported host format published or launched"); }
            finally { preview.DisposeAsync().AsTask().GetAwaiter().GetResult(); }
            var files = theme.Files.ToDictionary(file => file.Key, file => file.Value.ToArray(), StringComparer.Ordinal);
            files["theme.json"] = Contract.Bytes(theme.Manifest with { RequiredCapabilities = theme.Manifest.RequiredCapabilities.Where(value => value != "host.palette.v1").ToArray() });
            Reject("capability", () => PackageStore.Validate(files, catalog));
        });
        test("host project viewer-color snapshot composites opacity explicitly without modifying its source", () =>
        {
            var project = ProjectStore.Create(catalog, "Host viewer snapshot"); project.Edits.Colors["ThemeBg"] = "0 0 0 1"; project.Edits.Colors["ThemeAccent"] = "1 0 0 0.5";
            var before = Contract.Bytes(project); var palette = HostProject.FromViewer("", project, catalog);
            Assert(palette.Colors["accent"] == "#800000" && palette.Colors["background"] == "#000000" && Contract.Bytes(project).SequenceEqual(before), "Viewer color snapshot lost opacity or changed source");
        });
    }
}
