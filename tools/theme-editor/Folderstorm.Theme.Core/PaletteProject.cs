// SPDX-License-Identifier: LGPL-2.1-or-later
using System.Xml.Linq;
namespace Folderstorm.Theme;

public sealed record PaletteChange(string Name, string? Value = null, string? Link = null);
public sealed record PalettePreview(ProjectEdits Edits, int DirectChanges, Dictionary<string, string> EffectiveChanges, string[] NativeOnly, byte[]? ImportedPayload = null, bool ImportedChanged = false);
public static partial class PaletteProject
{
    public static PalettePreview Preview(ThemeProject project, BaseCatalog catalog, IEnumerable<PaletteChange> changes, string? directory = null)
    {
        ProjectStore.EnsureEditable(project);
        Contract.Require(project.BaseSkin == catalog.Skin && project.BaseScheme == catalog.Scheme, "base", "Palette base differs from the project");
        var selected = changes.Take(catalog.Colors.Count + 1).ToArray();
        Contract.Require(selected.Length <= catalog.Colors.Count && selected.Select(item => item.Name).Distinct(StringComparer.Ordinal).Count() == selected.Length,
            "palette_batch", "Choose distinct known colors");
        var colors = new Dictionary<string, string>(project.Edits.Colors, StringComparer.Ordinal);
        var links = new Dictionary<string, string>(project.Edits.ColorLinks, StringComparer.Ordinal);
        var before = Read(directory, project, catalog);
        var raw = before.RawBytes is { Length: > 0 } ? Contract.Xml(before.RawBytes) : null; var rawChanged = false; var direct = 0;
        foreach (var item in selected)
        {
            Contract.Require(catalog.Colors.ContainsKey(item.Name), "color", "Unknown color: " + item.Name);
            Contract.Require(item.Value == null || item.Link == null, "palette_batch", "Choose a value or a link for each color");
            var literal = item.Value == null ? null : ThemeColor.Parse(item.Value).NativeText;
            if (literal != null && before.Literals.TryGetValue(item.Name, out var old) && ThemeColor.Parse(old).NativeText == literal && !before.Links.ContainsKey(item.Name) ||
                item.Link != null && before.Links.GetValueOrDefault(item.Name) == item.Link) continue;
            var changed = colors.Remove(item.Name) | links.Remove(item.Name);
            if (literal != null) { colors[item.Name] = literal; changed = true; }
            if (item.Link != null) { links[item.Name] = item.Link; changed = true; }
            if (literal == null && item.Link == null && raw?.Root!.Elements("color").SingleOrDefault(node => (string?)node.Attribute("name") == item.Name) is { } node)
            { node.Remove(); rawChanged = changed = true; }
            if (changed) ++direct;
        }
        var edits = project.Edits with { Colors = colors, ColorLinks = links };
        var rawBytes = rawChanged ? raw!.Root!.HasElements ? Contract.XmlBytes(raw) : [] : before.RawBytes;
        var after = new PaletteState(catalog, edits, before.RawAsset, rawBytes); var compiled = after.CompileChanges();
        var affected = new Dictionary<string, string>(StringComparer.Ordinal); var nativeOnly = new List<string>();
        IEnumerable<string> previous;
        try { previous = before.CompileChanges().Keys; }
        catch (ThemeException) { previous = project.Edits.Colors.Keys.Concat(project.Edits.ColorLinks.Keys); }
        var candidates = compiled.Keys.Concat(previous).Concat(selected.Select(item => item.Name)).Concat(before.Imported.Keys).Concat(after.Imported.Keys).Distinct(StringComparer.Ordinal);
        foreach (var name in candidates)
        {
            string next;
            try { next = after.Inspect(name).Value.NativeText; }
            catch (ThemeException) { nativeOnly.Add(name); continue; }
            string? old = null;
            try { old = before.Inspect(name).Value.NativeText; }
            catch (ThemeException) { }
            if (next != old) affected[name] = next;
        }
        return new(edits, direct, affected, nativeOnly.ToArray(), rawBytes, rawChanged);
    }
    public static int Apply(ThemeProject project, BaseCatalog catalog, IEnumerable<PaletteChange> changes, string label, string? directory = null)
    {
        var prepared = Preview(project, catalog, changes, directory); if (prepared.DirectChanges == 0) return 0;
        var edits = prepared.Edits;
        if (prepared.ImportedChanged)
        {
            Contract.Require(directory != null, "palette_context", "Imported colors require the project directory");
            var previous = project.Edits.Assets.Single(asset => asset.PayloadPath == PayloadPath);
            var assets = new List<ProjectAsset>(edits.Assets); assets.RemoveAll(asset => asset.PayloadPath == PayloadPath);
            if (prepared.ImportedPayload is { Length: > 0 } bytes) assets.Add(ProjectAssets.Prepare(directory, PayloadPath, bytes, preservedOriginal: previous.OriginalPath));
            edits = edits with { Assets = assets };
        }
        if (directory != null) PackageStore.Compile(directory, project with { Edits = edits }, catalog);
        project.Checkpoint(label); project.Edits = edits; return prepared.DirectChanges;
    }
    public static bool ResetAll(string directory, ThemeProject project, BaseCatalog catalog)
    {
        var state = Read(directory, project, catalog);
        var names = state.Literals.Keys.Concat(state.Links.Keys).Distinct(StringComparer.Ordinal).Select(name => new PaletteChange(name));
        return Apply(project, catalog, names, "Reset palette", directory) > 0;
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
