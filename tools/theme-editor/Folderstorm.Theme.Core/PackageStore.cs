// SPDX-License-Identifier: LGPL-2.1-or-later
using System.IO.Compression;
using System.Text;
using System.Text.Json;
using System.Xml.Linq;

namespace Folderstorm.Theme;

public sealed record ValidatedTheme(ThemeManifest Manifest, string Revision, Dictionary<string, byte[]> Files);

public static class PackageStore
{
    private static readonly DateTimeOffset ZipEpoch = new(2000, 1, 1, 0, 0, 0, TimeSpan.Zero);
    private static readonly string[] ManifestFields = ["format_version", "theme_id", "package_version", "display_name", "base_skin", "base_scheme", "adapter_id", "adapter_version", "required_capabilities", "base_fingerprint", "files", "font_dependencies", "author", "description", "extensions"];

    public static string Kind(string path)
    {
        Contract.PathName(path);
        if (path == "base-dependencies.json") return "dependencies";
        if (path == "viewer/colors.xml") return "colors";
        if (path == "viewer/textures/textures.xml") return "textures";
        if (path == "viewer/fonts/fonts.xml") return "font_config";
        if (path == "host/palette.json") return "host_palette";
        if (path.StartsWith("viewer/xui/en/", StringComparison.Ordinal) && XuiPolicy.Files.Contains(path[7..])) return "xui";
        var extension = Path.GetExtension(path).ToLowerInvariant();
        if (path == "preview.png" || path.StartsWith("viewer/textures/", StringComparison.Ordinal) && extension is ".png" or ".tga" or ".jpg" or ".jpeg" or ".j2c" or ".bmp") return "image";
        if (path.StartsWith("viewer/fonts/", StringComparison.Ordinal) && extension is ".ttf" or ".otf" or ".ttc" or ".otc") return "font";
        if (path.StartsWith("licenses/", StringComparison.Ordinal) && extension is ".txt" or ".md") return "license";
        throw new ThemeException("kind", "Unsupported payload: " + path);
    }

