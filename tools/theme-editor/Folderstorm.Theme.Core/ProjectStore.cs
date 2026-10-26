// SPDX-License-Identifier: LGPL-2.1-or-later
using System.Text.Json;

namespace Folderstorm.Theme;

public sealed record ProjectDocument(ThemeProject? Project, int SourceVersion, byte[] OriginalBytes, string[] ReadOnlyReasons)
{
    public bool CanEdit => Project != null && ReadOnlyReasons.Length == 0;
    public bool WasMigrated => CanEdit && SourceVersion < ProjectStore.CurrentVersion;
}

public static class ProjectStore
{
    public const int CurrentVersion = 2;
    public const string FileName = "project.fsthemeproj";

    public static ThemeProject Create(BaseCatalog catalog, string name) => new()
    { DisplayName = name, BaseSkin = catalog.Skin, BaseScheme = catalog.Scheme, BaseDependencies = catalog.Fingerprint() };

    public static ProjectDocument OpenDocument(string directory)
    {
        var bytes = ReadBounded(Contract.Contained(directory, FileName));
        using var json = Contract.Parse(bytes);
        Contract.Require(json.RootElement.ValueKind == JsonValueKind.Object &&
            json.RootElement.TryGetProperty("project_version", out var value) && value.ValueKind == JsonValueKind.Number && value.TryGetInt32(out var version) && version > 0,
            "project_version", "A positive project version is required");
        version = json.RootElement.GetProperty("project_version").GetInt32();
        if (version > CurrentVersion) return new(null, version, bytes, ["This project needs a newer editor."]);
        ThemeProject project;
        try { project = Contract.Read<ThemeProject>(bytes) with { ProjectVersion = CurrentVersion }; }
        catch (ThemeException error) when (error.Code == "schema")
        { return new(null, version, bytes, ["This project contains unsupported structure: " + error.Message]); }
        var reasons = ReadOnlyReasons(project);
        if (reasons.Length == 0) ValidateAssets(directory, project);
        return new(project, version, bytes, reasons);
    }

    public static ThemeProject Open(string directory)
    {
        var document = OpenDocument(directory);
        Contract.Require(document.CanEdit, "project_readonly", string.Join(" ", document.ReadOnlyReasons));
        return document.Project!;
    }

    public static void EnsureEditable(ThemeProject project)
    {
        Contract.Require(project.ProjectVersion == CurrentVersion, "project_version", "Migrate this project with OpenDocument before editing");
        var reasons = ReadOnlyReasons(project);
        Contract.Require(reasons.Length == 0, "project_readonly", string.Join(" ", reasons));
    }

    public static void Save(string directory, ThemeProject project)
    {
        EnsureEditable(project);
        ValidateAssets(directory, project);
        var bytes = Contract.Bytes(project);
        Contract.Require(bytes.Length <= Contract.ManifestLimit, "size", "Project exceeds 4 MiB");
        var previous = Path.Combine(directory, FileName);
        byte[]? previousBytes = null;
        if (File.Exists(previous))
        {
            var existing = OpenDocument(directory);
            Contract.Require(existing.CanEdit, "project_readonly", "The destination contains a project this editor cannot safely overwrite");
            previousBytes = existing.OriginalBytes;
        }
        Directory.CreateDirectory(directory);
        previous = Contract.Contained(directory, FileName);
        if (previousBytes != null)
            Contract.AtomicWrite(Contract.Contained(directory, "checkpoints/" + Contract.Hash(previousBytes) + ".fsthemeproj"), previousBytes);
        Contract.AtomicWrite(previous, bytes);
    }

