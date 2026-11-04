// SPDX-License-Identifier: LGPL-2.1-or-later
using Folderstorm.Theme;
using Folderstorm.Theme.Editor;
using System.IO;

internal static class DrawingProjectChecks
{
    public static void Run(BaseCatalog catalog)
    {
        void Assert(bool condition, string message) { if (!condition) throw new Exception(message); }
        var evidence = Path.Combine(Environment.CurrentDirectory, "artifacts", "drawing-project-" + Guid.NewGuid().ToString("N"));
        var directory = Path.Combine(evidence, "project"); var project = ProjectStore.Create(catalog, "Pixel icon probe");
        var texture = catalog.Textures["Inv_FolderClosed"]; var sibling = catalog.Textures["Inv_SysClosed"];
        var bytes = catalog.ResolveBytes("textures/" + texture.Attributes["file_name"]); var sourceHash = Contract.Hash(bytes);
        var image = ImageCodec.Decode(bytes, texture.Attributes["file_name"]); var geometry = TextureGeometry.FromAttributes(image.Width, image.Height, texture.Attributes);
        var original = image; string? originalPath = null;
        foreach (var tool in new[] { PixelTool.Pen, PixelTool.Line, PixelTool.Circle })
        {
            var start = tool == PixelTool.Circle ? new PixelPoint(image.Width / 2, image.Height / 2) : new PixelPoint(1, 1);
            var stroke = new PixelStroke(image, tool, start, ThemeColor.Parse(tool == PixelTool.Line ? "#64D8B280" : "#EF3366FF"));
            stroke.Move(tool == PixelTool.Circle ? new(start.X + Math.Max(1, image.Width / 4), start.Y) : new(image.Width - 2, image.Height - 2));
            if (tool == PixelTool.Pen) stroke.Move(new(image.Width - 2, 1));
            Assert(stroke.HasChanges, "Drawing fixture did not change pixels");
            image = stroke.Preview; ImageProject.SetPng(directory, project, catalog, texture.Name, ImageCodec.EncodePng(image), geometry, stroke.Recipe);
            var asset = project.Edits.Assets.Single(item => item.Kind == "image");
            originalPath ??= asset.OriginalPath;
            Assert(asset.OriginalPath == originalPath && Contract.HashFile(Path.Combine(directory, asset.OriginalPath)) == sourceHash, "Drawing lost the imported TGA original");
            Assert(ImageCodec.Decode(ImageProject.Read(directory, project, catalog, texture.Name)).PixelEquals(image), "Drawing PNG changed exact pixels");
        }
        Assert(project.Undo.Count == 3 && project.Edits.Recipes[texture.Name].Parameters["steps"].GetArrayLength() == 3, "A gesture did not make exactly one edit step/checkpoint");
        Assert(ImageProject.Read(directory, project, catalog, sibling.Name).SequenceEqual(catalog.ResolveBytes("textures/" + sibling.Attributes["file_name"])), "Drawing changed the shared-file sibling");
        ProjectStore.Save(directory, project); project = ProjectStore.Open(directory);
        var theme = PackageStore.Compile(directory, project, catalog); var preview = PackageStore.CompileImagePreview(directory, project, catalog, texture.Name);
        var revisionDirectory = ThemeLibrary.Publish(theme, Path.Combine(evidence, "library"), catalog);
        var previewDirectory = ThemeLibrary.Publish(preview, Path.Combine(evidence, "preview-library"), catalog);
        for (var index = 0; index < 3; ++index) Assert(project.UndoEdit(), "Drawing Undo step missing");
        Assert(ImageCodec.Decode(ImageProject.Read(directory, project, catalog, texture.Name), texture.Attributes["file_name"]).PixelEquals(original), "Drawing Undo did not restore inherited pixels");
        for (var index = 0; index < 3; ++index) Assert(project.RedoEdit(), "Drawing Redo step missing");
        Assert(ImageCodec.Decode(ImageProject.Read(directory, project, catalog, texture.Name)).PixelEquals(image), "Drawing Redo changed final pixels");
        File.WriteAllBytes(Path.Combine(evidence, "result.json"), Contract.Bytes(new { Revision = theme.Revision, RevisionDirectory = revisionDirectory, PreviewRevision = preview.Revision, PreviewRevisionDirectory = previewDirectory, BaseRoot = catalog.Root, LogicalId = texture.Name, OriginalSha256 = sourceHash, NativeVisualAcceptance = "not tested by this probe" }));
        Console.WriteLine("PASS pen/line/circle derivatives, exact PNG pixels, original/sibling isolation, history and package compile. Receipt: " + Path.Combine(evidence, "result.json"));
    }
}
