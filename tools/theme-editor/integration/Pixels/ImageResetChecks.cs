// SPDX-License-Identifier: LGPL-2.1-or-later
using Folderstorm.Theme;
using Folderstorm.Theme.Editor;
using System.IO;

internal static class ImageResetChecks
{
    public static void Run(BaseCatalog catalog)
    {
        void Assert(bool condition, string message) { if (!condition) throw new Exception(message); }
        var evidence = Path.Combine(Environment.CurrentDirectory, "artifacts", "image-reset-" + Guid.NewGuid().ToString("N"));
        var directory = Path.Combine(evidence, "project");
        var selected = catalog.Textures["Rounded_Rect_Top"]; var sibling = catalog.Textures["Rounded_Rect_Bottom"];
        var file = selected.Attributes["file_name"];
        Assert(file == sibling.Attributes["file_name"] && file.EndsWith(".png", StringComparison.Ordinal), "Shared PNG fixture changed");
        var original = catalog.ResolveBytes("textures/" + file); var source = ImageCodec.Decode(original, file);
        var replacement = ImageCodec.EncodePng(RasterImage.Fill(source.Width, source.Height, ThemeColor.Parse("#64D8B280")));
        var p = ProjectStore.Create(catalog, "Shared-file reset probe");
        p.Edits.Colors["ThemeAccent"] = "0.2 0.8 0.5 1";
        p.Edits.Assets.Add(ProjectAssets.Prepare(directory, "viewer/textures/" + file, replacement, original, ".png"));
        var edited = Contract.Bytes(p.Edits); var history = p.Undo.Count;
        Assert(ImageProject.Reset(directory, p, catalog, [selected.Name]) && p.Undo.Count == history + 1, "Shared reset lost its checkpoint");
        Assert(ImageCodec.Decode(ImageProject.Read(directory, p, catalog, selected.Name)).ToRgba().SequenceEqual(source.ToRgba()), "Selected reset did not restore inherited pixels");
        Assert(ImageProject.Read(directory, p, catalog, sibling.Name).SequenceEqual(replacement), "Selected reset changed its shared-file sibling");
        var reset = Contract.Bytes(p.Edits);
        Assert(!ImageProject.Reset(directory, p, catalog, [selected.Name]) && Contract.Bytes(p.Edits).SequenceEqual(reset), "Repeated shared reset changed the project");
        Assert(p.UndoEdit() && Contract.Bytes(p.Edits).SequenceEqual(edited) && p.RedoEdit(), "Shared reset undo/redo failed");
        ProjectStore.Save(directory, p); p = ProjectStore.Open(directory);
        var theme = PackageStore.Compile(directory, p, catalog); var revision = ThemeLibrary.Publish(theme, Path.Combine(evidence, "library"), catalog);
        Assert(ImageProject.ResetAll(p, catalog) && p.Edits.Colors.Count == 1 && !p.Edits.Assets.Any(asset => asset.PayloadPath.StartsWith("viewer/textures/", StringComparison.Ordinal)), "Reset all did not preserve unrelated colors");
        Assert(ImageProject.Read(directory, p, catalog, selected.Name).SequenceEqual(original) && ImageProject.Read(directory, p, catalog, sibling.Name).SequenceEqual(original), "Reset all failed to restore shared inheritance");
        Assert(p.UndoEdit() && Contract.Bytes(p.Edits).SequenceEqual(reset), "Reset all undo did not restore both image layers");
        Assert(Contract.Hash(catalog.ResolveBytes("textures/" + file)) == Contract.Hash(original), "Reset changed base artwork");
        File.WriteAllBytes(Path.Combine(evidence, "result.json"), Contract.Bytes(new { Revision = theme.Revision, RevisionDirectory = revision, BaseRoot = catalog.Root, Selected = selected.Name, Sibling = sibling.Name, NativeVisualAcceptance = "not tested by this probe" }));
        Console.WriteLine("PASS shared-file reset isolation, inherited pixels, no-op, history and save/reopen. Receipt: " + Path.Combine(evidence, "result.json"));
    }
}
