// SPDX-License-Identifier: LGPL-2.1-or-later
namespace Folderstorm.Theme;

public sealed record ThemeSelection(string ThemeId, string Revision);
public static class ThemeLibrary
{
    public static string DefaultRoot => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Folderstorm", "theme-library");
    public static string Publish(ValidatedTheme theme, string? root = null, BaseCatalog? catalog = null)
    {
        root ??= DefaultRoot; Directory.CreateDirectory(root);
        using var ownership = RetryFileOperation(() => new FileStream(Path.Combine(root, ".import.lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None));
        var parent = Contract.Contained(root, theme.Manifest.ThemeId); Directory.CreateDirectory(parent);
        var destination = Contract.Contained(root, theme.Manifest.ThemeId + "/" + theme.Revision);
        if (Directory.Exists(destination))
        {
            var current = PackageStore.ReadRevision(destination, catalog);
            Contract.Require(current.Revision == theme.Revision, "integrity", "Existing revision differs"); return destination;
        }
        var temporary = Path.Combine(root, ".staging-" + Guid.NewGuid().ToString("N")); Directory.CreateDirectory(temporary);
        try
        {
            foreach (var file in theme.Files) Contract.AtomicWrite(Contract.Contained(temporary, file.Key), file.Value);
            RetryFileOperation(() => { Directory.Move(temporary, destination); return true; }); return destination;
        }
        finally { if (Directory.Exists(temporary)) Directory.Delete(temporary, true); }
    }
    private static T RetryFileOperation<T>(Func<T> action)
    {
        for (var attempt = 0; ; ++attempt)
        {
            try { return action(); }
            catch (Exception error) when (OperatingSystem.IsWindows() && attempt < 5 &&
                error is IOException or UnauthorizedAccessException && (error.HResult & 0xffff) is 5 or 32 or 33)
            {
                // Windows readers can briefly block an atomic directory rename.
                // Keep the import lock and immutable destination while retrying.
                Thread.Sleep(100 << attempt);
            }
        }
    }
    public static void Select(string profileDirectory, ThemeSelection? selection)
    {
        Directory.CreateDirectory(profileDirectory);
        var path = Path.Combine(profileDirectory, "custom-theme.json");
        if (File.Exists(path)) Contract.AtomicWrite(Path.Combine(profileDirectory, "custom-theme.last-good.json"), File.ReadAllBytes(path));
        Contract.AtomicWrite(path, Contract.Bytes(selection));
    }
}
