// SPDX-License-Identifier: LGPL-2.1-or-later
namespace Folderstorm.Theme;

public static class HostProject
{
    public const string PayloadPath = "host/palette.json";
    public static HostPaletteDefinition FromViewer(string directory, ThemeProject? project, BaseCatalog catalog)
    {
        var palette = PaletteProject.Read(directory, project, catalog); var defaults = HostPalette.Midnight;
        ThemeColor Color(string role, string preferred, string fallback)
        {
            try { return palette.Inspect(catalog.Colors.ContainsKey(preferred) ? preferred : fallback).Value; }
            catch (ThemeException) { return ThemeColor.Parse(defaults.Colors[role]); }
        }
        var background = Color("background", "ThemeBg", "MenuBarBgColor").Over(ThemeColor.Parse(defaults.Colors["background"]));
        var colors = new Dictionary<string, string>(StringComparer.Ordinal) { ["background"] = background.Hex(false) };
        foreach (var (role, preferred, fallback) in new[] { ("panel", "ThemePanel", "SL-Background"), ("input", "ThemeInput", "TextBgWriteableColor"), ("text", "ThemeText", "TextFgColor"), ("muted", "ThemeMuted", "LabelDisabledColor"), ("accent", "ThemeAccent", "AccentColor"), ("border", "ThemeFrame", "FloaterUnfocusBorderColor") })
            colors[role] = Color(role, preferred, fallback).Over(background).Hex(false);
        return new() { FormatVersion = 1, Colors = colors };
    }
    public static HostPaletteDefinition? Read(string directory, ThemeProject? project)
    {
        var asset = project?.Edits.Assets.SingleOrDefault(asset => asset.PayloadPath == PayloadPath);
        if (asset == null) return null;
        Contract.Require(project!.Edits.HostPaletteVersion == 1, "host_palette_version", "This host palette needs a newer project reader");
        return HostPalette.Read(LibraryCatalog.ReadBounded(Contract.Contained(directory, asset.AssetPath), HostPalette.Limit));
    }
    public static bool Apply(string directory, ThemeProject project, BaseCatalog catalog, HostPaletteDefinition palette, string label = "Set host palette")
    {
        ProjectStore.EnsureEditable(project); HostPalette.Validate(palette); var current = Read(directory, project);
        if (current != null && HostPalette.Roles.All(role => string.Equals(current.Colors[role], palette.Colors[role], StringComparison.OrdinalIgnoreCase))) return false;
        var previous = project.Edits.Assets.SingleOrDefault(asset => asset.PayloadPath == PayloadPath);
        var edits = Contract.Read<ProjectEdits>(Contract.Bytes(project.Edits)) with { HostPaletteVersion = 1 };
        var asset = ProjectAssets.Prepare(directory, PayloadPath, HostPalette.Bytes(palette), preservedOriginal: previous?.OriginalPath);
        edits.Assets.RemoveAll(asset => asset.PayloadPath == PayloadPath); edits.Assets.Add(asset);
        PackageStore.Compile(directory, project with { Edits = edits }, catalog);
        project.Checkpoint(label); project.Edits = edits; return true;
    }
    public static bool SetColor(string directory, ThemeProject project, BaseCatalog catalog, string role, string value)
    {
        ProjectStore.EnsureEditable(project);
        var palette = HostPalette.WithColor(Read(directory, project) ?? HostPalette.Midnight, role, value);
        return Apply(directory, project, catalog, palette, "Set host " + role);
    }
    public static bool Reset(string directory, ThemeProject project, BaseCatalog catalog)
    {
        ProjectStore.EnsureEditable(project);
        var edits = Contract.Read<ProjectEdits>(Contract.Bytes(project.Edits)) with { HostPaletteVersion = null };
        var removed = edits.Assets.RemoveAll(asset => asset.PayloadPath == PayloadPath);
        if (removed == 0 && project.Edits.HostPaletteVersion == null) return false;
        PackageStore.Compile(directory, project with { Edits = edits }, catalog);
        project.Checkpoint("Reset host palette"); project.Edits = edits; return true;
    }
}
