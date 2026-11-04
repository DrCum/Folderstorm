// SPDX-License-Identifier: LGPL-2.1-or-later
using Folderstorm.Theme;
using System.Buffers.Binary;
using System.Text;
using System.Text.Json;

internal static class ProjectChangesChecks
{
    public static void Register(Action<string, Action> test, string root, BaseCatalog catalog)
    {
        void Assert(bool value, string message) { if (!value) throw new Exception(message); }
        void Reject(string code, Action action) { try { action(); throw new Exception("Expected " + code); } catch (ThemeException error) { Assert(error.Code == code, error.Message); } }
        var directory = Path.Combine(root, "project-changes"); var p = ProjectStore.Create(catalog, "Change review");
        var texture = catalog.Textures["Rounded_Rect_Top"]; var png = catalog.ResolveBytes("textures/" + texture.Attributes["file_name"]);
        var width = (int)BinaryPrimitives.ReadUInt32BigEndian(png.AsSpan(16, 4)); var height = (int)BinaryPrimitives.ReadUInt32BigEndian(png.AsSpan(20, 4));
        var geometry = TextureGeometry.FromAttributes(width, height, texture.Attributes);
        test("changes review lists mixed overrides without writes and shows normalized base/current XML", () =>
        {
            p.Edits.Colors["ThemeAccent"] = ThemeColor.Parse("#64D8B2").NativeText;
            ImageProject.SetPng(directory, p, catalog, texture.Name, png, geometry, new("tint", new() { ["color"] = JsonSerializer.SerializeToElement("#fff") }));
            XuiProject.Apply(directory, p, catalog, "xui/en/widgets/button.xml", [new(0, "label_color", "ThemeAccent")], "Label color");
            FontProject.Apply(directory, p, catalog, FontProject.SetSize(FontProject.Read(directory, p, catalog), "Small", 13.5), "Font size");
            p.Edits.Assets.Add(ProjectAssets.Prepare(directory, "preview.png", png));
            p.Edits.Assets.Add(ProjectAssets.Prepare(directory, "licenses/artwork.txt", Encoding.UTF8.GetBytes("Keep these original bytes")));
            var before = Contract.Bytes(p); var files = Directory.EnumerateFiles(directory, "*", SearchOption.AllDirectories).ToDictionary(path => path, Contract.HashFile);
            var review = ProjectChanges.Inspect(directory, p, catalog);
            Assert(review.Entries.Select(entry => entry.Group).Distinct().Count() == 6 && review.Entries.Any(entry => entry.Id == "color:ThemeAccent" && entry.After.Contains("#64D8B2", StringComparison.Ordinal)), "Review missed override groups or color values");
            Assert(review.Entries.Single(entry => entry.Id == "file:viewer/xui/en/widgets/button.xml").After.Contains("label_color=\"ThemeAccent\"", StringComparison.Ordinal), "Review missed current XUI property");
            Assert(review.Entries.Single(entry => entry.Id == "file:viewer/xui/en/widgets/button.xml").After.StartsWith("Changed properties", StringComparison.Ordinal) && review.Entries.Single(entry => entry.Id == "file:viewer/xui/en/widgets/button.xml").After.Contains("label_color = ThemeAccent", StringComparison.Ordinal), "Review hid a changed property in a long XML line");
            Assert(review.Entries.Single(entry => entry.Id == "fonts:all").After.Contains("13.5", StringComparison.Ordinal) && review.Entries.Single(entry => entry.Id == "fonts:all").After.Contains("SHA256", StringComparison.Ordinal) && review.Entries.Any(entry => entry.Id == "image:" + texture.Name), "Review missed image/font content");
            Assert(Contract.Bytes(p).SequenceEqual(before) && files.Count == Directory.EnumerateFiles(directory, "*", SearchOption.AllDirectories).Count() && files.All(file => Contract.HashFile(file.Key) == file.Value), "Review changed project or files");
        });
        test("changes selected image and layout resets retain other resources and make one Undo step", () =>
        {
            var before = Contract.Bytes(p.Edits); var count = p.Undo.Count;
            var review = ProjectChanges.Inspect(directory, p, catalog);
            Assert(ProjectChanges.Reset(directory, p, catalog, new(review.EditsHash, "image:" + texture.Name)) && p.Undo.Count == count + 1, "Image reset did not commit once");
            Assert(!p.Edits.Recipes.ContainsKey(texture.Name) && p.Edits.Colors.Count == 1 && p.Edits.Assets.Any(asset => asset.PayloadPath == FontProject.ConfigPath), "Image reset touched unrelated resources");
            Assert(p.UndoEdit() && Contract.Bytes(p.Edits).SequenceEqual(before), "Image reset Undo lost authoring state");
            review = ProjectChanges.Inspect(directory, p, catalog); count = p.Undo.Count;
            Assert(ProjectChanges.Reset(directory, p, catalog, new(review.EditsHash, "file:viewer/xui/en/widgets/button.xml")) && p.Undo.Count == count + 1, "Layout reset did not commit once");
            Assert(p.Edits.Assets.All(asset => asset.PayloadPath != "viewer/xui/en/widgets/button.xml") && p.UndoEdit() && Contract.Bytes(p.Edits).SequenceEqual(before), "Layout reset lost unrelated state or Undo");
            review = ProjectChanges.Inspect(directory, p, catalog);
            Assert(ProjectChanges.Reset(directory, p, catalog, new(review.EditsHash, "file:viewer/textures/textures.xml")) && p.Edits.Recipes.Count == 0 && p.Edits.Assets.All(asset => !asset.PayloadPath.StartsWith("viewer/textures/", StringComparison.Ordinal)), "Registration-table reset left stale image recipes or payloads");
            Assert(p.UndoEdit() && Contract.Bytes(p.Edits).SequenceEqual(before), "Registration-table Undo lost image state");
        });
        test("changes imported palette resets preserve independent compiled aliases and manifest provenance", () =>
        {
            var target = Path.Combine(root, "review-imported"); var theme = PackageStore.Compile(directory, p, catalog); var imported = PackageStore.ImportProject(target, theme, catalog);
            var before = Contract.Bytes(imported.Edits); var original = imported.Edits.CompiledManifestPath;
            var review = ProjectChanges.Inspect(target, imported, catalog);
            Assert(review.Entries.Any(entry => entry.Id == "color:ThemeAccent" && entry.Source == "Imported override"), "Imported provenance missing");
            Assert(ProjectChanges.Reset(target, imported, catalog, new(review.EditsHash, "color:ThemeAccent")) && imported.Undo.Count == 1, "Raw reset failed");
            var palette = PaletteProject.Read(target, imported, catalog);
            Assert(!palette.Imported.ContainsKey("ThemeAccent") && palette.Inspect("AccentColor").Value.Hex(false) == "#64D8B2" && imported.Edits.CompiledManifestPath == original, "Reset inferred missing links or lost origin");
            Assert(imported.UndoEdit() && Contract.Bytes(imported.Edits).SequenceEqual(before), "Imported reset Undo lost exact source");
        });
        test("changes group and project resets preserve details and original files through durable Undo and Redo", () =>
        {
            var before = Contract.Bytes(p.Edits); var files = Directory.EnumerateFiles(directory, "*", SearchOption.AllDirectories).ToDictionary(path => path, Contract.HashFile);
            var review = ProjectChanges.Inspect(directory, p, catalog); var count = p.Undo.Count;
            Assert(ProjectChanges.Reset(directory, p, catalog, new(review.EditsHash, Group: ChangeGroup.Fonts)) && p.Undo.Count == count + 1 && p.Edits.Assets.All(asset => asset.PayloadPath != FontProject.ConfigPath), "Font group reset failed");
            Assert(p.Edits.Colors.Count == 1 && p.UndoEdit() && Contract.Bytes(p.Edits).SequenceEqual(before), "Font group reset lost unrelated edits");
            review = ProjectChanges.Inspect(directory, p, catalog); count = p.Undo.Count;
            Assert(ProjectChanges.Reset(directory, p, catalog, new(review.EditsHash)) && p.Undo.Count == count + 1 && ProjectChanges.Inspect(directory, p, catalog).Entries.Length == 0, "Project reset left active overrides");
            Assert(p.DisplayName == "Change review" && files.All(file => File.Exists(file.Key) && Contract.HashFile(file.Key) == file.Value), "Reset changed details or originals");
            ProjectStore.Save(directory, p); var saved = ProjectStore.Open(directory);
            Assert(saved.UndoEdit() && Contract.Bytes(saved.Edits).SequenceEqual(before) && saved.RedoEdit() && saved.Edits.Assets.Count == 0, "Durable Undo/Redo lost reset state");
            review = ProjectChanges.Inspect(directory, saved, catalog); count = saved.Undo.Count;
            Assert(!ProjectChanges.Reset(directory, saved, catalog, new(review.EditsHash)) && saved.Undo.Count == count, "No-op reset added history");
            Assert(p.UndoEdit(), "Failed to restore fixture");
        });
        test("changes stale content, invalid selections and failed compilation leave document/history unchanged", () =>
        {
            var review = ProjectChanges.Inspect(directory, p, catalog); var before = Contract.Bytes(p);
            Reject("changes_selection", () => ProjectChanges.Reset(directory, p, catalog, new(review.EditsHash, "file:missing")));
            Reject("changes_selection", () => ProjectChanges.Reset(directory, p, catalog, new(review.EditsHash, Group: (ChangeGroup)99)));
            Reject("changes_selection", () => ProjectChanges.Reset(directory, p, catalog, new(review.EditsHash, "color:ThemeAccent", ChangeGroup.Images)));
            Assert(Contract.Bytes(p).SequenceEqual(before), "Invalid reset changed document");
            p.PackageVersion = "invalid"; before = Contract.Bytes(p);
            Reject("identity", () => ProjectChanges.Reset(directory, p, catalog, new(review.EditsHash)));
            Assert(Contract.Bytes(p).SequenceEqual(before), "Failed full compilation changed state"); p.PackageVersion = "1.0.0";
            var license = p.Edits.Assets.Single(asset => asset.PayloadPath == "licenses/artwork.txt"); var path = Contract.Contained(directory, license.AssetPath); var bytes = File.ReadAllBytes(path);
            File.AppendAllText(path, " externally changed"); before = Contract.Bytes(p);
            Reject("changes_stale", () => ProjectChanges.Reset(directory, p, catalog, new(review.EditsHash)));
            Assert(Contract.Bytes(p).SequenceEqual(before), "Stale content reset changed document"); File.WriteAllBytes(path, bytes);
            p.Edits.Colors["ThemeBg"] = "0 0 0 1"; before = Contract.Bytes(p);
            Reject("changes_stale", () => ProjectChanges.Reset(directory, p, catalog, new(review.EditsHash)));
            Assert(Contract.Bytes(p).SequenceEqual(before), "Stale edits reset changed document");
        });
    }
}
