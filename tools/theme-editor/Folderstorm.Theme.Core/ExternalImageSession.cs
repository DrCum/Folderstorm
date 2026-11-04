// SPDX-License-Identifier: LGPL-2.1-or-later
using System.Buffers.Binary;
using System.Text.Json;

namespace Folderstorm.Theme;

public sealed record ExternalImageRecord(int Version, string ProjectDirectory, string ProjectId, string LogicalId,
    string SourceFingerprint, string ImportedHash, int Width, int Height);

public sealed class ExternalImageSession
{
    public const int FileLimit = 32 * 1024 * 1024;
    private readonly string root, folder;
    private ExternalImageRecord record;
    public string WorkingDirectory => Contract.Contained(root, folder);
    public string WorkingPath => Contract.Contained(root, folder + "/image.png");
    public string LogicalId => record.LogicalId;
    public string? Warning { get; private set; }
    private ExternalImageSession(string root, string folder, ExternalImageRecord record) => (this.root, this.folder, this.record) = (root, folder, record);

    private static string DirectoryName(string path) => Path.TrimEndingDirectorySeparator(Path.GetFullPath(path));
    public bool Matches(string directory, ThemeProject project, string logicalId) =>
        DirectoryName(directory).Equals(record.ProjectDirectory, StringComparison.OrdinalIgnoreCase) && project.ProjectId == record.ProjectId && logicalId == record.LogicalId;
    private static void Ready(ThemeProject project, BaseCatalog catalog)
    {
        ProjectStore.EnsureEditable(project);
        Contract.Require(project.BaseSkin == catalog.Skin && project.BaseScheme == catalog.Scheme, "base", "External edit base differs from the project");
    }
    private static string Fingerprint(string directory, ThemeProject project, BaseCatalog catalog, string name)
    {
        var texture = ImageProject.Resolve(directory, project, catalog, name);
        var file = texture.Attributes.GetValueOrDefault("file_name");
        var asset = project.Edits.Assets.LastOrDefault(item => item.PayloadPath == "viewer/textures/" + file);
        return Contract.Hash(Contract.Bytes(new { texture.Attributes, Bytes = Contract.Hash(ImageProject.Read(directory, project, catalog, name)), Asset = asset,
            Recipe = project.Edits.Recipes.GetValueOrDefault(name) }));
    }
    public bool IsCurrent(string directory, ThemeProject project, BaseCatalog catalog)
        => Matches(directory, project, LogicalId) && record.SourceFingerprint == Fingerprint(directory, project, catalog, LogicalId);
    private void SaveRecord() => Contract.AtomicWrite(Contract.Contained(root, folder + "/session.json"), Contract.Bytes(record));

    public static ExternalImageSession Create(string directory, ThemeProject project, BaseCatalog catalog, string name, string workingRoot,
        Func<byte[], string, byte[]> encodePng)
    {
        Ready(project, catalog); directory = DirectoryName(directory); workingRoot = DirectoryName(workingRoot);
        Contract.Require(!workingRoot.Equals(directory, StringComparison.OrdinalIgnoreCase) && !workingRoot.StartsWith(directory.TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase),
            "external_scope", "Keep external working files outside the project");
        var texture = ImageProject.Resolve(directory, project, catalog, name);
        Contract.Require(texture.Attributes.TryGetValue("file_name", out var file), "reference", "Image has no filename");
        var fingerprint = Fingerprint(directory, project, catalog, name); var bytes = ImageProject.Read(directory, project, catalog, name);
        var png = file.EndsWith(".png", StringComparison.OrdinalIgnoreCase) ? bytes : encodePng(bytes, file);
        PackageStore.ValidateImage(png, "image.png"); Contract.Require(png.Length <= FileLimit, "size", "Working PNG exceeds 32 MiB");
        var width = checked((int)BinaryPrimitives.ReadUInt32BigEndian(png.AsSpan(16, 4))); var height = checked((int)BinaryPrimitives.ReadUInt32BigEndian(png.AsSpan(20, 4)));
        Contract.Require(fingerprint == Fingerprint(directory, project, catalog, name), "external_conflict", "Image changed while creating the working copy");
        Directory.CreateDirectory(workingRoot); var folder = Guid.NewGuid().ToString("N");
        Directory.CreateDirectory(Contract.Contained(workingRoot, folder));
        var session = new ExternalImageSession(workingRoot, folder, new(1, directory, project.ProjectId, name, fingerprint, Contract.Hash(png), width, height));
        Contract.AtomicWrite(session.WorkingPath, png); session.SaveRecord(); return session;
    }

