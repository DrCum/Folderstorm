// SPDX-License-Identifier: LGPL-2.1-or-later
namespace Folderstorm.Theme;

public static class ProjectCopy
{
    // Stage a complete project, then expose the new folder with one rename.
    public static string SaveAs(string source, string destination, ThemeProject project)
    {
        ProjectStore.EnsureEditable(project);
        source = Path.TrimEndingDirectorySeparator(Path.GetFullPath(source));
        destination = Path.TrimEndingDirectorySeparator(Path.GetFullPath(destination));
        static bool Inside(string path, string root) => path.Equals(root, StringComparison.OrdinalIgnoreCase) ||
            path.StartsWith(root.TrimEnd(Path.DirectorySeparatorChar) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);
        Contract.Require(!Inside(destination, source) && !Inside(source, destination), "project_overlap", "Choose a new folder outside the source project");
        Contract.Require(!File.Exists(destination) && !Directory.Exists(destination), "project_exists", "Save As requires a new project folder");
        var parent = Path.GetDirectoryName(destination);
        Contract.Require(parent != null && Directory.Exists(parent), "project_directory", "The destination parent must exist");
        Contract.Contained(parent, Path.GetFileName(destination));
        var document = ProjectStore.OpenDocument(source);
        Contract.Require(document.CanEdit, "project_readonly", "The source project contains unsupported edits");
        var directories = new List<string>(); var files = new List<(string Relative, long Length)>();
        var pending = new Stack<string>(); pending.Push(source); long expanded = 0;
        while (pending.Count > 0)
            foreach (var path in Directory.EnumerateFileSystemEntries(pending.Pop()))
            {
                var relative = Path.GetRelativePath(source, path).Replace('\\', '/');
                Contract.Contained(source, relative);
                Contract.Require(directories.Count + files.Count < Contract.EntryLimit, "size", "Project copy exceeds 10000 entries");
                if (Directory.Exists(path)) { directories.Add(relative); pending.Push(path); }
                else
                {
                    var length = new FileInfo(path).Length; expanded = checked(expanded + length);
                    Contract.Require(expanded <= Contract.ExpandedLimit, "size", "Project copy exceeds 512 MiB");
                    files.Add((relative, length));
                }
            }
        var staging = destination + ".staging-" + Guid.NewGuid().ToString("N");
        Contract.Contained(parent, Path.GetFileName(staging)); Directory.CreateDirectory(staging);
        try
        {
            foreach (var relative in directories) Directory.CreateDirectory(Contract.Contained(staging, relative));
            foreach (var (relative, length) in files)
            {
                using var input = new FileStream(Contract.Contained(source, relative), FileMode.Open, FileAccess.Read, FileShare.Read);
                Contract.Require(input.Length == length, "project_changed", "Project file changed during copy: " + relative);
                using var output = new FileStream(Contract.Contained(staging, relative), FileMode.CreateNew, FileAccess.Write, FileShare.None);
                input.CopyTo(output); output.Flush(true);
                Contract.Require(output.Length == length, "project_changed", "Project file changed during copy: " + relative);
            }
            ProjectStore.Save(staging, project);
            Directory.Move(staging, destination);
            return destination;
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or ThemeException)
        {
            // Preserve a failed stage for diagnosis; never erase the user's source.
            throw new ThemeException("project_copy", "Copy incomplete. Source is unchanged; partial copy retained at " + staging + ". " + error.Message);
        }
    }
}
