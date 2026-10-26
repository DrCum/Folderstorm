// SPDX-License-Identifier: LGPL-2.1-or-later
using System.Globalization;
using System.Diagnostics.CodeAnalysis;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;
using System.Xml;
using System.Xml.Linq;

namespace Folderstorm.Theme;

public sealed class ThemeException(string code, string detail) : Exception($"{code}: {detail}")
{
    public string Code { get; } = code;
}

public static partial class Contract
{
    public const int ManifestLimit = 4 * 1024 * 1024;
    public const long ArchiveLimit = 128L * 1024 * 1024, ExpandedLimit = 512L * 1024 * 1024;
    public const int EntryLimit = 10000;
    public static readonly string[] Capabilities = ["viewer.overlay.v1", "viewer.preview.v1", "viewer.fonts.v1", "viewer.xui.v1", "host.palette.v1"];
    public static readonly JsonSerializerOptions JsonOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower,
        UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow,
        MaxDepth = 64,
        WriteIndented = true
    };

    [GeneratedRegex("^[0-9a-f]{64}$", RegexOptions.CultureInvariant)]
    private static partial Regex HashPattern();
    [GeneratedRegex("^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(?:-[0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*)?(?:\\+[0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*)?$", RegexOptions.CultureInvariant)]
    private static partial Regex VersionPattern();

    public static void Require([DoesNotReturnIf(false)] bool condition, string code, string detail)
    {
        if (!condition) throw new ThemeException(code, detail);
    }
    public static bool IsHash(string value) => HashPattern().IsMatch(value);
    public static bool IsVersion(string value)
    {
        if (value.Length > 128 || !VersionPattern().IsMatch(value)) return false;
        var release = value.Split('+')[0]; var dash = release.IndexOf('-');
        return dash < 0 || release[(dash + 1)..].Split('.').All(p => !p.All(char.IsAsciiDigit) || p.Length == 1 || p[0] != '0');
    }
    public static string Hash(ReadOnlySpan<byte> bytes) => Convert.ToHexStringLower(SHA256.HashData(bytes));
    public static string HashFile(string path)
    {
        using var stream = File.OpenRead(path);
        return Convert.ToHexStringLower(SHA256.HashData(stream));
    }

    public static JsonDocument Parse(byte[] bytes)
    {
        Require(bytes.Length <= ManifestLimit, "size", "JSON exceeds 4 MiB");
        try
        {
            var reader = new Utf8JsonReader(bytes, new JsonReaderOptions { MaxDepth = 64 });
            var objects = new Stack<HashSet<string>>();
            while (reader.Read())
            {
                if (reader.TokenType == JsonTokenType.StartObject) objects.Push(new(StringComparer.Ordinal));
                else if (reader.TokenType == JsonTokenType.EndObject) objects.Pop();
                else if (reader.TokenType == JsonTokenType.PropertyName)
                    Require(objects.Peek().Add(reader.GetString()!), "duplicate_key", reader.GetString()!);
            }
            return JsonDocument.Parse(bytes, new JsonDocumentOptions { MaxDepth = 64 });
        }
        catch (JsonException e) { throw new ThemeException("json", e.Message); }
    }
    public static T Read<T>(byte[] bytes)
    {
        using var document = Parse(bytes);
        try { return document.Deserialize<T>(JsonOptions) ?? throw new ThemeException("json", "Null document"); }
        catch (JsonException e) { throw new ThemeException("schema", e.Message); }
    }
    public static byte[] Bytes<T>(T value)
    {
        using var document = JsonSerializer.SerializeToDocument(value, JsonOptions);
        using var output = new MemoryStream();
        using (var writer = new Utf8JsonWriter(output, new JsonWriterOptions { Indented = true })) WriteSorted(writer, document.RootElement);
        output.WriteByte((byte)'\n');
        return output.ToArray();
    }
    private static void WriteSorted(Utf8JsonWriter writer, JsonElement value)
    {
        if (value.ValueKind == JsonValueKind.Object)
        {
            writer.WriteStartObject();
            foreach (var property in value.EnumerateObject().OrderBy(p => p.Name, StringComparer.Ordinal))
            { writer.WritePropertyName(property.Name); WriteSorted(writer, property.Value); }
            writer.WriteEndObject();
        }
        else if (value.ValueKind == JsonValueKind.Array)
        { writer.WriteStartArray(); foreach (var item in value.EnumerateArray()) WriteSorted(writer, item); writer.WriteEndArray(); }
        else value.WriteTo(writer);
    }
    public static void AtomicWrite(string path, byte[] bytes)
    {
        path = Path.GetFullPath(path);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var temporary = path + ".tmp-" + Guid.NewGuid().ToString("N");
        try
        {
            using (var output = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None))
            { output.Write(bytes); output.Flush(true); }
            File.Move(temporary, path, true);
        }
        finally { if (File.Exists(temporary)) File.Delete(temporary); }
    }
    public static void PathName(string path)
    {
        Require(path.Length is > 0 and <= 240 && !path.Contains('\\') && !path.StartsWith('/'), "path", path);
        foreach (var part in path.Split('/'))
        {
            var stem = part.Split('.')[0].ToUpperInvariant();
            Require(part.Length > 0 && part is not "." and not ".." && !part.EndsWith('.') && !part.EndsWith(' ') &&
                !part.Any(c => c < 32 || "<>:\"|?*".Contains(c)) && stem is not "CON" and not "PRN" and not "AUX" and not "NUL" &&
                !(stem.Length == 4 && (stem.StartsWith("COM", StringComparison.Ordinal) || stem.StartsWith("LPT", StringComparison.Ordinal)) && "123456789¹²³".Contains(stem[3])), "path", path);
        }
    }
    public static string Contained(string root, string relative)
    {
        PathName(relative);
        root = Path.GetFullPath(root);
        var path = Path.GetFullPath(Path.Combine(root, relative));
        Require(path.StartsWith(root.TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase), "path", relative);
        var current = root;
        Require(!File.GetAttributes(root).HasFlag(FileAttributes.ReparsePoint), "link", root);
        foreach (var part in relative.Split('/'))
        {
            current = Path.Combine(current, part);
            if (File.Exists(current) || Directory.Exists(current))
                Require(!File.GetAttributes(current).HasFlag(FileAttributes.ReparsePoint), "link", relative);
        }
        return path;
    }
    public static XDocument Xml(byte[] bytes)
    {
        Require(bytes.Length <= ManifestLimit, "size", "XML exceeds 4 MiB");
        try
        {
            using var stream = new MemoryStream(bytes);
            using var reader = XmlReader.Create(stream, new XmlReaderSettings
            { DtdProcessing = DtdProcessing.Prohibit, XmlResolver = null, MaxCharactersInDocument = ManifestLimit });
            return XDocument.Load(reader, LoadOptions.PreserveWhitespace);
        }
        catch (XmlException e) { throw new ThemeException("xml", e.Message); }
    }
    public static byte[] XmlBytes(XDocument document) => Encoding.UTF8.GetBytes(document.ToString(SaveOptions.DisableFormatting) + "\n");
    public static double[] Rgba(string value)
    {
        var parts = value.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
        Require(parts.Length == 4, "color", value);
        var rgba = parts.Select(p => double.TryParse(p, NumberStyles.Float, CultureInfo.InvariantCulture, out var number) ? number : double.NaN).ToArray();
        Require(rgba.All(n => double.IsFinite(n) && n is >= 0 and <= 1), "color", value);
        return rgba;
    }
    public static string ColorText(IEnumerable<double> rgba) => string.Join(' ', rgba.Select(v => v.ToString("0.######", CultureInfo.InvariantCulture)));
}