    private static byte[] ReadBounded(string path)
    {
        using var input = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read | FileShare.Delete);
        Contract.Require(input.Length <= Contract.ManifestLimit, "size", "Project exceeds 4 MiB");
        var bytes = new byte[(int)input.Length]; input.ReadExactly(bytes);
        Contract.Require(input.ReadByte() == -1, "size", "Project changed while reading");
        return bytes;
    }

    private static IEnumerable<ProjectEdits> Snapshots(ThemeProject project)
    {
        Contract.Require(project.Edits != null && project.Undo != null && project.Redo != null, "schema", "Missing project edits or history");
        yield return project.Edits;
        foreach (var checkpoint in project.Undo.Concat(project.Redo))
        {
            Contract.Require(checkpoint != null && checkpoint.Edits != null && checkpoint.Label != null, "schema", "Invalid history checkpoint");
            if (checkpoint.Metadata is { } metadata)
                Contract.Require(metadata.DisplayName != null && metadata.PackageVersion != null && metadata.Author != null && metadata.Description != null && metadata.Extensions != null,
                    "schema", "Invalid metadata checkpoint");
            yield return checkpoint.Edits;
        }
    }

    private static string[] ReadOnlyReasons(ThemeProject project)
    {
        var reasons = new HashSet<string>(StringComparer.Ordinal);
        foreach (var edits in Snapshots(project))
        {
            Contract.Require(edits.Colors != null && edits.ColorLinks != null && edits.Assets != null && edits.Recipes != null, "schema", "Invalid edit collections");
            if (edits.UnknownMetadata?.Count > 0) reasons.Add("Unsupported edit fields are preserved read-only.");
            foreach (var asset in edits.Assets)
            {
                Contract.Require(asset != null, "schema", "Null project asset");
                if (asset.UnknownMetadata?.Count > 0) reasons.Add("Unsupported asset fields are preserved read-only.");
            }
            foreach (var recipe in edits.Recipes.Values)
            {
                Contract.Require(recipe != null && recipe.Operation != null && recipe.Parameters != null, "schema", "Invalid recipe");
                reasons.UnionWith(RecipeHistory.ReadOnlyReasons(recipe));
            }
        }
        if (project.Undo.Concat(project.Redo).Any(checkpoint => checkpoint.UnknownMetadata?.Count > 0))
            reasons.Add("Unsupported history fields are preserved read-only.");
        return reasons.Order(StringComparer.Ordinal).ToArray();
    }

    private static void ValidateAssets(string directory, ThemeProject project)
    {
        foreach (var edits in Snapshots(project))
        {
            foreach (var asset in edits.Assets)
            {
                Contract.Require(asset.PayloadPath != null && asset.AssetPath != null && asset.OriginalPath != null && asset.Kind != null, "schema", "Invalid project asset");
                Contract.PathName(asset.PayloadPath);
                Contract.Require(File.Exists(Contract.Contained(directory, asset.AssetPath)) && File.Exists(Contract.Contained(directory, asset.OriginalPath)), "project_asset", asset.AssetPath);
            }
            foreach (var (name, value) in edits.Colors)
            {
                Contract.Require(!string.IsNullOrEmpty(name) && value != null, "color", "A color requires a name and value");
                Contract.Require(!edits.ColorLinks.ContainsKey(name), "color", "A color cannot be both a literal and a link: " + name);
                Contract.Rgba(value);
            }
            foreach (var (name, target) in edits.ColorLinks)
                Contract.Require(!string.IsNullOrEmpty(name) && !string.IsNullOrEmpty(target), "reference", "Color links require a source and target");
        }
    }

    public static ProjectAsset ImportAsset(string directory, string input, string payload, string kind)
    {
        Contract.PathName(payload); Directory.CreateDirectory(directory);
        Contract.Require(new FileInfo(input).Length <= 64L * 1024 * 1024, "size", "Asset exceeds 64 MiB");
        var bytes = File.ReadAllBytes(input); var hash = Contract.Hash(bytes);
        var relative = "assets/" + hash + Path.GetExtension(input).ToLowerInvariant();
        var path = Contract.Contained(directory, relative);
        if (!File.Exists(path)) Contract.AtomicWrite(path, bytes);
        return new(payload, relative, relative, kind);
    }
}