    public static ThemeManifest Manifest(byte[] bytes)
    {
        using var document = Contract.Parse(bytes);
        var root = document.RootElement;
        Contract.Require(root.ValueKind == JsonValueKind.Object, "schema", "Manifest must be an object");
        foreach (var field in root.EnumerateObject()) Contract.Require(ManifestFields.Contains(field.Name), "schema", field.Name);
        foreach (var name in new[] { "theme_id", "package_version", "display_name", "base_skin", "base_scheme", "adapter_id", "base_fingerprint" })
            Contract.Require(root.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.String, "schema", name);
        foreach (var name in new[] { "files", "required_capabilities", "font_dependencies" })
            Contract.Require(root.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.Array, "schema", name);
        foreach (var entry in root.GetProperty("files").EnumerateArray())
            Shape(entry, ["path", "sha256", "kind"], ["size"], ["path", "size", "sha256", "kind"]);
        foreach (var entry in root.GetProperty("font_dependencies").EnumerateArray())
            Shape(entry, ["kind", "file"], [], ["kind", "file", "face"]);
        foreach (var entry in root.GetProperty("font_dependencies").EnumerateArray())
            if (entry.TryGetProperty("face", out var face)) Contract.Require(face.ValueKind == JsonValueKind.Number && face.TryGetInt32(out _), "schema", "face");
        var manifest = Contract.Read<ThemeManifest>(bytes);
        Contract.Require(manifest.FormatVersion == 1, "format_version", "Unsupported format");
        Contract.Require(Guid.TryParseExact(manifest.ThemeId, "D", out var id) && id.ToString("D") == manifest.ThemeId, "identity", "Canonical UUID required");
        Contract.Require(Contract.IsVersion(manifest.PackageVersion), "identity", "Invalid package version");
        Contract.Require(manifest.DisplayName.EnumerateRunes().Count() is > 0 and <= 128 && !manifest.DisplayName.Any(char.IsControl), "identity", "Invalid display name");
        Contract.Require(manifest.BaseSkin == "ansastorm" && manifest.AdapterId == "ansastorm-classic" && manifest.AdapterVersion == 1, "base", "Unsupported base adapter");
        if (manifest.BaseScheme.Length > 0) Contract.PathName(manifest.BaseScheme);
        Contract.Require(manifest.BaseScheme.IndexOf('/') < 0, "base", "Invalid scheme ID");
        Contract.Require(manifest.RequiredCapabilities.All(c => c != null && Contract.Capabilities.Contains(c)) &&
            manifest.RequiredCapabilities.Distinct(StringComparer.Ordinal).Count() == manifest.RequiredCapabilities.Length, "capability", "Unsupported or duplicate capability");
        Contract.Require(Contract.IsHash(manifest.BaseFingerprint), "schema", "Invalid base fingerprint");
        Contract.Require(manifest.Files.Length is > 0 and < Contract.EntryLimit, "size", "Invalid payload count");
        var paths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        string? previous = null; long expanded = 0;
        foreach (var file in manifest.Files)
        {
            Contract.Require(file != null && file.Path != null && file.Sha256 != null && file.Kind != null, "schema", "Invalid payload entry");
            Contract.PathName(file!.Path);
            Contract.Require(paths.Add(file.Path), "collision", file.Path);
            Contract.Require(previous == null || string.CompareOrdinal(previous, file.Path) < 0, "schema", "Payload list must be path-sorted"); previous = file.Path;
            Contract.Require(file.Size >= 0 && file.Size <= Limit(file.Kind) && Contract.IsHash(file.Sha256), "size", file.Path);
            expanded = checked(expanded + file.Size);
            Contract.Require(file.Kind == Kind(file.Path), "kind", file.Path);
        }
        Contract.Require(expanded <= Contract.ExpandedLimit && paths.Contains("base-dependencies.json"), "size", "Invalid expanded package or missing dependencies");
        foreach (var font in manifest.FontDependencies)
        {
            Contract.Require(font != null && font.Kind is "embedded" or "installed" && font.Face is >= 0 and <= 128 && font.File != null, "font", "Invalid dependency");
            Contract.PathName(font!.File);
            Contract.Require(font.Kind != "installed" || !font.File.Contains('/'), "font", "Installed font must be a filename");
            if (font.Kind == "embedded") Contract.Require(manifest.Files.Any(f => f.Path == "viewer/fonts/" + font.File && f.Kind == "font"), "font", "Missing embedded font: " + font.File);
        }
        Contract.Require(manifest.Author != null && manifest.Description != null && manifest.Author.EnumerateRunes().Count() <= 1024 && manifest.Description.EnumerateRunes().Count() <= 32768 && manifest.Extensions != null, "schema", "Invalid metadata");
        return manifest;
    }
    private static void Shape(JsonElement value, string[] strings, string[] integers, string[] allowed)
    {
        Contract.Require(value.ValueKind == JsonValueKind.Object, "schema", "Expected an object");
        foreach (var field in value.EnumerateObject()) Contract.Require(allowed.Contains(field.Name), "schema", field.Name);
        foreach (var name in strings) Contract.Require(value.TryGetProperty(name, out var item) && item.ValueKind == JsonValueKind.String, "schema", name);
        foreach (var name in integers) Contract.Require(value.TryGetProperty(name, out var item) && item.ValueKind == JsonValueKind.Number && item.TryGetInt64(out _), "schema", name);
    }
    private static long Limit(string kind) => kind switch
    { "font" => 64L * 1024 * 1024, "image" => 32L * 1024 * 1024, _ => Contract.ManifestLimit };