public sealed record Payload(string Path, long Size, string Sha256, string Kind);
public sealed record FontDependency(string Kind, string File, int Face = 0);
public sealed record ThemeManifest
{
    public required int FormatVersion { get; init; }
    public required string ThemeId { get; init; }
    public required string PackageVersion { get; init; }
    public required string DisplayName { get; init; }
    public required string BaseSkin { get; init; }
    public required string BaseScheme { get; init; }
    public required string AdapterId { get; init; }
    public required int AdapterVersion { get; init; }
    public required string[] RequiredCapabilities { get; init; }
    public required string BaseFingerprint { get; init; }
    public required Payload[] Files { get; init; }
    public required FontDependency[] FontDependencies { get; init; }
    public string Author { get; init; } = "";
    public string Description { get; init; } = "";
    public Dictionary<string, JsonElement> Extensions { get; init; } = new(StringComparer.Ordinal);
}

public sealed record Dependency(string Path, bool Present, long Size, string Sha256, string LogicalId, int Layer);
public sealed record DependencySet(int FormatVersion, string BaseSkin, string BaseScheme, string[] Roots, Dependency[] Files);
public sealed record ColorDefinition(string Name, string? Value, string? Reference, string Source);
public sealed record TextureDefinition(string Name, Dictionary<string, string> Attributes, string? Source);
public sealed record Scheme(string Id, string Name);
public sealed record ProjectAsset(string PayloadPath, string AssetPath, string OriginalPath, string Kind)
{
    [JsonExtensionData] public Dictionary<string, JsonElement>? UnknownMetadata { get; init; }
}
public sealed record Recipe(string Operation, Dictionary<string, JsonElement> Parameters)
{
    [JsonExtensionData] public Dictionary<string, JsonElement>? UnknownMetadata { get; init; }
}
public sealed record ProjectEdits
{
    public Dictionary<string, string> Colors { get; init; } = new(StringComparer.Ordinal);
    public Dictionary<string, string> ColorLinks { get; init; } = new(StringComparer.Ordinal);
    public List<ProjectAsset> Assets { get; init; } = [];
    public Dictionary<string, Recipe> Recipes { get; init; } = new(StringComparer.Ordinal);
    public string? FontConfiguration { get; set; }
    [JsonExtensionData] public Dictionary<string, JsonElement>? UnknownMetadata { get; init; }
}
public sealed record ProjectMetadata(string DisplayName, string PackageVersion, string Author, string Description,
    Dictionary<string, JsonElement> Extensions, Dictionary<string, JsonElement>? UnknownMetadata);
