// SPDX-License-Identifier: LGPL-2.1-or-later
using Folderstorm.Theme;
using Folderstorm.Theme.Editor;
using System.IO;

internal static class ImageFamilyChecks
{
    public static void Run(BaseCatalog catalog)
    {
        void Assert(bool condition, string message) { if (!condition) throw new Exception(message); }
        var evidence = Path.Combine(Environment.CurrentDirectory, "artifacts", "image-family-" + Guid.NewGuid().ToString("N"));
        var directory = Path.Combine(evidence, "project"); var project = ProjectStore.Create(catalog, "Image states"); ProjectStore.Save(directory, project);
        var entries = ImageFamilyEdit.Load(directory, project, catalog, "PushButton_Off");
        var selection = entries.Where(item => item.Name is "PushButton_Off" or "PushButton_On").ToArray();
        Assert(selection.Length == 2 && selection.All(item => item.CanEdit), "Expected editable button state fixtures");
        var before = Contract.Bytes(project); var original = selection.ToDictionary(item => item.Name, item => ImageProject.Read(directory, project, catalog, item.Name));
        var untouched = ImageProject.Read(directory, project, catalog, "PushButton_Disabled");
        var color = ThemeColor.Parse("#64D8B280"); var results = ImageFamilyEdit.Preview(selection, new(FamilyOperation.Tint, color));
        Assert(results.All(item => item.Changed && item.Image.PixelEquals(item.Source.Image!.Tint(color))) && Contract.Bytes(project).SequenceEqual(before), "Preview changed project or individual state pixels");
        Assert(ImageProject.SetPngBatch(directory, project, catalog, results.Select(item => item.Encode()), "Tint selected states") == 2 && project.Undo.Count == 1, "State tint did not commit once");
        foreach (var entry in selection)
        {
            var texture = ImageProject.Resolve(directory, project, catalog, entry.Name); var asset = project.Edits.Assets.Single(item => item.PayloadPath == "viewer/textures/" + texture.Attributes["file_name"]);
            Assert(File.ReadAllBytes(Path.Combine(directory, asset.OriginalPath)).SequenceEqual(original[entry.Name]), "Family tint lost original state artwork");
        }
        Assert(ImageProject.Read(directory, project, catalog, "PushButton_Disabled").SequenceEqual(untouched), "Unchecked state changed");
        entries = ImageFamilyEdit.Load(directory, project, catalog, "PushButton_Off"); selection = entries.Where(item => original.ContainsKey(item.Name)).ToArray();
        var unchanged = ImageFamilyEdit.Preview(selection, new(FamilyOperation.Adjust, color));
        Assert(unchanged.All(item => !item.Changed), "Neutral adjustments changed pixels");
        var adjusted = ImageFamilyEdit.Preview(selection, new(FamilyOperation.Adjust, color, Brightness: .1, Saturation: .5, Alpha: .5));
        Assert(adjusted.All(item => item.Changed && item.Image.PixelEquals(item.Source.Image!.Adjust(.1, 0, .5, 1, .5))), "State adjustment differs from per-image operation");
        var resized = ImageFamilyEdit.Preview(selection, new(FamilyOperation.Resize, color, Width: 64, Height: 48));
        Assert(resized.All(item => item.Image.Width == 64 && item.Image.Height == 48 && item.Geometry.Width == 64), "State resize geometry differs");
        ImageProject.SetPngBatch(directory, project, catalog, resized.Select(item => item.Encode()), "Resize selected states");
        Assert(project.Undo.Count == 2 && project.Edits.Recipes.Values.All(recipe => recipe.Operation == "pipeline" && recipe.Parameters["steps"].GetArrayLength() == 2), "Batch lost prior recipe steps");
        var guides = selection[0].Geometry!.Value; guides = guides with { Margins = guides.Margins with { Left = guides.Margins.Left + 1 } }; guides.Validate();
        var guideResult = ImageFamilyEdit.Preview(selection, new(FamilyOperation.Guides, color, Guides: guides));
        Assert(guideResult.All(item => item.Image.PixelEquals(item.Source.Image!) && item.Geometry == guides.Resize(item.Image.Width, item.Image.Height) && item.Recipe.Operation == "nine-slice"), "Guide sharing changed pixels or copied wrong origins");
        var expected = resized.ToDictionary(item => item.Source.Name, item => item.Image);
        ProjectStore.Save(directory, project); project = ProjectStore.Open(directory);
        var theme = PackageStore.Compile(directory, project, catalog); var preview = PackageStore.CompileImagePreview(directory, project, catalog, selection[0].Name);
        var revision = ThemeLibrary.Publish(theme, Path.Combine(evidence, "library"), catalog); var previewDirectory = ThemeLibrary.Publish(preview, Path.Combine(evidence, "preview-library"), catalog);
        Assert(project.UndoEdit() && ImageCodec.Decode(ImageProject.Read(directory, project, catalog, selection[0].Name)).PixelEquals(results[0].Image), "Family Undo did not restore previous state size/pixels");
        Assert(project.RedoEdit() && expected.All(item => ImageCodec.Decode(ImageProject.Read(directory, project, catalog, item.Key)).PixelEquals(item.Value)), "Family Redo lost a state");
        var document = Contract.Bytes(project);
        try { ImageFamilyEdit.Preview(selection, new(FamilyOperation.Resize, color, Width: 16384, Height: 16384)); throw new Exception("Oversized family accepted"); }
        catch (ThemeException) { }
        Assert(Contract.Bytes(project).SequenceEqual(document), "Failed preview changed history");
        File.WriteAllBytes(Path.Combine(evidence, "result.json"), Contract.Bytes(new { Revision = theme.Revision, RevisionDirectory = revision, PreviewRevision = preview.Revision,
            PreviewRevisionDirectory = previewDirectory, BaseRoot = catalog.Root, States = selection.Select(item => item.Name).ToArray(), FamilyMembers = entries.Length,
            NativeVisualAcceptance = "not tested by this probe" }));
        Console.WriteLine("PASS checked-state tint/adjust/resize/guides, preview isolation, one checkpoint, originals/unchecked states, save/reopen and package compile. Receipt: " + Path.Combine(evidence, "result.json"));
    }
}
