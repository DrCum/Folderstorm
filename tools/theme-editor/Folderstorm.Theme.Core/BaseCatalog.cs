// SPDX-License-Identifier: LGPL-2.1-or-later
using System.Xml.Linq;

namespace Folderstorm.Theme;

public sealed class BaseCatalog
{
    public string Root { get; }
    public string Skin { get; }
    public string Scheme { get; }
    public string[] Layers { get; }
    public Dictionary<string, ColorDefinition> Colors { get; } = new(StringComparer.Ordinal);
    public Dictionary<string, TextureDefinition> Textures { get; } = new(StringComparer.Ordinal);
    public List<string> FontConfigurations { get; } = [];
    public Scheme[] Schemes { get; }

    public BaseCatalog(string root, string skin = "ansastorm", string scheme = "folderstorm")
    {
        Root = Path.GetFullPath(Directory.Exists(Path.Combine(root, "indra/newview/skins")) ? Path.Combine(root, "indra/newview") : root);
        Contract.Require(skin == "ansastorm", "base", "Only classic AnsaStorm is enabled in the initial adapter");
        Skin = skin; Scheme = scheme;
        var skins = Contract.Xml(File.ReadAllBytes(Path.Combine(Root, "skins/skins.xml")));
        var map = skins.Root!.Element("array")!.Elements("map").First(m => Value(m, "folder")?.Value == skin);
        Schemes = Value(map, "themes")!.Elements("map").Select(m => new Scheme(Value(m, "folder")!.Value, Value(m, "name")!.Value)).ToArray();
        Contract.Require(Schemes.Any(s => s.Id == scheme), "base", "Unknown base scheme: " + scheme);
        Layers = scheme.Length > 0 ? ["skins/default", "skins/" + skin, "skins/" + skin + "/themes/" + scheme] : ["skins/default", "skins/" + skin];
        foreach (var layer in Layers)
        {
            var colorPath = layer + "/colors.xml";
            if (File.Exists(Path.Combine(Root, colorPath)))
                foreach (var node in Contract.Xml(File.ReadAllBytes(Path.Combine(Root, colorPath))).Root!.Elements("color"))
                {
                    var name = (string?)node.Attribute("name");
                    if (name != null) Colors[name] = new(name, (string?)node.Attribute("value"), (string?)node.Attribute("reference"), colorPath);
                }
            var texturePath = Path.Combine(Root, layer, "textures/textures.xml");
            if (File.Exists(texturePath))
                foreach (var node in Contract.Xml(File.ReadAllBytes(texturePath)).Root!.Elements())
                {
                    var name = (string?)node.Attribute("name");
                    if (name != null) Textures[name] = new(name, node.Attributes().ToDictionary(a => a.Name.LocalName, a => a.Value, StringComparer.Ordinal), null);
                }
        }
        foreach (var name in Textures.Keys.ToArray())
        {
            var texture = Textures[name];
            if (texture.Attributes.TryGetValue("file_name", out var file))
                Textures[name] = texture with { Source = Resolve("textures/" + file) };
        }
        if (Directory.Exists(Path.Combine(Root, "fonts")))
            FontConfigurations.AddRange(Directory.EnumerateFiles(Path.Combine(Root, "fonts"), "fonts*.xml").Select(Path.GetFileName).OfType<string>().Order(StringComparer.Ordinal));
    }
    private static XElement? Value(XElement map, string key) => map.Elements("key").FirstOrDefault(k => k.Value == key)?.ElementsAfterSelf().FirstOrDefault();
    public string? Resolve(string relative)
    {
        Contract.PathName(relative);
        return Layers.Select(l => l + "/" + relative).LastOrDefault(p => File.Exists(Path.Combine(Root, p)));
    }
    public byte[] ResolveBytes(string relative)
    {
        var path = Resolve(relative) ?? throw new ThemeException("reference", "Missing base resource: " + relative);
        return File.ReadAllBytes(Contract.Contained(Root, path));
    }
    public DependencySet Fingerprint()
    {
        var logical = LogicalResources();
        var records = new List<Dependency>();
        var checkedDirectories = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var name in logical)
            for (var index = 0; index < Layers.Length; ++index) Add(Layers[index] + "/" + name, name, index);
        if (Directory.Exists(Path.Combine(Root, "fonts")))
            foreach (var path in Directory.EnumerateFiles(Path.Combine(Root, "fonts"), "*", SearchOption.AllDirectories))
                Add(Path.GetRelativePath(Root, path).Replace('\\', '/'), "fonts/" + Path.GetFileName(path), Layers.Length);
        return new(1, Skin, Scheme, Layers, records.OrderBy(r => r.Path, StringComparer.Ordinal).ToArray());

