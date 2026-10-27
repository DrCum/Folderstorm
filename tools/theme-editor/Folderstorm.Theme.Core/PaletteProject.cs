// SPDX-License-Identifier: LGPL-2.1-or-later
namespace Folderstorm.Theme;

public sealed record PaletteChange(string Name, string? Value = null, string? Link = null);
public sealed record PalettePreview(ProjectEdits Edits, int DirectChanges, Dictionary<string, string> EffectiveChanges, string[] NativeOnly);
public static class PaletteProject
{
    public static PalettePreview Preview(ThemeProject project, BaseCatalog catalog, IEnumerable<PaletteChange> changes)
    {
        ProjectStore.EnsureEditable(project);
        Contract.Require(project.BaseSkin == catalog.Skin && project.BaseScheme == catalog.Scheme, "base", "Palette base differs from the project");
        var selected = changes.Take(catalog.Colors.Count + 1).ToArray();
        Contract.Require(selected.Length <= catalog.Colors.Count && selected.Select(item => item.Name).Distinct(StringComparer.Ordinal).Count() == selected.Length,
            "palette_batch", "Choose distinct known colors");
        var colors = new Dictionary<string, string>(project.Edits.Colors, StringComparer.Ordinal);
        var links = new Dictionary<string, string>(project.Edits.ColorLinks, StringComparer.Ordinal);
        foreach (var item in selected)
        {
            Contract.Require(catalog.Colors.ContainsKey(item.Name), "color", "Unknown color: " + item.Name);
            Contract.Require(item.Value == null || item.Link == null, "palette_batch", "Choose a value or a link for each color");
            colors.Remove(item.Name); links.Remove(item.Name);
            if (item.Value != null) colors[item.Name] = ThemeColor.Parse(item.Value).NativeText;
            if (item.Link != null) links[item.Name] = item.Link;
        }
        var resolver = new ColorResolver(catalog, colors, links); var compiled = resolver.Compile();
        var direct = selected.Count(item => project.Edits.Colors.GetValueOrDefault(item.Name) != colors.GetValueOrDefault(item.Name) ||
            project.Edits.ColorLinks.GetValueOrDefault(item.Name) != links.GetValueOrDefault(item.Name));
        var affected = new Dictionary<string, string>(StringComparer.Ordinal); var nativeOnly = new List<string>();
        IEnumerable<string> previous; ColorResolver? beforeResolver;
        try { beforeResolver = new(catalog, project.Edits.Colors, project.Edits.ColorLinks); previous = beforeResolver.Compile().Keys; }
        catch (ThemeException) { beforeResolver = null; previous = project.Edits.Colors.Keys.Concat(project.Edits.ColorLinks.Keys); }
        var candidates = compiled.Keys.Concat(previous).Distinct(StringComparer.Ordinal);
        foreach (var name in candidates)
        {
            string after;
            try { after = resolver.Inspect(name).Value.NativeText; }
            catch (ThemeException) { nativeOnly.Add(name); continue; }
            string? before = null;
            try { before = beforeResolver?.Inspect(name).Value.NativeText; }
            catch (ThemeException) { }
            if (after != before) affected[name] = after;
        }
        return new(project.Edits with { Colors = colors, ColorLinks = links }, direct, affected, nativeOnly.ToArray());
    }
    public static int Apply(ThemeProject project, BaseCatalog catalog, IEnumerable<PaletteChange> changes, string label)
    {
        var prepared = Preview(project, catalog, changes); if (prepared.DirectChanges == 0) return 0;
        project.Checkpoint(label); project.Edits = prepared.Edits; return prepared.DirectChanges;
    }
}

public sealed record PalettePreset(string Name, string[] Colors)
{
    private static readonly (string Role, string? Fallback)[] Roles =
    [
        ("ThemeBg", "MenuBarBgColor"), ("ThemePanel", "SL-Background"), ("ThemeLine", "FloaterUnfocusBorderColor"),
        ("ThemeText", "TextFgColor"), ("ThemeMuted", "LabelDisabledColor"), ("ThemeAccent", "AccentColor"),
        ("ThemeBright", "EmphasisColor"), ("ThemeGold", null), ("ThemeTitle", "FloaterFocusBorderColor"),
        ("ThemeSelected", "ScrollSelectedBGColor"), ("ThemeInventory", null), ("ThemeHover", "ScrollHoveredColor"),
        ("ThemeInput", "TextBgWriteableColor"), ("ThemeInk", null), ("ThemeFrame", null)
    ];
    public PaletteChange[] Changes(BaseCatalog catalog)
    {
        Contract.Require(Colors.Length == Roles.Length, "palette_preset", "Preset must define every palette role");
        var result = Roles.Select((role, index) => new PaletteChange(catalog.Colors.ContainsKey(role.Role) ? role.Role : role.Fallback ?? "", Colors[index]))
            .Where(item => catalog.Colors.ContainsKey(item.Name)).ToList();
        if (Name == "Paper")
            foreach (var name in new[] { "ButtonLabelColor", "ButtonLabelSelectedColor" })
                if (catalog.Colors.ContainsKey(name)) result.Add(new(name, "#F5F2FF"));
        return result.ToArray();
    }
    public static PalettePreset[] Choices =>
    [
        new("Violet", ["#111016", "#1A1921", "#302D3B", "#F5F3FA", "#ADA9BA", "#BFA4FC", "#9F74F2", "#F0CC53", "#302140", "#51347D", "#51347D", "#352943", "#111016", "#211332", "#735595"]),
        new("Mint", ["#0D1616", "#152222", "#29483D", "#E8FFF9", "#9EBAB2", "#64D8B2", "#25BA8C", "#FFD37A", "#193A30", "#225746", "#225746", "#1B3A32", "#0B1714", "#0C2821", "#3B826C"]),
        new("Paper", ["#EDEAF3", "#F8F6FC", "#BEB5CF", "#211A30", "#635970", "#65409A", "#8054BB", "#896000", "#D6CAE6", "#CEBCE8", "#CEBCE8", "#E0D5EE", "#FFFFFF", "#3F2B5C", "#9B87B8"])
    ];
}
