// SPDX-License-Identifier: LGPL-2.1-or-later
namespace Folderstorm.Theme;

public static class ProjectImport
{
    public static string Destination(string path, string? protectedLibrary = null)
    {
        path = Path.TrimEndingDirectorySeparator(Path.GetFullPath(path));
        foreach (var library in new[] { ThemeLibrary.DefaultRoot, protectedLibrary }.OfType<string>())
        {
            var root = Path.TrimEndingDirectorySeparator(Path.GetFullPath(library));
            Contract.Require(!path.Equals(root, StringComparison.OrdinalIgnoreCase) && !path.StartsWith(root + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase), "project_overlap", "Choose a project folder outside the theme library");
        }
        Contract.Require(!Directory.Exists(path) && !File.Exists(path), "project_exists", "Choose a new project directory");
        var parent = Path.GetDirectoryName(path); Contract.Require(parent != null && Directory.Exists(parent), "project_directory", "Choose an existing parent directory");
        return Contract.Contained(parent, Path.GetFileName(path));
    }
    public static ThemeProject Create(string directory, ValidatedTheme theme, BaseCatalog? catalog = null, string? protectedLibrary = null)
    {
        directory = Destination(directory, protectedLibrary);
        var files = theme.Files.ToDictionary(file => file.Key, file => file.Value.ToArray(), StringComparer.Ordinal);
        var validated = PackageStore.Validate(files, catalog);
        Contract.Require(validated.Revision == theme.Revision, "integrity", "Package changed before import");
        var parent = Path.GetDirectoryName(directory)!;
        var staging = Contract.Contained(parent, ".theme-import-" + Guid.NewGuid().ToString("N")); Directory.CreateDirectory(staging);
        try
        {
            var header = validated.Manifest;
            var project = new ThemeProject
            {
                ThemeId = header.ThemeId, DisplayName = header.DisplayName, PackageVersion = header.PackageVersion,
                Author = header.Author, Description = header.Description, BaseSkin = header.BaseSkin, BaseScheme = header.BaseScheme,
                BaseDependencies = Contract.Read<DependencySet>(files["base-dependencies.json"]), Origin = "compiled-package", Extensions = header.Extensions,
                Edits = new() { CompiledManifestPath = ProjectAssets.Store(staging, files["theme.json"], ".json"), HostPaletteVersion = files.ContainsKey(HostProject.PayloadPath) ? 1 : null }
            };
            foreach (var payload in header.Files.Where(file => file.Path != "base-dependencies.json"))
            {
                var relative = ProjectAssets.Store(staging, files[payload.Path], Path.GetExtension(payload.Path));
                project.Edits.Assets.Add(new(payload.Path, relative, relative, payload.Kind));
            }
            ProjectStore.Save(staging, project);
            if (catalog != null)
            {
                var compiled = PackageStore.Compile(staging, project, catalog);
                Contract.Require(compiled.Files.Count == files.Count && compiled.Files.All(file => files[file.Key].SequenceEqual(file.Value)), "integrity", "Imported project changed compiled bytes");
            }
            // The final folder appears only when the full project has been validated.
            for (var attempt = 0; ; ++attempt)
            {
                try { Destination(directory, protectedLibrary); Directory.Move(staging, directory); break; }
                catch (IOException error) when (OperatingSystem.IsWindows() && attempt < 5 && (error.HResult & 0xffff) is 5 or 32 or 33) { Thread.Sleep(100 << attempt); }
            }
            return project;
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or ThemeException)
        { throw new ThemeException("project_import", "Import incomplete. Destination is unchanged; recovery files are at " + staging + ". " + error.Message); }
    }
}
