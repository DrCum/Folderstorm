// SPDX-License-Identifier: LGPL-2.1-or-later
namespace Folderstorm.Theme;

internal sealed class ColorResolver
{
    private sealed record Resolved(string Text, string[] Chain);
    private readonly BaseCatalog catalog;
    private readonly IReadOnlyDictionary<string, string> literals, links;
    private readonly IReadOnlyDictionary<string, ColorDefinition>? imported;
    private readonly Func<string, ColorInspection>? baseline;
    private readonly Dictionary<string, (string Role, double Alpha)> alphaLinks;
    private readonly HashSet<string> affected = new(StringComparer.Ordinal);
    private readonly Dictionary<string, Resolved> memo = new(StringComparer.Ordinal);
    private readonly HashSet<string> visiting = new(StringComparer.Ordinal);

    public ColorResolver(BaseCatalog catalog, IReadOnlyDictionary<string, string> literals, IReadOnlyDictionary<string, string>? links,
        IReadOnlyDictionary<string, ColorDefinition>? imported = null, Func<string, ColorInspection>? baseline = null)
    {
        this.catalog = catalog; this.literals = literals; this.links = links ?? new Dictionary<string, string>();
        this.imported = imported; this.baseline = baseline;
        alphaLinks = BaseCatalog.AlphaLinks.Where(link => catalog.Colors.ContainsKey(link.Name))
            .ToDictionary(link => link.Name, link => (link.Role, link.Alpha), StringComparer.Ordinal);
        foreach (var (name, value) in literals)
        {
            Contract.Require(catalog.Colors.ContainsKey(name), "color", "Unknown color: " + name);
            Contract.Require(!this.links.ContainsKey(name), "color", "A color cannot be both a literal and a link: " + name);
            Contract.Rgba(value); affected.Add(name);
        }
        foreach (var (name, target) in this.links)
        {
            Contract.Require(catalog.Colors.ContainsKey(name) && catalog.Colors.ContainsKey(target), "reference", "Unknown color link: " + name + " → " + target);
            affected.Add(name);
        }
        // Only resolve the edited closure. Unrelated legacy base values remain untouched.
        bool changed;
        do
        {
            changed = false;
            foreach (var color in catalog.Colors.Values)
            {
                var raw = imported?.GetValueOrDefault(color.Name);
                var reference = raw?.Reference ?? (raw == null ? color.Reference : null);
                if (!affected.Contains(color.Name) &&
                    (reference != null && affected.Contains(reference) ||
                     raw == null && alphaLinks.TryGetValue(color.Name, out var alpha) && affected.Contains(alpha.Role)))
                    changed |= affected.Add(color.Name);
            }
        } while (changed);
    }

    public Dictionary<string, string> Compile() => affected.Order(StringComparer.Ordinal).ToDictionary(name => name, name => Resolve(name, 0).Text, StringComparer.Ordinal);

    public ColorInspection Inspect(string name)
    {
        var resolved = Resolve(name, 0);
        return new(name, ThemeColor.Parse(resolved.Text), imported?.GetValueOrDefault(name)?.Source ?? catalog.Colors[name].Source, resolved.Chain,
            literals.ContainsKey(name) || links.ContainsKey(name) || imported?.ContainsKey(name) == true, affected.Contains(name));
    }

    private Resolved Resolve(string name, int depth)
    {
        Contract.Require(depth < 128, "color_depth", "Color references exceed 128 entries");
        if (memo.TryGetValue(name, out var cached))
        {
            Contract.Require(depth + cached.Chain.Length <= 128, "color_depth", "Color references exceed 128 entries");
            return cached;
        }
        Contract.Require(catalog.Colors.TryGetValue(name, out var color), "reference", "Unknown color: " + name);
        var raw = imported?.GetValueOrDefault(name); color = raw ?? color;
        Contract.Require(visiting.Add(name), "color_cycle", "Color reference cycle at " + name);
        try
        {
            Resolved result;
            if (literals.TryGetValue(name, out var literal)) result = new(literal, [name]);
            else if (links.TryGetValue(name, out var target)) result = Follow(target);
            else if (baseline != null && !affected.Contains(name))
            {
                var inherited = baseline(name); result = new(inherited.Value.NativeText, inherited.ReferenceChain);
            }
            else if (raw == null && alphaLinks.TryGetValue(name, out var alpha) && affected.Contains(alpha.Role))
            {
                var role = Follow(alpha.Role); var rgba = Contract.Rgba(role.Text); rgba[3] = alpha.Alpha;
                result = role with { Text = Contract.ColorText(rgba) };
            }
            else if (color.Reference != null) result = Follow(color.Reference);
            else
            {
                Contract.Require(color.Value != null, "color_literal", "Inherited color has no RGBA value: " + name);
                try { Contract.Rgba(color.Value); }
                catch (ThemeException error) when (error.Code == "color")
                { throw new ThemeException("color_literal", "Inherited color " + name + " is not a numeric RGBA value: " + color.Value); }
                result = new(color.Value, [name]);
            }
            memo[name] = result; return result;
        }
        finally { visiting.Remove(name); }

        Resolved Follow(string target)
        {
            var inherited = Resolve(target, depth + 1);
            return new(inherited.Text, [name, .. inherited.Chain]);
        }
    }
}

public static class ColorAuthoring
{
    public static bool SetLiteral(ThemeProject project, BaseCatalog catalog, string name, string value, string? directory = null)
    {
        return PaletteProject.Apply(project, catalog, [new(name, Value: value)], "Set " + name, directory) > 0;
    }
    public static bool SetLink(ThemeProject project, BaseCatalog catalog, string name, string target, string? directory = null)
    {
        return PaletteProject.Apply(project, catalog, [new(name, Link: target)], "Link " + name, directory) > 0;
    }
    public static bool Reset(ThemeProject project, BaseCatalog catalog, string name, string? directory = null)
    {
        return PaletteProject.Apply(project, catalog, [new(name)], "Reset " + name, directory) > 0;
    }
}
