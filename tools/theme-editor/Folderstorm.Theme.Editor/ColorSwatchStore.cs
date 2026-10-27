// SPDX-License-Identifier: LGPL-2.1-or-later
using System.IO;

namespace Folderstorm.Theme.Editor;

public sealed record SavedSwatches(int Version, string[] Colors);
public sealed record SwatchState(string[] Colors, bool CanWrite, string? Message = null);
public sealed class ColorSwatchStore(string path)
{
    public const int Limit = 32;
    public static string DefaultPath => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Folderstorm", "theme-editor", "swatches.json");
    public string FilePath { get; } = Path.GetFullPath(path);
    private string SafePath(string name) => Contract.Contained(Path.GetDirectoryName(FilePath)!, name);
    public SwatchState Load()
    {
        try
        {
            if (!File.Exists(FilePath)) return new([], true);
            var safe = SafePath(Path.GetFileName(FilePath));
            using var input = new FileStream(safe, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            Contract.Require(input.Length is > 0 and <= 16384, "swatches", "Saved swatches exceed the supported size");
            var bytes = new byte[(int)input.Length]; input.ReadExactly(bytes);
            var document = Contract.Read<SavedSwatches>(bytes);
            Contract.Require(document.Version == 1 && document.Colors != null && document.Colors.Length <= Limit && document.Colors.All(color => color != null), "swatches", "Saved swatches need a compatible lab version");
            var colors = document.Colors.Select(color => ThemeColor.Parse(color).Hex()).ToArray();
            Contract.Require(colors.Distinct(StringComparer.Ordinal).Count() == colors.Length, "swatches", "Saved swatches contain duplicate colors");
            return new(colors, true);
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or ThemeException or ArgumentException)
        { return new([], false, "Saved swatches were kept unchanged: " + error.Message); }
    }
    public SwatchState Save(ThemeColor color) => Change(color.Hex(), false);
    public SwatchState Remove(ThemeColor color) => Change(color.Hex(), true);
    private SwatchState Change(string color, bool remove)
    {
        Directory.CreateDirectory(Path.GetDirectoryName(FilePath)!);
        var safe = SafePath(Path.GetFileName(FilePath)); var lockPath = SafePath(Path.GetFileName(FilePath) + ".lock");
        FileStream? lease = null;
        for (var attempt = 0; attempt < 30 && lease == null; ++attempt)
        {
            try { lease = new(lockPath, FileMode.OpenOrCreate, FileAccess.ReadWrite, FileShare.None); }
            catch (IOException) when (attempt < 29) { Thread.Sleep(50); }
            catch (IOException) { throw new ThemeException("swatches_busy", "Another lab is saving swatches. Try again."); }
        }
        using (lease)
        {
            var current = Load(); Contract.Require(current.CanWrite, "swatches_readonly", current.Message ?? "Saved swatches are read-only");
            var colors = current.Colors.ToList();
            if (remove) { if (!colors.Remove(color)) return current; }
            else
            {
                if (colors.Contains(color, StringComparer.Ordinal)) return current;
                Contract.Require(colors.Count < Limit, "swatches_full", "32 colors saved. Remove a swatch before adding another."); colors.Add(color);
            }
            Contract.AtomicWrite(safe, Contract.Bytes(new SavedSwatches(1, colors.ToArray()))); return new(colors.ToArray(), true);
        }
    }
}
