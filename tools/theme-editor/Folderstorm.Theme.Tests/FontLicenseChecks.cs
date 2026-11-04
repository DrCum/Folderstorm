// SPDX-License-Identifier: LGPL-2.1-or-later
using Folderstorm.Theme;
using System.Text;

internal static class FontLicenseChecks
{
    public static void Register(Action<string, Action> test, string root, BaseCatalog catalog)
    {
        void Assert(bool condition, string message) { if (!condition) throw new Exception(message); }
        void Reject(string code, Action action) { try { action(); throw new Exception("Expected " + code); } catch (ThemeException error) { Assert(error.Code == code, error.Message); } }
        test("font license edits retain exact text, create one checkpoint and export with Undo/Redo", () =>
        {
            var directory = Path.Combine(root, "font-licenses"); var project = ProjectStore.Create(catalog, "License tools");
            var bytes = File.ReadAllBytes(Path.Combine(catalog.Root, "fonts", "InterVariableFont.ttf")); var config = FontProject.Read(directory, project, catalog);
            var role = FontProject.Roles(config).First(role => role.Name == "Monospace"); FontProject.Embed(directory, project, catalog, role.Index, ".ttf", bytes, "Original license\r\nРусский");
            var file = FontProject.References(FontProject.Read(directory, project, catalog), role.Index)[0].File; var license = FontProject.ReadLicense(directory, project, file); var history = project.Undo.Count;
            Assert(license.Exists && license.Text == "Original license\r\nРусский", "Embedded license not found by font content");
            Assert(!FontProject.SetLicense(directory, project, catalog, file, license.Text) && project.Undo.Count == history, "Unchanged license added history");
            var configBytes = FontProject.Read(directory, project, catalog); const string replacement = "Updated license\n© Font maker\n中文";
            Assert(FontProject.SetLicense(directory, project, catalog, file, replacement) && project.Undo.Count == history + 1 && FontProject.Read(directory, project, catalog).SequenceEqual(configBytes), "License edit changed font config or added extra checkpoints");
            Assert(project.UndoEdit() && FontProject.ReadLicense(directory, project, file).Text == license.Text && project.RedoEdit() && FontProject.ReadLicense(directory, project, file).Text == replacement, "License Undo/Redo changed text");
            Assert(FontProject.RemoveLicense(directory, project, catalog, file) && !FontProject.ReadLicense(directory, project, file).Exists, "License removal failed"); history = project.Undo.Count;
            Assert(!FontProject.RemoveLicense(directory, project, catalog, file) && project.Undo.Count == history && project.UndoEdit() && FontProject.ReadLicense(directory, project, file).Text == replacement, "License remove no-op/Undo failed");
            project.Edits.Assets.Add(ProjectAssets.Prepare(directory, "licenses/readme.txt", Encoding.UTF8.GetBytes("Keep this unrelated license")));
            ProjectStore.Save(directory, project); var reopened = ProjectStore.Open(directory); Assert(FontProject.ReadLicense(directory, reopened, file).Text == replacement, "License save/reopen lost Unicode or line endings");
            var theme = PackageStore.Compile(directory, reopened, catalog); Assert(theme.Files[license.PayloadPath].SequenceEqual(Encoding.UTF8.GetBytes(replacement)) && theme.Files["viewer/fonts/" + file].SequenceEqual(bytes), "Export changed license or font bytes");
            var path = ThemeLibrary.Publish(theme, Path.Combine(root, "font-license-library")); Contract.AtomicWrite(Path.Combine(directory, "result.json"), Contract.Bytes(new { revision = theme.Revision, path }));
            Assert(FontProject.Reset(reopened) && reopened.Edits.Assets.Single().PayloadPath == "licenses/readme.txt" && reopened.UndoEdit() && FontProject.ReadLicense(directory, reopened, file).Text == replacement, "Font reset touched unrelated license or lost recovery");
        });
        test("font licenses refuse installed paths, blank/oversized data and preserve failed or unsupported edits", () =>
        {
            var directory = Path.Combine(root, "font-license-errors"); var project = ProjectStore.Create(catalog, "License failures");
            var bytes = File.ReadAllBytes(Path.Combine(catalog.Root, "fonts", "InterVariableFont.ttf")); var role = FontProject.Roles(FontProject.Read(directory, project, catalog)).First(role => role.Name == "Monospace");
            FontProject.Embed(directory, project, catalog, role.Index, ".ttf", bytes); var file = FontProject.References(FontProject.Read(directory, project, catalog), role.Index)[0].File;
            var before = Contract.Bytes(project); Reject("font_license", () => FontProject.SetLicense(directory, project, catalog, "InterVariableFont.ttf", "Bad target"));
            Reject("path", () => FontProject.ReadLicense(directory, project, "../bad.ttf")); Reject("font_license", () => FontProject.SetLicense(directory, project, catalog, file, "  "));
            Reject("size", () => FontProject.SetLicense(directory, project, catalog, file, new string('x', Contract.ManifestLimit + 1)));
            Assert(Contract.Bytes(project).SequenceEqual(before), "Rejected license input changed history");
            project.Edits.ColorLinks["ThemeAccent"] = "ThemeAccent"; before = Contract.Bytes(project);
            Reject("color_cycle", () => FontProject.SetLicense(directory, project, catalog, file, "Should not commit")); Assert(Contract.Bytes(project).SequenceEqual(before), "Failed staged compile changed license/history"); project.Edits.ColorLinks.Clear();
            var target = FontProject.ReadLicense(directory, project, file); var bad = ProjectAssets.Prepare(directory, target.PayloadPath, [0xff, 0xfe, 0x00]); project.Edits.Assets.Add(bad);
            before = Contract.Bytes(project); var original = File.ReadAllBytes(Contract.Contained(directory, bad.AssetPath)); Reject("font_license", () => FontProject.ReadLicense(directory, project, file));
            Assert(Contract.Bytes(project).SequenceEqual(before) && File.ReadAllBytes(Contract.Contained(directory, bad.AssetPath)).SequenceEqual(original), "Non-UTF8 license was modified");
        });
    }
}
