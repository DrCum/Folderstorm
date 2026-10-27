// SPDX-License-Identifier: LGPL-2.1-or-later
namespace Folderstorm.Theme;

public sealed record LibraryEntry(string Root, string ThemeId, string Revision, string DisplayName, ThemeManifest? Header, string HeaderHash, string? Error)
{
    public bool HasPreview => Header?.Files.Any(file => file.Path == "preview.png") == true;
}
public sealed record LibraryScan(LibraryEntry[] Entries, bool Truncated, string[] Warnings);

// Listing reads bounded headers only. Full validation is required before preview/import.
public static class LibraryCatalog
{
    public static LibraryScan Scan(string root, int limit = 512)
    {
        Contract.Require(limit is > 0 and <= 4096, "library_limit", "Choose1..4096 library entries");
        root = Path.GetFullPath(root); var entries = new List<LibraryEntry>(); var warnings = new List<string>(); var truncated = false;
        if (!Directory.Exists(root)) return new([], false, File.Exists(root) ? ["The library path is a file."] : []);
        try
        {
            Contract.Require(!File.GetAttributes(root).HasFlag(FileAttributes.ReparsePoint), "link", root);
            var inspected = 0; var inspectedRevisions = 0; long headerBytes = 0;
            foreach (var theme in Directory.EnumerateDirectories(root).Take(limit + 1))
            {
                if (++inspected > limit) { truncated = true; break; }
                var id = Path.GetFileName(theme); if (!CanonicalId(id)) continue;
                string[] revisions;
                try { Contract.Contained(root, id); revisions = Directory.EnumerateDirectories(theme).Take(limit - inspectedRevisions + 1).ToArray(); }
                catch (Exception error) when (ReadableFailure(error)) { warnings.Add(error.Message); continue; }
                foreach (var revisionPath in revisions)
                {
                    if (++inspectedRevisions > limit || headerBytes >= 32L * 1024 * 1024) { truncated = true; break; }
                    var revision = Path.GetFileName(revisionPath); if (!Contract.IsHash(revision)) continue;
                    try
                    {
                        var directory = Contract.Contained(root, id + "/" + revision); var manifestPath = Contract.Contained(directory, "theme.json");
                        var length = new FileInfo(manifestPath).Length;
                        if (length <= Contract.ManifestLimit && length > 32L * 1024 * 1024 - headerBytes) { truncated = true; break; }
                        var bytes = ReadBounded(manifestPath, Contract.ManifestLimit); headerBytes += bytes.Length;
                        var header = PackageStore.Manifest(bytes); Contract.Require(header.ThemeId == id, "library_identity", "Manifest theme ID differs from its folder");
                        entries.Add(new(root, id, revision, header.DisplayName, header, Contract.Hash(bytes), null));
                    }
                    catch (Exception error) when (ReadableFailure(error)) { entries.Add(new(root, id, revision, id, null, "", error.Message)); }
                }
                if (truncated) break;
            }
        }
        catch (Exception error) when (ReadableFailure(error)) { warnings.Add(error.Message); }
        return new(entries.OrderBy(entry => entry.DisplayName, StringComparer.OrdinalIgnoreCase).ThenBy(entry => entry.Revision, StringComparer.Ordinal).ToArray(), truncated, warnings.ToArray());
    }
    private static bool CanonicalId(string id) => Guid.TryParseExact(id, "D", out var value) && value.ToString("D") == id;
    private static bool ReadableFailure(Exception error) => error is ThemeException or IOException or UnauthorizedAccessException;
    private static (string Directory, ThemeManifest Header) Resolve(LibraryEntry entry)
    {
        Contract.Require(CanonicalId(entry.ThemeId) && Contract.IsHash(entry.Revision), "library_identity", "Invalid library identity");
        Contract.Require(entry.Error == null && entry.Header != null, "library_entry", entry.Error ?? "Unsupported library entry");
        var path = Contract.Contained(entry.Root, entry.ThemeId + "/" + entry.Revision);
        var bytes = ReadBounded(Contract.Contained(path, "theme.json"), Contract.ManifestLimit);
        Contract.Require(Contract.Hash(bytes) == entry.HeaderHash, "library_changed", "Library entry changed. Refresh before opening it.");
        var header = PackageStore.Manifest(bytes); Contract.Require(header.ThemeId == entry.ThemeId, "library_identity", "Manifest theme ID differs from its folder");
        return (path, header);
    }
    public static byte[]? ReadPreview(LibraryEntry entry)
    {
        var (path, header) = Resolve(entry);
        var payload = header.Files.SingleOrDefault(file => file.Path == "preview.png");
        if (payload == null) return null;
        var bytes = ReadBounded(Contract.Contained(path, "preview.png"), 32L * 1024 * 1024);
        Contract.Require(bytes.LongLength == payload.Size && Contract.Hash(bytes) == payload.Sha256, "integrity", "Preview differs from the library manifest");
        PackageStore.ValidateImage(bytes, "preview.png"); return bytes;
    }
    public static ValidatedTheme Validate(LibraryEntry entry, BaseCatalog catalog)
    {
        var (path, _) = Resolve(entry); BoundTree(path);
        var theme = PackageStore.ReadRevision(path, catalog);
        Contract.Require(theme.Manifest.ThemeId == entry.ThemeId && theme.Revision == entry.Revision, "library_identity", "Revision content differs from its folder");
        return theme;
    }
    private static void BoundTree(string root)
    {
        var pending = new Stack<(string Path, int Depth)>(); pending.Push((root, 0)); var count = 0; long size = 0;
        while (pending.TryPop(out var directory))
            foreach (var path in Directory.EnumerateFileSystemEntries(directory.Path))
            {
                Contract.Require(++count <= Contract.EntryLimit * 2, "size", "Library revision has too many entries");
                var attributes = File.GetAttributes(path); Contract.Require(!attributes.HasFlag(FileAttributes.ReparsePoint), "link", path);
                if (attributes.HasFlag(FileAttributes.Directory))
                { Contract.Require(directory.Depth < 32, "size", "Library revision folders are too deep"); pending.Push((path, directory.Depth + 1)); }
                else { var length = new FileInfo(path).Length; Contract.Require(length <= Contract.ExpandedLimit + Contract.ManifestLimit - size, "size", "Library revision exceeds the byte limit"); size += length; }
            }
    }
    internal static byte[] ReadBounded(string path, long limit)
    {
        using var input = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read | FileShare.Delete);
        Contract.Require(input.Length <= limit, "size", "File exceeds its byte limit");
        var bytes = new byte[checked((int)input.Length)]; input.ReadExactly(bytes);
        Contract.Require(input.ReadByte() == -1, "library_changed", "File changed while reading"); return bytes;
    }
}