    public static ValidatedTheme Validate(Dictionary<string, byte[]> files, BaseCatalog? catalog = null)
    {
        Contract.Require(files.TryGetValue("theme.json", out var manifestBytes), "manifest", "Missing theme.json");
        var manifest = Manifest(manifestBytes!);
        Contract.Require(files.Count == manifest.Files.Length + 1, "coverage", "Unlisted or missing payload");
        foreach (var payload in manifest.Files)
            Contract.Require(files.TryGetValue(payload.Path, out var bytes) && bytes.LongLength == payload.Size && Contract.Hash(bytes) == payload.Sha256, "integrity", payload.Path);
        var dependencyBytes = files["base-dependencies.json"];
        Contract.Require(Contract.Hash(dependencyBytes) == manifest.BaseFingerprint, "integrity", "Base fingerprint");
        var dependencies = Contract.Read<DependencySet>(dependencyBytes);
        Contract.Require(dependencies.FormatVersion == 1 && dependencies.BaseSkin == manifest.BaseSkin && dependencies.BaseScheme == manifest.BaseScheme && dependencies.Roots != null && dependencies.Files != null, "base", "Invalid dependency set");
        var dependencyPaths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var dependency in dependencies.Files)
        {
            Contract.PathName(dependency.Path);
            Contract.Require(dependencyPaths.Add(dependency.Path) && dependency.Layer >= 0 && dependency.Size >= 0 &&
                (dependency.Present ? Contract.IsHash(dependency.Sha256) : dependency.Size == 0 && dependency.Sha256 == ""), "base", dependency.Path);
            Contract.Require(dependency.Path.StartsWith("fonts/", StringComparison.Ordinal) || dependencies.Roots.Any(r => dependency.Path.StartsWith(r + "/", StringComparison.Ordinal)), "base", dependency.Path);
        }
        if (catalog != null)
        {
            Contract.Require(catalog.Skin == manifest.BaseSkin && catalog.Scheme == manifest.BaseScheme, "base", "Selected base differs");
            catalog.Verify(dependencies);
        }
        foreach (var payload in manifest.Files)
        {
            var bytes = files[payload.Path];
            switch (payload.Kind)
            {
                case "colors": ValidateColors(bytes, catalog); break;
                case "textures": ValidateTextures(bytes, files, catalog); break;
                case "xui":
                    Contract.Require(catalog != null, "base_required", "XUI validation requires the matching installed base");
                    XuiPolicy.Validate(payload.Path[7..], bytes, catalog!); break;
                case "font_config": ValidateFonts(bytes, manifest, catalog); break;
                case "image": ValidateImage(bytes, payload.Path); break;
                case "host_palette": throw new ThemeException("capability", "Host palette activation is not implemented yet");
            }
        }
        return new(manifest, Contract.Hash(manifestBytes!), files);
    }
    private static void ValidateColors(byte[] bytes, BaseCatalog? catalog)
    {
        var root = Contract.Xml(bytes).Root!; Contract.Require(root.Name == "colors", "xml", "Expected colors");
        var names = new Dictionary<string, string?>(StringComparer.Ordinal);
        foreach (var node in root.Elements())
        {
            var name = (string?)node.Attribute("name");
            Contract.Require(node.Name == "color" && name != null && !node.HasElements && node.Attributes().All(a => a.Name.LocalName is "name" or "value" or "reference"), "color", "Invalid color entry");
            Contract.Require(names.TryAdd(name!, (string?)node.Attribute("reference")), "color", "Duplicate color: " + name);
            Contract.Require(catalog == null || catalog.Colors.ContainsKey(name!), "color", "Unknown color: " + name);
            var value = (string?)node.Attribute("value"); var reference = (string?)node.Attribute("reference");
            Contract.Require((value != null) != (reference != null), "color", "Specify a value or reference");
            if (value != null) Contract.Rgba(value);
        }
        foreach (var name in names.Keys)
        {
            var visited = new HashSet<string>(StringComparer.Ordinal) { name }; var current = names[name];
            while (current != null)
            {
                Contract.Require(visited.Add(current), "color_cycle", name);
                if (names.TryGetValue(current, out var next)) current = next;
                else { Contract.Require(catalog == null || catalog.Colors.ContainsKey(current), "reference", current); break; }
            }
        }
    }
    private static void ValidateTextures(byte[] bytes, Dictionary<string, byte[]> files, BaseCatalog? catalog)
    {
        var root = Contract.Xml(bytes).Root!; Contract.Require(root.Name == "textures", "xml", "Expected textures");
        var names = new HashSet<string>(StringComparer.Ordinal);
        foreach (var node in root.Elements())
        {
            var name = (string?)node.Attribute("name"); var file = (string?)node.Attribute("file_name");
            Contract.Require(node.Name == "texture" && name != null && file != null && !node.HasElements && names.Add(name), "texture", "Invalid registration");
            Contract.PathName(file!);
            Contract.Require(catalog == null || catalog.Textures.ContainsKey(name!), "texture", "Unknown texture: " + name);
            Contract.Require(files.ContainsKey("viewer/textures/" + file) || catalog?.Resolve("textures/" + file) != null, "reference", "Missing image: " + file);
            foreach (var attribute in node.Attributes())
            {
                var key = attribute.Name.LocalName;
                Contract.Require(key is "name" or "file_name" or "preload" or "scale.left" or "scale.top" or "scale.right" or "scale.bottom" or "clip.left" or "clip.top" or "clip.right" or "clip.bottom", "texture", key);
                if (key.StartsWith("scale.", StringComparison.Ordinal) || key.StartsWith("clip.", StringComparison.Ordinal))
                    Contract.Require(double.TryParse(attribute.Value, System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out var value) && double.IsFinite(value) && value is >= 0 and <= 16384, "texture", key);
            }
            foreach (var prefix in new[] { "scale.", "clip." })
                foreach (var (low, high) in new[] { ("left", "right"), ("bottom", "top") })
                    if (node.Attribute(prefix + low) is { } first && node.Attribute(prefix + high) is { } last)
                        Contract.Require(double.Parse(first.Value, System.Globalization.CultureInfo.InvariantCulture) <= double.Parse(last.Value, System.Globalization.CultureInfo.InvariantCulture), "texture", "Reversed " + prefix + low);
        }
    }
    private static void ValidateFonts(byte[] bytes, ThemeManifest manifest, BaseCatalog? catalog)
    {
        var root = Contract.Xml(bytes).Root!; Contract.Require(root.Name == "fonts", "font", "Expected fonts");
        foreach (var file in root.Descendants("file"))
        {
            Contract.PathName(file.Value);
            Contract.Require(!file.Value.Contains('/') && (File.Exists(Path.Combine(catalog?.Root ?? "", "fonts", file.Value)) ||
                manifest.FontDependencies.Any(f => f.File == file.Value)), "font", "Undeclared font: " + file.Value);
        }
        Contract.Require(root.Elements().All(n => n.Name.LocalName is "font" or "font_size") && root.Descendants().All(n => n.Name.LocalName is "font" or "file" or "os" or "font_size"), "font", "Unsupported font structure");
        foreach (var node in root.Descendants())
            foreach (var attribute in node.Attributes())
                if (node.Name == "font_size" && attribute.Name == "size" || node.Name == "file" && attribute.Name.LocalName is "size_delta" or "font_weight")
                {
                    var name = attribute.Name.LocalName;
                    var (min, max) = name == "size" ? (1d, 256d) : name == "size_delta" ? (-64d, 64d) : (1d, 1000d);
                    Contract.Require(double.TryParse(attribute.Value, System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out var number) && double.IsFinite(number) && number >= min && number <= max && (name != "font_weight" || number == Math.Truncate(number)), "font", "Invalid " + name);
                }
    }
    private static void ValidateImage(byte[] bytes, string path)
    {
        if (path.EndsWith(".png", StringComparison.OrdinalIgnoreCase))
        {
            Contract.Require(bytes.Length >= 33 && bytes.AsSpan(0, 8).SequenceEqual(new byte[] { 137, 80, 78, 71, 13, 10, 26, 10 }) && Encoding.ASCII.GetString(bytes, 12, 4) == "IHDR", "image", "Invalid PNG");
            var width = System.Buffers.Binary.BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(16, 4));
            var height = System.Buffers.Binary.BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(20, 4));
            Contract.Require(width > 0 && height > 0 && (ulong)width * height <= 16000000, "image", "Image exceeds 16 megapixels");
        }
        else throw new ThemeException("image", "Native dimension inspection required before accepting: " + path);
    }

    public static ValidatedTheme ReadArchive(string path, BaseCatalog? catalog = null)
    {
        Contract.Require(new FileInfo(path).Length <= Contract.ArchiveLimit, "size", "Archive exceeds 128 MiB");
        using var input = File.OpenRead(path); using var archive = new ZipArchive(input, ZipArchiveMode.Read);
        Contract.Require(archive.Entries.Count is > 0 and <= Contract.EntryLimit, "size", "Invalid archive entry count");
        var files = new Dictionary<string, byte[]>(StringComparer.Ordinal); var folded = new HashSet<string>(StringComparer.OrdinalIgnoreCase); long expanded = 0;
        foreach (var entry in archive.Entries)
        {
            Contract.PathName(entry.FullName);
            Contract.Require(folded.Add(entry.FullName), "collision", entry.FullName);
            Contract.Require(((entry.ExternalAttributes >> 16) & 0xF000) is 0 or 0x8000 && (entry.ExternalAttributes & 0x400) == 0, "link", entry.FullName);
            var limit = entry.FullName == "theme.json" ? Contract.ManifestLimit : Limit(Kind(entry.FullName));
            expanded = checked(expanded + entry.Length);
            Contract.Require(entry.Length >= 0 && entry.Length <= limit && expanded <= Contract.ExpandedLimit, "size", entry.FullName);
        }
        foreach (var entry in archive.Entries)
        {
            using var stream = entry.Open(); using var output = new MemoryStream(); var buffer = new byte[65536]; int count;
            while ((count = stream.Read(buffer)) != 0)
            { Contract.Require(output.Length + count <= entry.Length, "size", entry.FullName); output.Write(buffer, 0, count); }
            Contract.Require(output.Length == entry.Length, "size", entry.FullName); files.Add(entry.FullName, output.ToArray());
        }
        return Validate(files, catalog);
    }
    public static ValidatedTheme ReadRevision(string directory, BaseCatalog? catalog = null)
    {
        var manifestPath = Contract.Contained(directory, "theme.json");
        Contract.Require(new FileInfo(manifestPath).Length <= Contract.ManifestLimit, "size", "Manifest exceeds limit");
        var bytes = File.ReadAllBytes(manifestPath); var manifest = Manifest(bytes);
        var files = new Dictionary<string, byte[]>(StringComparer.Ordinal) { ["theme.json"] = bytes };
        foreach (var payload in manifest.Files)
        {
            var file = Contract.Contained(directory, payload.Path);
            Contract.Require(File.Exists(file) && new FileInfo(file).Length == payload.Size, "integrity", payload.Path);
            files.Add(payload.Path, File.ReadAllBytes(file));
        }
        var actual = EnumeratePlainFiles(directory).Select(f => Path.GetRelativePath(directory, f).Replace('\\', '/')).ToArray();
        Contract.Require(actual.Length == files.Count && actual.All(files.ContainsKey), "coverage", "Unlisted revision payload");
        return Validate(files, catalog);
    }
    private static IEnumerable<string> EnumeratePlainFiles(string directory)
    {
        foreach (var path in Directory.EnumerateFileSystemEntries(directory))
        {
            var attributes = File.GetAttributes(path); Contract.Require(!attributes.HasFlag(FileAttributes.ReparsePoint), "link", path);
            if (attributes.HasFlag(FileAttributes.Directory)) { foreach (var file in EnumeratePlainFiles(path)) yield return file; }
            else yield return path;
        }
    }
    public static ValidatedTheme Compile(string projectDirectory, ThemeProject project, BaseCatalog catalog)
    {
        ProjectStore.EnsureEditable(project);
        var dependencies = Contract.Bytes(project.BaseDependencies);
        var files = new Dictionary<string, byte[]>(StringComparer.Ordinal) { ["base-dependencies.json"] = dependencies };
        var colors = catalog.CompileColors(project.Edits.Colors, project.Edits.ColorLinks);
        if (colors.Count > 0) files.Add("viewer/colors.xml", Contract.XmlBytes(new XDocument(new XElement("colors", colors.OrderBy(c => c.Key, StringComparer.Ordinal).Select(c => new XElement("color", new XAttribute("name", c.Key), new XAttribute("value", c.Value)))))));
        foreach (var asset in project.Edits.Assets)
        {
            Contract.Require(asset.Kind == Kind(asset.PayloadPath), "kind", asset.PayloadPath);
            Contract.Require(files.TryAdd(asset.PayloadPath, File.ReadAllBytes(Contract.Contained(projectDirectory, asset.AssetPath))), "collision", asset.PayloadPath);
        }
        var fonts = files.TryGetValue("viewer/fonts/fonts.xml", out var fontBytes) ? Contract.Xml(fontBytes).Descendants("file").Select(f => f.Value).Distinct(StringComparer.Ordinal).Select(f =>
            new FontDependency(files.ContainsKey("viewer/fonts/" + f) ? "embedded" : "installed", f)).ToArray() : [];
        var capabilities = new List<string> { "viewer.overlay.v1" };
        if (files.ContainsKey("viewer/fonts/fonts.xml")) capabilities.Add("viewer.fonts.v1");
        if (files.Keys.Any(p => p.StartsWith("viewer/xui/", StringComparison.Ordinal))) capabilities.Add("viewer.xui.v1");
        var manifest = new ThemeManifest
        {
            FormatVersion = 1, ThemeId = project.ThemeId, PackageVersion = project.PackageVersion,
            DisplayName = project.DisplayName, Author = project.Author, Description = project.Description,
            BaseSkin = project.BaseSkin, BaseScheme = project.BaseScheme, AdapterId = "ansastorm-classic", AdapterVersion = 1,
            RequiredCapabilities = capabilities.ToArray(), BaseFingerprint = Contract.Hash(dependencies),
            Files = files.OrderBy(f => f.Key, StringComparer.Ordinal).Select(f => new Payload(f.Key, f.Value.LongLength, Contract.Hash(f.Value), Kind(f.Key))).ToArray(),
            FontDependencies = fonts, Extensions = project.Extensions
        };
        files.Add("theme.json", Contract.Bytes(manifest)); return Validate(files, catalog);
    }
    public static void Export(string path, ValidatedTheme theme)
    {
        using var buffer = new MemoryStream();
        using (var zip = new ZipArchive(buffer, ZipArchiveMode.Create, true))
            foreach (var file in theme.Files.OrderBy(f => f.Key, StringComparer.Ordinal))
            {
                var entry = zip.CreateEntry(file.Key, CompressionLevel.Optimal); entry.LastWriteTime = ZipEpoch; entry.ExternalAttributes = 0;
                using var stream = entry.Open(); stream.Write(file.Value);
            }
        Contract.Require(buffer.Length <= Contract.ArchiveLimit, "size", "Export exceeds 128 MiB");
        Contract.AtomicWrite(path, buffer.ToArray());
    }
    public static ThemeProject ImportProject(string directory, ValidatedTheme theme)
    {
        Contract.Require(!Directory.Exists(directory), "project_exists", "Choose a new project directory"); Directory.CreateDirectory(directory);
        var project = new ThemeProject
        {
            ThemeId = theme.Manifest.ThemeId, DisplayName = theme.Manifest.DisplayName, PackageVersion = theme.Manifest.PackageVersion,
            Author = theme.Manifest.Author, Description = theme.Manifest.Description, BaseSkin = theme.Manifest.BaseSkin, BaseScheme = theme.Manifest.BaseScheme,
            BaseDependencies = Contract.Read<DependencySet>(theme.Files["base-dependencies.json"]), Origin = "compiled-package", Extensions = theme.Manifest.Extensions
        };
        foreach (var payload in theme.Manifest.Files.Where(f => f.Path != "base-dependencies.json"))
        {
            var relative = "assets/" + payload.Sha256 + Path.GetExtension(payload.Path);
            Contract.AtomicWrite(Contract.Contained(directory, relative), theme.Files[payload.Path]);
            project.Edits.Assets.Add(new(payload.Path, relative, relative, payload.Kind));
        }
        ProjectStore.Save(directory, project); return project;
    }
}

