// SPDX-License-Identifier: LGPL-2.1-or-later
using Folderstorm.Theme;
using Folderstorm.Theme.Editor;
using System.IO;
using System.Windows.Media.Imaging;

internal static class ImageImportChecks
{
    public static void Run(BaseCatalog catalog)
    {
        void Assert(bool condition, string message) { if (!condition) throw new Exception(message); }
        var evidence = Path.Combine(Environment.CurrentDirectory, "artifacts", "image-import-" + Guid.NewGuid().ToString("N"));
        var directory = Path.Combine(evidence, "project"); var project = ProjectStore.Create(catalog, "Imported images"); ProjectStore.Save(directory, project);
        var source = RasterImage.FromRgba(3, 2, [255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 64, 17, 31, 47, 0, 70, 80, 90, 255, 1, 2, 3, 1]);
        byte[] Encode(BitmapEncoder encoder) { encoder.Frames.Add(BitmapFrame.Create(ImageCodec.ToBitmap(source))); using var output = new MemoryStream(); encoder.Save(output); return output.ToArray(); }
        var tga = catalog.ResolveBytes("textures/" + catalog.Textures["Inv_FolderClosed"].Attributes["file_name"]);
        var fixtures = new[] { (".png", ImageCodec.EncodePng(source)), (".tga", tga), (".bmp", Encode(new BmpBitmapEncoder())), (".jpg", Encode(new JpegBitmapEncoder())), (".jpeg", Encode(new JpegBitmapEncoder())) };
        var untouched = ImageProject.Read(directory, project, catalog, "PushButton_On");
        foreach (var (extension, bytes) in fixtures)
        {
            var original = (byte[])bytes.Clone(); var imported = ImageImport.Prepare(bytes, "source" + extension);
            Assert(imported.Extension == extension && imported.Original.SequenceEqual(original) && bytes.SequenceEqual(original), "Import changed original bytes or extension");
            Assert(ImageCodec.Decode(imported.Png).PixelEquals(ImageCodec.Decode(original, "source" + extension)), "Prepared PNG lost decoded pixels/alpha");
            if (extension == ".png") Assert(imported.Png.SequenceEqual(bytes), "PNG import transcoded source bytes");
            var texture = ImageProject.Resolve(directory, project, catalog, "PushButton_Off");
            var geometry = new TextureGeometry(imported.Image.Width, imported.Image.Height, new(0, imported.Image.Height, imported.Image.Width, 0), new(0, 0, 0, 0));
            ImageProject.SetPng(directory, project, catalog, texture.Name, imported.Png, geometry, new("replace", new()), imported.Original, imported.Extension);
            var asset = project.Edits.Assets.Single(item => item.Kind == "image");
            Assert(asset.OriginalPath.EndsWith(extension, StringComparison.Ordinal) && File.ReadAllBytes(Path.Combine(directory, asset.OriginalPath)).SequenceEqual(bytes), "Imported original did not survive project storage");
        }
        Assert(project.Undo.Count == fixtures.Length && ImageProject.Read(directory, project, catalog, "PushButton_On").SequenceEqual(untouched), "Import history or sibling isolation failed");
        var before = Contract.Bytes(project);
        try { ImageImport.Prepare(fixtures[0].Item2, "wrong.jpg"); throw new Exception("Misnamed image accepted"); } catch (ThemeException) { }
        try { ImageImport.Prepare(fixtures[0].Item2, "unsupported.gif"); throw new Exception("Unsupported image accepted"); } catch (ThemeException) { }
        Assert(Contract.Bytes(project).SequenceEqual(before), "Rejected import changed project");
        ProjectStore.Save(directory, project); project = ProjectStore.Open(directory);
        var importedTheme = PackageStore.Compile(directory, project, catalog); var importedPreview = PackageStore.CompileImagePreview(directory, project, catalog, "PushButton_Off");
        var importedDirectory = ThemeLibrary.Publish(importedTheme, Path.Combine(evidence, "library"), catalog); var importedPreviewDirectory = ThemeLibrary.Publish(importedPreview, Path.Combine(evidence, "preview-library"), catalog);
        Assert(project.UndoEdit() && project.RedoEdit() && Contract.Bytes(project).SequenceEqual(before), "Import history failed after save/reopen");
        Console.WriteLine("PASS PNG/TGA/BMP/JPEG import keeps original bytes/extension, exact decoded PNG pixels, sibling/history isolation and package compile");

        (TextureDefinition Texture, RasterImage Image, byte[] Bytes)? invalid = null;
        foreach (var candidate in catalog.Textures.Values.OrderBy(item => item.Name, StringComparer.Ordinal))
        {
            if (!candidate.Attributes.Keys.Any(key => key.StartsWith("scale.", StringComparison.Ordinal) || key.StartsWith("clip.", StringComparison.Ordinal)) || !candidate.Attributes.TryGetValue("file_name", out var file)) continue;
            try
            {
                var bytes = catalog.ResolveBytes("textures/" + file); var image = ImageCodec.Decode(bytes, file);
                try { _ = TextureGeometry.FromAttributes(image.Width, image.Height, candidate.Attributes); }
                catch (ThemeException) { invalid = (candidate, image, bytes); break; }
            }
            catch (Exception error) when (error is IOException or ThemeException or FormatException or NotSupportedException) { }
        }
        Assert(invalid != null, "Expected an inherited legacy guide fixture");
        var legacy = invalid!.Value; var repairDirectory = Path.Combine(evidence, "guide-project"); var repairProject = ProjectStore.Create(catalog, "Explicit guide repair"); ProjectStore.Save(repairDirectory, repairProject);
        var repaired = new TextureGeometry(legacy.Image.Width, legacy.Image.Height, new(0, legacy.Image.Height, legacy.Image.Width, 0), new(0, 0, 0, 0));
        ImageProject.SetGeometry(repairDirectory, repairProject, catalog, legacy.Texture.Name, repaired);
        Assert(repairProject.Undo.Count == 1 && ImageProject.Read(repairDirectory, repairProject, catalog, legacy.Texture.Name).SequenceEqual(legacy.Bytes), "Guide reset changed artwork or history");
        Assert(TextureGeometry.FromAttributes(repaired.Width, repaired.Height, ImageProject.Resolve(repairDirectory, repairProject, catalog, legacy.Texture.Name).Attributes) == repaired, "Repaired guide bounds differ");
        ProjectStore.Save(repairDirectory, repairProject); repairProject = ProjectStore.Open(repairDirectory);
        var repairTheme = PackageStore.Compile(repairDirectory, repairProject, catalog); var repairPreview = PackageStore.CompileImagePreview(repairDirectory, repairProject, catalog, legacy.Texture.Name);
        var repairRevision = ThemeLibrary.Publish(repairTheme, Path.Combine(evidence, "guide-library"), catalog); var repairPreviewDirectory = ThemeLibrary.Publish(repairPreview, Path.Combine(evidence, "guide-preview-library"), catalog);
        Assert(repairProject.UndoEdit() && ImageProject.Resolve(repairDirectory, repairProject, catalog, legacy.Texture.Name).Attributes.SequenceEqual(legacy.Texture.Attributes), "Guide Undo did not restore original attributes");
        Assert(repairProject.RedoEdit() && ImageProject.Read(repairDirectory, repairProject, catalog, legacy.Texture.Name).SequenceEqual(legacy.Bytes), "Guide Redo changed original artwork");
        File.WriteAllBytes(Path.Combine(evidence, "result.json"), Contract.Bytes(new { BaseRoot = catalog.Root, ImportRevision = importedTheme.Revision, ImportDirectory = importedDirectory,
            ImportPreviewRevision = importedPreview.Revision, ImportPreviewDirectory = importedPreviewDirectory, RepairImage = legacy.Texture.Name, RepairRevision = repairTheme.Revision,
            RepairDirectory = repairRevision, RepairPreviewRevision = repairPreview.Revision, RepairPreviewDirectory = repairPreviewDirectory, NativeVisualAcceptance = "not tested by this probe" }));
        Console.WriteLine("PASS explicit legacy guide repair keeps source bytes, restores inherited attributes through Undo, saves/reopens and compiles. Receipt: " + Path.Combine(evidence, "result.json"));
    }
}
