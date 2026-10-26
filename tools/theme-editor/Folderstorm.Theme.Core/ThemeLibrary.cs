// SPDX-License-Identifier: LGPL-2.1-or-later
namespace Folderstorm.Theme;

public sealed record ThemeSelection(string ThemeId, string Revision);
public static class ThemeLibrary
{
    public static string DefaultRoot => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Folderstorm", "theme-library");
    public static string Publish(ValidatedTheme theme, string? root = null, BaseCatalog? catalog = null)
    {
        root ??= DefaultRoot; Directory.CreateDirectory(root);
        using var ownership = new FileStream(Path.Combine(root, ".import.lock"), FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None);
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
            Directory.Move(temporary, destination); return destination;
        }
        finally { if (Directory.Exists(temporary)) Directory.Delete(temporary, true); }
    }
    public static void Select(string profileDirectory, ThemeSelection? selection)
    {
        Directory.CreateDirectory(profileDirectory);
        var path = Path.Combine(profileDirectory, "custom-theme.json");
        if (File.Exists(path)) Contract.AtomicWrite(Path.Combine(profileDirectory, "custom-theme.last-good.json"), File.ReadAllBytes(path));
        Contract.AtomicWrite(path, Contract.Bytes(selection));
    }
}