public static class XuiPolicy
{
    private static readonly JsonDocument Rules = JsonDocument.Parse(typeof(XuiPolicy).Assembly.GetManifestResourceStream("Folderstorm.Theme.XuiRules")!);
    public static readonly HashSet<string> Files = ReadNames("paths");
    private static readonly HashSet<string> Attributes = ReadNames("editable_attributes");
    private static readonly JsonElement Values = Rules.RootElement.GetProperty("value_rules");
    private static HashSet<string> ReadNames(string name) => Rules.RootElement.GetProperty(name).EnumerateArray().Select(v => v.GetString()!).ToHashSet(StringComparer.Ordinal);
    public static void Validate(string path, byte[] bytes, BaseCatalog catalog)
    {
        Contract.Require(Files.Contains(path), "xui_path", path);
        var baseline = Contract.Xml(catalog.ResolveBytes(path)); var candidate = Contract.Xml(bytes);
        Compare(baseline.Root!, candidate.Root!, "/" + baseline.Root!.Name, catalog);
    }
    private static void Compare(XElement baseline, XElement candidate, string selector, BaseCatalog catalog)
    {
        Contract.Require(baseline.Name == candidate.Name && baseline.Elements().Count() == candidate.Elements().Count(), "xui_structure", selector);
        var attributes = baseline.Attributes().Select(a => a.Name).Union(candidate.Attributes().Select(a => a.Name));
        foreach (var name in attributes)
            if ((string?)baseline.Attribute(name) != (string?)candidate.Attribute(name))
            {
                Contract.Require(name.NamespaceName.Length == 0 && Attributes.Contains(name.LocalName), "xui_attribute", selector + "/@" + name);
                if (candidate.Attribute(name) is { } attribute) ValidateValue(name.LocalName, attribute.Value, catalog);
            }
        var baseText = string.Concat(baseline.Nodes().OfType<XText>().Select(n => n.Value)).Trim();
        var newText = string.Concat(candidate.Nodes().OfType<XText>().Select(n => n.Value)).Trim();
        Contract.Require(baseText == newText || baseline.Name.LocalName == "text", "xui_structure", selector + "/text()");
        Contract.Require(newText.Length <= Values.GetProperty("text_limit").GetInt32(), "xui_value", "Text is too long");
        foreach (var (left, right) in baseline.Elements().Zip(candidate.Elements())) Compare(left, right, selector + "/" + left.Name + "[" + (string?)left.Attribute("name") + "]", catalog);
    }
    private static void ValidateValue(string name, string value, BaseCatalog catalog)
    {
        bool In(string group) => Values.GetProperty(group).EnumerateArray().Any(v => v.GetString() == name);
        Contract.Require(value.Length <= Values.GetProperty("text_limit").GetInt32(), "xui_value", name);
        if (In("nonnegative_integer") || In("signed_integer"))
        {
            var limit = Values.GetProperty("integer_limit").GetInt32();
            Contract.Require(int.TryParse(value, System.Globalization.NumberStyles.Integer, System.Globalization.CultureInfo.InvariantCulture, out var number) && number <= limit && number >= (In("signed_integer") ? -limit : 0), "xui_value", name);
        }
        else if (In("color") && !catalog.Colors.ContainsKey(value))
        {
            try { Contract.Rgba(value); }
            catch (ThemeException) { throw new ThemeException("reference", "Unknown color: " + value); }
        }
        else if (In("image")) Contract.Require(catalog.Textures.ContainsKey(value), "reference", "Unknown image: " + value);
    }
}
