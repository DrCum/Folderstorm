// SPDX-License-Identifier: LGPL-2.1-or-later
using Folderstorm.Theme;
using System.Text;
using System.Text.Json.Nodes;

internal static class LibraryChecks
{
    public static void Register(Action<string, Action> test, string root, BaseCatalog catalog)
    {
        void Assert(bool value, string message) { if (!value) throw new Exception(message); }
        void Reject(string code, Action action) { try { action(); throw new Exception("Expected " + code); } catch (ThemeException error) { Assert(error.Code == code, error.Message); } }
        var directory = Path.Combine(root, "library-project"); ThemeProject? project = null; byte[]? first = null; byte[]? second = null;
        var library = Path.Combine(root, "library"); LibraryEntry? entry = null;
        test("library thumbnail edits preserve exact bytes, originals, unrelated edits and durable history", () =>
        {
            var samples = catalog.Textures.Values.Where(texture => texture.Source?.EndsWith(".png", StringComparison.OrdinalIgnoreCase) == true).Select(texture => texture.Source!).Distinct(StringComparer.Ordinal).Take(2).ToArray();
            first = File.ReadAllBytes(Path.Combine(catalog.Root, samples[0])); second = File.ReadAllBytes(Path.Combine(catalog.Root, samples[1]));
            Assert(!first.SequenceEqual(second), "Thumbnail fixtures should differ");
            project = ProjectStore.Create(catalog, "Pixel gallery"); project.Edits.Colors["ThemeAccent"] = "1 0 0 1";
            Assert(ThumbnailProject.SetPng(directory, project, catalog, first) && !ThumbnailProject.SetPng(directory, project, catalog, first) && project.Undo.Count == 1, "Thumbnail Set/no-op failed");
            var original = project.Edits.Assets.Single(asset => asset.PayloadPath == "preview.png").OriginalPath;
            Assert(ThumbnailProject.SetPng(directory, project, catalog, second) && project.Undo.Count == 2 && project.Edits.Assets.Single().OriginalPath == original, "Thumbnail replacement lost original/history");
            Assert(project.UndoEdit() && ThumbnailProject.Read(directory, project)!.SequenceEqual(first) && project.RedoEdit() && ThumbnailProject.Read(directory, project)!.SequenceEqual(second), "Thumbnail Undo/Redo changed bytes");
            var before = Contract.Bytes(project); Reject("image", () => ThumbnailProject.SetPng(directory, project, catalog, Encoding.UTF8.GetBytes("not PNG")));
            Assert(Contract.Bytes(project).SequenceEqual(before), "Invalid PNG changed project/history");
            Assert(ThumbnailProject.Reset(directory, project, catalog) && ThumbnailProject.Read(directory, project) == null && project.Edits.Colors.Count == 1 && !ThumbnailProject.Reset(directory, project, catalog) && project.UndoEdit(), "Thumbnail reset/no-op/Undo lost other edits");
            Assert(File.ReadAllBytes(Contract.Contained(directory, original)).SequenceEqual(first), "Original thumbnail bytes changed");
            ProjectStore.Save(directory, project); project = ProjectStore.Open(directory); Assert(ThumbnailProject.Read(directory, project)!.SequenceEqual(second), "Saved thumbnail changed");
            Reject("project_version", () => ThumbnailProject.SetPng(directory, project with { ProjectVersion = 99 }, catalog, first));
            var theme = PackageStore.Compile(directory, project, catalog); ThemeLibrary.Publish(theme, library, catalog);
            Contract.AtomicWrite(Path.Combine(directory, "result.json"), Contract.Bytes(new { revision = theme.Revision, path = Path.Combine(library, theme.Manifest.ThemeId, theme.Revision) }));
        });
        test("library header scan and thumbnail read preserve files; selected revision validates fully", () =>
        {
            var before = Directory.EnumerateFiles(library, "*", SearchOption.AllDirectories).ToDictionary(path => path, Contract.HashFile);
            var scan = LibraryCatalog.Scan(library); Assert(scan.Entries.Length == 1 && !scan.Truncated && scan.Warnings.Length == 0, "Library listing lost valid revision");
            entry = scan.Entries.Single(); Assert(entry.Error == null && entry.DisplayName == "Pixel gallery" && entry.HasPreview && LibraryCatalog.ReadPreview(entry)!.SequenceEqual(second!), "Header/thumbnail differs");
            var theme = LibraryCatalog.Validate(entry, catalog); Assert(theme.Revision == entry.Revision && theme.Files["preview.png"].SequenceEqual(second!), "Selected library validation changed payload");
            Assert(before.Count == Directory.EnumerateFiles(library, "*", SearchOption.AllDirectories).Count() && before.All(file => Contract.HashFile(file.Key) == file.Value), "Library inspection wrote files");
        });
        test("library refuses stale headers, wrong revision identity and corrupt previews without writes", () =>
        {
            var revision = Path.Combine(library, entry!.ThemeId, entry.Revision); var headerPath = Path.Combine(revision, "theme.json"); var saved = File.ReadAllBytes(headerPath);
            var json = JsonNode.Parse(saved)!; json["description"] = "Externally changed"; File.WriteAllText(headerPath, json.ToJsonString());
            Reject("library_changed", () => LibraryCatalog.Validate(entry, catalog)); Reject("library_changed", () => LibraryCatalog.ReadPreview(entry));
            var changed = LibraryCatalog.Scan(library).Entries.Single(); Reject("library_identity", () => LibraryCatalog.Validate(changed, catalog));
            File.WriteAllBytes(headerPath, saved);
            var previewPath = Path.Combine(revision, "preview.png"); File.WriteAllBytes(previewPath, first!); Reject("integrity", () => LibraryCatalog.ReadPreview(entry)); File.WriteAllBytes(previewPath, second!);
            json["format_version"] = 99; File.WriteAllText(headerPath, json.ToJsonString()); var future = LibraryCatalog.Scan(library).Entries.Single();
            Assert(future.Error?.StartsWith("format_version:", StringComparison.Ordinal) == true && future.Header == null, "Future manifest was advertised as usable"); Reject("library_entry", () => LibraryCatalog.Validate(future, catalog));
            File.WriteAllBytes(headerPath, saved);
            var bounded = Path.Combine(root, "bounded-library"); Directory.CreateDirectory(bounded);
            for (var index = 0; index < 3; ++index) Directory.CreateDirectory(Path.Combine(bounded, Guid.NewGuid().ToString("D"), new string((char)('a' + index), 64)));
            Assert(LibraryCatalog.Scan(bounded, 1).Truncated, "Library scan did not report truncation");
            Assert(LibraryCatalog.Scan(Path.Combine(root, "missing-library")).Entries.Length == 0 && !Directory.Exists(Path.Combine(root, "missing-library")), "Empty scan created a library");
        });
    }
}