        void Add(string relative, string id, int index)
        {
            Contract.PathName(relative);
            var path = Path.Combine(Root, relative); CheckParents(path); var present = File.Exists(path);
            if (present) Contract.Require(!File.GetAttributes(path).HasFlag(FileAttributes.ReparsePoint), "link", relative);
            records.Add(new(relative, present, present ? new FileInfo(path).Length : 0, present ? Contract.HashFile(path) : "", id, index));
        }
        void CheckParents(string path)
        {
            var parent = Path.GetDirectoryName(path);
            if (parent == null || !checkedDirectories.Add(parent)) return;
            if (Directory.Exists(parent)) Contract.Require(!File.GetAttributes(parent).HasFlag(FileAttributes.ReparsePoint), "link", parent);
            if (!string.Equals(parent, Root, StringComparison.OrdinalIgnoreCase)) CheckParents(parent);
        }
    }
    public void Verify(DependencySet dependencies)
    {
        Contract.Require(dependencies.FormatVersion == 1 && dependencies.BaseSkin == Skin && dependencies.BaseScheme == Scheme &&
            dependencies.Roots.SequenceEqual(Layers), "base", "Base adapter/scheme does not match");
        foreach (var entry in dependencies.Files)
        {
            var path = Contract.Contained(Root, entry.Path); var present = File.Exists(path);
            Contract.Require(entry.Present == present && (!present || new FileInfo(path).Length == entry.Size && Contract.HashFile(path) == entry.Sha256), "base_changed", entry.Path);
        }
        var expected = LogicalResources().SelectMany(name => Layers.Select(layer => layer + "/" + name)).ToHashSet(StringComparer.Ordinal);
        if (Directory.Exists(Path.Combine(Root, "fonts")))
            expected.UnionWith(Directory.EnumerateFiles(Path.Combine(Root, "fonts"), "*", SearchOption.AllDirectories).Select(path => Path.GetRelativePath(Root, path).Replace('\\', '/')));
        expected.SymmetricExceptWith(dependencies.Files.Select(file => file.Path));
        Contract.Require(expected.Count == 0, "base_changed", "Dependency closure differs: " + expected.FirstOrDefault());
    }
    private SortedSet<string> LogicalResources()
    {
        var names = new SortedSet<string>(StringComparer.Ordinal);
        foreach (var layer in Layers)
            foreach (var path in Directory.EnumerateFiles(Path.Combine(Root, layer), "*", SearchOption.AllDirectories))
            {
                var relative = Path.GetRelativePath(Path.Combine(Root, layer), path).Replace('\\', '/');
                if (relative == "colors.xml" || relative.StartsWith("textures/", StringComparison.Ordinal) || relative.StartsWith("xui/en/", StringComparison.Ordinal)) names.Add(relative);
            }
        return names;
    }
    public Dictionary<string, string> CompileColors(IReadOnlyDictionary<string, string> edits)
    {
        var output = new Dictionary<string, string>(StringComparer.Ordinal);
        foreach (var (name, value) in edits)
        { Contract.Require(Colors.ContainsKey(name), "color", "Unknown color: " + name); Contract.Rgba(value); output[name] = value; }
        bool changed;
        do
        {
            changed = false;
            foreach (var color in Colors.Values)
                if (!output.ContainsKey(color.Name) && color.Reference != null && output.TryGetValue(color.Reference, out var value))
                { output[color.Name] = value; changed = true; }
        } while (changed);
        foreach (var (name, role, alpha) in AlphaLinks)
            if (!edits.ContainsKey(name) && Colors.ContainsKey(name) && output.TryGetValue(role, out var value))
            { var rgba = Contract.Rgba(value); rgba[3] = alpha; output[name] = Contract.ColorText(rgba); }
        return output;
    }
    private static readonly (string, string, double)[] AlphaLinks =
    [
        ("EmphasisColor_13", "ThemeAccent", .13), ("EmphasisColor_35", "ThemeAccent", .35),
        ("SchemeLightest_35", "ThemeAccent", .35), ("DefaultHighlightLight", "ThemeAccent", .25), ("DefaultHighlightDark", "ThemeAccent", .10),
        ("AnsaStormBlue_Dk_86", "ThemeSelected", .86), ("AnsaStormBlue_Dk_66", "ThemeSelected", .66),
        ("AnsaStormBlue_Dk_33", "ThemeSelected", .33), ("AnsaStormBlue_Dk_13", "ThemeSelected", .13),
        ("AnsaStormBlue_Lt_13", "ThemeAccent", .13), ("AnsaStormBlue_Dk_Emphasis", "ThemeBright", .66),
        ("SL-Background_50", "ThemePanel", .50), ("SL-Background_66", "ThemePanel", .66),
        ("SL-Foreground_50", "ThemeHover", .50), ("SL-Foreground_66", "ThemeHover", .66)
    ];
}