public sealed record ProjectCheckpoint(string Label, ProjectEdits Edits, ProjectMetadata? Metadata = null)
{
    [JsonExtensionData] public Dictionary<string, JsonElement>? UnknownMetadata { get; init; }
}
public sealed record ThemeProject
{
    public int ProjectVersion { get; init; } = ProjectStore.CurrentVersion;
    public string ProjectId { get; init; } = Guid.NewGuid().ToString("D");
    public string ThemeId { get; init; } = Guid.NewGuid().ToString("D");
    public string DisplayName { get; set; } = "Untitled theme";
    public string PackageVersion { get; set; } = "1.0.0";
    public string Author { get; set; } = "";
    public string Description { get; set; } = "";
    public string BaseSkin { get; init; } = "ansastorm";
    public string BaseScheme { get; init; } = "folderstorm";
    public required DependencySet BaseDependencies { get; init; }
    public ProjectEdits Edits { get; set; } = new();
    public List<ProjectCheckpoint> Undo { get; init; } = [];
    public List<ProjectCheckpoint> Redo { get; init; } = [];
    public string Origin { get; init; } = "authored";
    public Dictionary<string, JsonElement> Extensions { get; init; } = new(StringComparer.Ordinal);
    [JsonExtensionData] public Dictionary<string, JsonElement>? UnknownMetadata { get; set; }

    public void Checkpoint(string label)
    {
        Undo.Add(new(label, Contract.Read<ProjectEdits>(Contract.Bytes(Edits)), CaptureMetadata()));
        Redo.Clear();
    }
    public bool UndoEdit()
    {
        if (Undo.Count == 0) return false;
        var checkpoint = Undo[^1]; Undo.RemoveAt(Undo.Count - 1);
        Redo.Add(new(checkpoint.Label, Edits, CaptureMetadata())); Edits = checkpoint.Edits; RestoreMetadata(checkpoint.Metadata); return true;
    }
    public bool RedoEdit()
    {
        if (Redo.Count == 0) return false;
        var checkpoint = Redo[^1]; Redo.RemoveAt(Redo.Count - 1);
        Undo.Add(new(checkpoint.Label, Edits, CaptureMetadata())); Edits = checkpoint.Edits; RestoreMetadata(checkpoint.Metadata); return true;
    }
    private ProjectMetadata CaptureMetadata() => Contract.Read<ProjectMetadata>(Contract.Bytes(new ProjectMetadata(DisplayName, PackageVersion, Author, Description, Extensions, UnknownMetadata)));
    private void RestoreMetadata(ProjectMetadata? metadata)
    {
        if (metadata == null) return; // Version-1 checkpoints did not capture metadata.
        DisplayName = metadata.DisplayName; PackageVersion = metadata.PackageVersion; Author = metadata.Author; Description = metadata.Description;
        Extensions.Clear(); foreach (var entry in metadata.Extensions) Extensions.Add(entry.Key, entry.Value.Clone());
        UnknownMetadata = metadata.UnknownMetadata?.ToDictionary(entry => entry.Key, entry => entry.Value.Clone(), StringComparer.Ordinal);
    }
}
