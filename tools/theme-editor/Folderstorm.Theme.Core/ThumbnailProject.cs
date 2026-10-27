// SPDX-License-Identifier: LGPL-2.1-or-later
namespace Folderstorm.Theme;

public static class ThumbnailProject
{
    public static byte[]? Read(string directory, ThemeProject project)
    {
        var asset = project.Edits.Assets.SingleOrDefault(asset => asset.PayloadPath == "preview.png");
        return asset == null ? null : LibraryCatalog.ReadBounded(Contract.Contained(directory, asset.AssetPath), 32L * 1024 * 1024);
    }
    public static bool SetPng(string directory, ThemeProject project, BaseCatalog catalog, byte[] png)
    {
        ProjectStore.EnsureEditable(project); Contract.Require(png.Length <= 32 * 1024 * 1024, "size", "Preview exceeds32 MiB");
        PackageStore.ValidateImage(png, "preview.png");
        var current = Read(directory, project); if (current != null && current.SequenceEqual(png)) return false;
        var previous = project.Edits.Assets.SingleOrDefault(asset => asset.PayloadPath == "preview.png");
        var edits = Contract.Read<ProjectEdits>(Contract.Bytes(project.Edits)); edits.Assets.RemoveAll(asset => asset.PayloadPath == "preview.png");
        edits.Assets.Add(ProjectAssets.Prepare(directory, "preview.png", png, preservedOriginal: previous?.OriginalPath));
        PackageStore.Compile(directory, project with { Edits = edits }, catalog); project.Checkpoint("Set preview image"); project.Edits = edits; return true;
    }
    public static bool Reset(string directory, ThemeProject project, BaseCatalog catalog)
    {
        ProjectStore.EnsureEditable(project); var edits = Contract.Read<ProjectEdits>(Contract.Bytes(project.Edits));
        if (edits.Assets.RemoveAll(asset => asset.PayloadPath == "preview.png") == 0) return false;
        PackageStore.Compile(directory, project with { Edits = edits }, catalog); project.Checkpoint("Reset preview image"); project.Edits = edits; return true;
    }
}
