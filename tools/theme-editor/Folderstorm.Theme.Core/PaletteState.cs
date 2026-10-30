// SPDX-License-Identifier: LGPL-2.1-or-later
using System.Xml.Linq;

namespace Folderstorm.Theme;

public sealed class PaletteState
{
    internal readonly byte[]? RawBytes;
    internal readonly ProjectAsset? RawAsset;
    private readonly ColorResolver resolver;
    public IReadOnlyDictionary<string, ColorDefinition> Imported { get; }
    public IReadOnlyDictionary<string, string> Literals { get; }
    public IReadOnlyDictionary<string, string> Links { get; }
    public ColorInspection Inspect(string name) => resolver.Inspect(name);
    public Dictionary<string, string> CompileChanges() => resolver.Compile();

    internal PaletteState(BaseCatalog catalog, ProjectEdits edits, ProjectAsset? asset, byte[]? bytes)
    {
        RawAsset = asset; RawBytes = bytes;
        var definitions = new Dictionary<string, ColorDefinition>(StringComparer.Ordinal);
        if (bytes is { Length: > 0 })
        {
            PackageStore.ValidateColors(bytes, catalog);
            foreach (var node in Contract.Xml(bytes).Root!.Elements("color"))
            {
                var name = node.Attribute("name")!.Value;
                definitions.Add(name, new(name, (string?)node.Attribute("value"), (string?)node.Attribute("reference"), "viewer/colors.xml · " + asset?.AssetPath));
            }
        }
        Imported = definitions;
        var literals = definitions.Values.Where(color => color.Value != null).ToDictionary(color => color.Name, color => color.Value!, StringComparer.Ordinal);
        var links = definitions.Values.Where(color => color.Reference != null).ToDictionary(color => color.Name, color => color.Reference!, StringComparer.Ordinal);
        foreach (var (name, value) in edits.Colors) { links.Remove(name); literals[name] = value; }
        foreach (var (name, target) in edits.ColorLinks) { literals.Remove(name); links[name] = target; }
        Literals = literals; Links = links;
        var inherited = new ColorResolver(catalog, new Dictionary<string, string>(), null);
        var memo = new Dictionary<string, ColorInspection>(StringComparer.Ordinal); var visiting = new HashSet<string>(StringComparer.Ordinal);
        ColorInspection Baseline(string name)
        {
            if (memo.TryGetValue(name, out var found)) return found;
            if (!definitions.TryGetValue(name, out var raw)) return inherited.Inspect(name);
            Contract.Require(visiting.Count < 128, "color_depth", "Imported color references exceed128 entries");
            Contract.Require(visiting.Add(name), "color_cycle", "Imported color reference cycle at " + name);
            try
            {
                ColorInspection value;
                if (raw.Reference == null) value = new(name, ThemeColor.Parse(raw.Value!), raw.Source, [name], true, false);
                else { var target = Baseline(raw.Reference); value = new(name, target.Value, raw.Source, [name, .. target.ReferenceChain], true, false); }
                memo[name] = value; return value;
            }
            finally { visiting.Remove(name); }
        }
        // Native base references have already resolved before the package layer loads.
        resolver = new ColorResolver(catalog, edits.Colors, edits.ColorLinks, bytes is { Length: > 0 } ? definitions : null, bytes is { Length: > 0 } ? Baseline : null);
    }
}

public static partial class PaletteProject
{
    public const string PayloadPath = "viewer/colors.xml";
    public static PaletteState Read(string? directory, ThemeProject? project, BaseCatalog catalog)
    {
        var assets = project?.Edits.Assets.Where(asset => asset.PayloadPath == PayloadPath).ToArray() ?? [];
        Contract.Require(assets.Length <= 1, "collision", PayloadPath);
        var asset = assets.SingleOrDefault(); byte[]? bytes = null;
        if (asset != null)
        {
            Contract.Require(directory != null, "palette_context", "Imported colors require the project directory");
            bytes = LibraryCatalog.ReadBounded(Contract.Contained(directory, asset.AssetPath), Contract.ManifestLimit);
            Contract.Require(bytes.Length > 0, "xml", "Palette XML is empty");
        }
        return new(catalog, project?.Edits ?? new(), asset, bytes);
    }
    internal static byte[]? CompilePayload(string directory, ThemeProject project, BaseCatalog catalog)
    {
        var state = Read(directory, project, catalog); var changes = state.CompileChanges();
        if (changes.Count == 0) return state.RawBytes is { Length: > 0 } ? state.RawBytes : null;
        var xml = state.RawBytes is { Length: > 0 } ? Contract.Xml(state.RawBytes) : new XDocument(new XElement("colors"));
        foreach (var (name, value) in changes.OrderBy(item => item.Key, StringComparer.Ordinal))
        {
            var node = xml.Root!.Elements("color").SingleOrDefault(node => (string?)node.Attribute("name") == name);
            if (node == null) { node = new XElement("color", new XAttribute("name", name)); xml.Root.Add(node); }
            node.SetAttributeValue("reference", null); node.SetAttributeValue("value", value);
        }
        return Contract.XmlBytes(xml);
    }
}