    public static ExternalImageSession? FindPrevious(string directory, ThemeProject project, string name, string workingRoot)
    {
        if (!Directory.Exists(workingRoot)) return null;
        ExternalImageSession? latest = null; var latestTime = DateTime.MinValue;
        foreach (var candidate in Directory.EnumerateDirectories(workingRoot).Take(1000))
        {
            var folder = Path.GetFileName(candidate); if (!Guid.TryParseExact(folder, "N", out _)) continue;
            try
            {
                var path = Contract.Contained(workingRoot, folder + "/session.json");
                if (!File.Exists(path) || new FileInfo(path).Length > 65536) continue;
                var record = Contract.Read<ExternalImageRecord>(File.ReadAllBytes(path));
                if (record.Version != 1 || record.SourceFingerprint == null || record.ImportedHash == null ||
                    !Contract.IsHash(record.SourceFingerprint) || !Contract.IsHash(record.ImportedHash) ||
                    record.Width is <= 0 or > 16384 || record.Height is <= 0 or > 16384 || (long)record.Width * record.Height > RasterImage.PixelLimit) continue;
                var session = new ExternalImageSession(DirectoryName(workingRoot), folder, record);
                if (session.Matches(directory, project, name) && File.Exists(session.WorkingPath))
                {
                    var created = Directory.GetCreationTimeUtc(candidate);
                    if (latest == null || created > latestTime) { latest = session; latestTime = created; }
                }
            }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException or ThemeException or ArgumentException) { }
        }
        return latest;
    }
    private byte[] ReadWorking()
    {
        var path = WorkingPath;
        Contract.Require(File.Exists(path), "external_missing", "Working copy is missing. Save back to image.png or open a new copy.");
        using var input = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
        var length = input.Length; var stamp = File.GetLastWriteTimeUtc(path);
        Contract.Require(length is > 0 and <= FileLimit, "size", "Working PNG must be nonempty and at most 32 MiB");
        var bytes = new byte[(int)length]; input.ReadExactly(bytes);
        Contract.Require(input.Length == length && File.Exists(path) && File.GetLastWriteTimeUtc(path) == stamp,
            "external_busy", "Working copy is still being saved. Try Reload again.");
        return bytes;
    }
    public bool HasChanges => Contract.Hash(ReadWorking()) != record.ImportedHash;
    public FileSystemWatcher Watch(Action changed)
    {
        _ = WorkingPath;
        var watcher = new FileSystemWatcher(WorkingDirectory, "image.png") { NotifyFilter = NotifyFilters.LastWrite | NotifyFilters.Size | NotifyFilters.FileName };
        watcher.Changed += (_, _) => changed(); watcher.Created += (_, _) => changed(); watcher.Deleted += (_, _) => changed(); watcher.Renamed += (_, _) => changed();
        watcher.EnableRaisingEvents = true; return watcher;
    }
    public bool Reload(string directory, ThemeProject project, BaseCatalog catalog, Action<byte[]> inspectPng)
    {
        Ready(project, catalog);
        Contract.Require(Matches(directory, project, LogicalId), "external_scope", "Working copy belongs to another project");
        Contract.Require(IsCurrent(directory, project, catalog), "external_conflict", "Image changed in this project. Open a new working copy; the old file is kept.");
        var png = ReadWorking(); var hash = Contract.Hash(png); if (hash == record.ImportedHash) return false;
        PackageStore.ValidateImage(png, "image.png"); inspectPng(png);
        var width = checked((int)BinaryPrimitives.ReadUInt32BigEndian(png.AsSpan(16, 4))); var height = checked((int)BinaryPrimitives.ReadUInt32BigEndian(png.AsSpan(20, 4)));
        var texture = ImageProject.Resolve(directory, project, catalog, LogicalId);
        var geometry = TextureGeometry.FromAttributes(record.Width, record.Height, texture.Attributes).Resize(width, height);
        var recipe = new Recipe("external-edit", new() { ["sha256"] = JsonSerializer.SerializeToElement(hash) });
        ImageProject.SetPng(directory, project, catalog, LogicalId, png, geometry, recipe);
        record = record with { SourceFingerprint = Fingerprint(directory, project, catalog, LogicalId), ImportedHash = hash, Width = width, Height = height };
        Warning = null;
        try { SaveRecord(); }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException) { Warning = "Image reloaded; local working-copy details could not be saved."; }
        return true;
    }
}
