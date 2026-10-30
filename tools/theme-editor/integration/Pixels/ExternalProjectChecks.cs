// SPDX-License-Identifier: LGPL-2.1-or-later
using Folderstorm.Theme;
using Folderstorm.Theme.Editor;
using System.IO;

internal static class ExternalProjectChecks
{
    public static void Run(BaseCatalog catalog)
    {
        void Assert(bool condition, string message) { if (!condition) throw new Exception(message); }
        var evidence = Path.Combine(Environment.CurrentDirectory, "artifacts", "external-project-" + Guid.NewGuid().ToString("N"));
        var directory = Path.Combine(evidence, "project"); var working = Path.Combine(evidence, "working");
        var project = ProjectStore.Create(catalog, "External TGA probe"); ProjectStore.Save(directory, project);
        var texture = catalog.Textures["Inv_FolderClosed"]; var sibling = catalog.Textures["Inv_SysClosed"];
        var sourceBytes = catalog.ResolveBytes("textures/" + texture.Attributes["file_name"]); var source = ImageCodec.Decode(sourceBytes, texture.Attributes["file_name"]);
        var session = ExternalImageSession.Create(directory, project, catalog, texture.Name, working, (bytes, file) => ImageCodec.EncodePng(ImageCodec.Decode(bytes, file)));
        Assert(ImageCodec.Decode(File.ReadAllBytes(session.WorkingPath)).PixelEquals(source) && project.Edits.Assets.Count == 0 && project.Undo.Count == 0, "External PNG conversion changed pixels or project");
        var edited = source.Resize(source.Width * 2, source.Height * 2).Border(1, ThemeColor.Parse("#64D8B280"));
        var png = ImageCodec.EncodePng(edited); Contract.AtomicWrite(session.WorkingPath, png);
        Assert(session.Reload(directory, project, catalog, bytes => Assert(ImageCodec.Decode(bytes).PixelEquals(edited), "External PNG validation changed pixels")), "External reload was ignored");
        var asset = project.Edits.Assets.Single(item => item.Kind == "image");
        Assert(asset.OriginalPath.EndsWith(".tga", StringComparison.Ordinal) && File.ReadAllBytes(Path.Combine(directory, asset.OriginalPath)).SequenceEqual(sourceBytes), "External reload discarded the TGA original");
        Assert(ImageProject.Read(directory, project, catalog, texture.Name).SequenceEqual(png) && project.Undo.Count == 1, "Reload transcoded imported bytes or added extra checkpoints");
        Assert(ImageProject.Read(directory, project, catalog, sibling.Name).SequenceEqual(catalog.ResolveBytes("textures/" + sibling.Attributes["file_name"])), "External edit changed its shared-file sibling");
        var geometry = TextureGeometry.FromAttributes(edited.Width, edited.Height, ImageProject.Resolve(directory, project, catalog, texture.Name).Attributes);
        Assert(geometry.Width == edited.Width && geometry.Height == edited.Height, "External resize did not update native geometry");
        ProjectStore.Save(directory, project); project = ProjectStore.Open(directory);
        Assert(ExternalImageSession.FindPrevious(directory, project, texture.Name, working)?.IsCurrent(directory, project, catalog) == true, "External copy was not recoverable after reopen");
        var theme = PackageStore.Compile(directory, project, catalog); var preview = PackageStore.CompileImagePreview(directory, project, catalog, texture.Name);
        Assert(!theme.Files.Keys.Any(path => path.Contains("session", StringComparison.Ordinal) || path.Contains("working", StringComparison.Ordinal)), "Runtime package contains local working state");
        var revisionDirectory = ThemeLibrary.Publish(theme, Path.Combine(evidence, "library"), catalog);
        var previewDirectory = ThemeLibrary.Publish(preview, Path.Combine(evidence, "preview-library"), catalog);
        Assert(project.UndoEdit() && ImageCodec.Decode(ImageProject.Read(directory, project, catalog, texture.Name), texture.Attributes["file_name"]).PixelEquals(source), "External Undo lost source pixels");
        Assert(project.RedoEdit() && ImageCodec.Decode(ImageProject.Read(directory, project, catalog, texture.Name)).PixelEquals(edited), "External Redo changed edited pixels");
        File.WriteAllBytes(Path.Combine(evidence, "result.json"), Contract.Bytes(new { Revision = theme.Revision, RevisionDirectory = revisionDirectory, PreviewRevision = preview.Revision, PreviewRevisionDirectory = previewDirectory, BaseRoot = catalog.Root, LogicalId = texture.Name, OriginalSha256 = Contract.Hash(sourceBytes), ExternalBytesSha256 = Contract.Hash(png), NativeVisualAcceptance = "not tested by this probe" }));
        Console.WriteLine("PASS external TGA/PNG round trip, exact imported bytes, resized geometry, original/sibling preservation, recovery/history and package compile. Receipt: " + Path.Combine(evidence, "result.json"));
    }
}
