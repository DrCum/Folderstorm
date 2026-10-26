// SPDX-License-Identifier: LGPL-2.1-or-later
using Folderstorm.Theme;
using Folderstorm.Theme.Editor;
using System.IO;
using System.Text.Json;

internal static class ImageProjectChecks
{
    public static void Run(BaseCatalog catalog)
    {
        void Assert(bool condition, string message) { if (!condition) throw new Exception(message); }
        var evidence = Path.Combine(Environment.CurrentDirectory, "artifacts", "image-project-" + Guid.NewGuid().ToString("N")); Directory.CreateDirectory(evidence);
        var directory = Path.Combine(evidence, "project");
        var shared = catalog.Textures.Values.Where(texture => texture.Source?.EndsWith(".tga", StringComparison.Ordinal) == true)
            .GroupBy(texture => texture.Attributes.GetValueOrDefault("file_name"), StringComparer.Ordinal).FirstOrDefault(group => group.Count() > 1);
        var texture = shared?.First() ?? catalog.Textures.Values.First(item => item.Source?.EndsWith(".tga", StringComparison.Ordinal) == true);
        var sibling = shared?.Skip(1).First();
        var originalBytes = File.ReadAllBytes(Path.Combine(catalog.Root, texture.Source!)); var originalHash = Contract.Hash(originalBytes);
        var original = ImageCodec.Decode(originalBytes, texture.Source); var geometry = TextureGeometry.FromAttributes(original.Width, original.Height, texture.Attributes);
        var project = ProjectStore.Create(catalog, "Image pipeline probe"); ProjectStore.Save(directory, project);
        var tinted = original.Tint(ThemeColor.Parse("#64D8B2")); var firstPng = ImageCodec.EncodePng(tinted);
        ImageProject.SetPng(directory, project, catalog, texture.Name, firstPng, geometry, new("tint", new() { ["color"] = JsonSerializer.SerializeToElement("#64D8B2") }));
        var resolved = ImageProject.Resolve(directory, project, catalog, texture.Name);
        var imagePayload = "viewer/textures/" + resolved.Attributes["file_name"];
        var asset = project.Edits.Assets.Single(item => item.PayloadPath == imagePayload);
        Assert(asset.OriginalPath.EndsWith(".tga", StringComparison.Ordinal) && Contract.HashFile(Path.Combine(directory, asset.OriginalPath)) == originalHash, "Derivative lost its imported TGA original");
        Assert(ImageProject.Read(directory, project, catalog, texture.Name).SequenceEqual(firstPng), "Scoped derivative is not the current image");
        if (sibling != null) Assert(ImageProject.Resolve(directory, project, catalog, sibling.Name).Attributes["file_name"] == sibling.Attributes["file_name"], "Editing one logical ID changed its shared-file sibling");
        var width = original.Width * 2; var height = original.Height * 2; var resized = tinted.Resize(width, height); var resizedGeometry = geometry.Resize(width, height);
        var resizedPng = ImageCodec.EncodePng(resized);
        ImageProject.SetPng(directory, project, catalog, texture.Name, resizedPng, resizedGeometry, new("resize", new()
        { ["width"] = JsonSerializer.SerializeToElement(width), ["height"] = JsonSerializer.SerializeToElement(height), ["interpolation"] = JsonSerializer.SerializeToElement("bilinear") }));
        Assert(project.Edits.Recipes[texture.Name].Operation == "pipeline" && project.Edits.Recipes[texture.Name].Parameters["steps"].GetArrayLength() == 2, "Recipe pipeline did not retain both edits");
        Assert(project.Edits.Assets.Single(item => item.PayloadPath == imagePayload).OriginalPath == asset.OriginalPath, "Second edit replaced the original reference");
        Assert(project.UndoEdit() && ImageProject.Read(directory, project, catalog, texture.Name).SequenceEqual(firstPng), "Image undo lost its earlier derivative");
        Assert(project.RedoEdit() && ImageProject.Read(directory, project, catalog, texture.Name).SequenceEqual(resizedPng), "Image redo lost the resized derivative");
        var before = Contract.Bytes(project);
        try { ImageProject.SetPng(directory, project, catalog, texture.Name, firstPng, resizedGeometry, new("tint", new())); throw new Exception("Mismatched PNG guides accepted"); }
        catch (ThemeException error) { Assert(error.Code == "image" && Contract.Bytes(project).SequenceEqual(before), "Rejected edit changed the project"); }
        ProjectStore.Save(directory, project); project = ProjectStore.Open(directory);
        var theme = PackageStore.Compile(directory, project, catalog); var package = Path.Combine(evidence, "image-pipeline.fstheme"); PackageStore.Export(package, theme);
        var revisionDirectory = ThemeLibrary.Publish(theme, Path.Combine(evidence, "library"), catalog);
        Assert(!theme.Files.Keys.Any(path => path.EndsWith(".tga", StringComparison.Ordinal)) && theme.Files[imagePayload].SequenceEqual(resizedPng), "Runtime export contains originals or wrong current bytes");
        Assert(Contract.HashFile(Path.Combine(catalog.Root, texture.Source!)) == originalHash, "Editor changed the inherited base image");
        File.WriteAllBytes(Path.Combine(evidence, "result.json"), Contract.Bytes(new
        {
            ThemeRevision = theme.Revision, RevisionDirectory = revisionDirectory, Package = package, BaseRoot = catalog.Root,
            LogicalId = texture.Name, Sibling = sibling?.Name, OriginalSha256 = originalHash, OriginalPath = asset.OriginalPath,
            ImagePayload = imagePayload, CoreAcceptanceOnly = true, NativeVisualAcceptance = "not tested by this probe"
        }));
        Console.WriteLine("PASS scoped image derivatives, TGA originals, recipe/history, sibling isolation and runtime export. Receipt: " + Path.Combine(evidence, "result.json"));
    }
}
