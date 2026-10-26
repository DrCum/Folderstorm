// SPDX-License-Identifier: LGPL-2.1-or-later
namespace Folderstorm.Theme;

public static class ProjectAssets
{
    public static string Store(string directory, byte[] bytes, string extension)
    {
        Contract.Require(extension.Length is >= 2 and <= 16 && extension[0] == '.' && extension[1..].All(char.IsAsciiLetterOrDigit), "path", "Use a plain asset extension");
        Contract.Require(bytes.LongLength <= 128L * 1024 * 1024, "size", "Original asset exceeds 128 MiB");
        Directory.CreateDirectory(directory);
        var relative = "assets/" + Contract.Hash(bytes) + extension.ToLowerInvariant();
        var path = Contract.Contained(directory, relative);
        if (File.Exists(path)) Contract.Require(Contract.HashFile(path) == Contract.Hash(bytes), "project_asset", "Stored original has changed: " + relative);
        else Contract.AtomicWrite(path, bytes);
        return relative;
    }
    public static ProjectAsset Prepare(string directory, string payload, byte[] bytes, byte[]? original = null, string? originalExtension = null, string? preservedOriginal = null)
    {
        var kind = PackageStore.Kind(payload);
        var limit = kind == "font" ? 64L * 1024 * 1024 : kind == "image" ? 32L * 1024 * 1024 : Contract.ManifestLimit;
        Contract.Require(bytes.LongLength <= limit, "size", "Asset exceeds the package limit: " + payload);
        Contract.Require(original == null || preservedOriginal == null, "project_asset", "Choose original bytes or a preserved original path");
        if (preservedOriginal != null) Contract.Require(File.Exists(Contract.Contained(directory, preservedOriginal)), "project_asset", "Missing original: " + preservedOriginal);
        var assetPath = Store(directory, bytes, Path.GetExtension(payload));
        var originalPath = preservedOriginal ?? (original == null ? assetPath : Store(directory, original, originalExtension ?? Path.GetExtension(payload)));
        return new(payload, assetPath, originalPath, kind);
    }
}
