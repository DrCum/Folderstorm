// SPDX-License-Identifier: LGPL-2.1-or-later
namespace Folderstorm.Theme;

public static class ProjectStore
{
    public const string FileName = "project.fsthemeproj";
    public static ThemeProject Create(BaseCatalog catalog, string name) => new()
    { DisplayName = name, BaseSkin = catalog.Skin, BaseScheme = catalog.Scheme, BaseDependencies = catalog.Fingerprint() };

    public static ThemeProject Open(string directory)
    {
        var project = Contract.Read<ThemeProject>(File.ReadAllBytes(Path.Combine(directory, FileName)));
        Contract.Require(project.ProjectVersion == 1, "project_version", "Unsupported project version; preserve this project for a newer editor");
        ValidateAssets(directory, project);
        return project;
    }
    public static void Save(string directory, ThemeProject project)
    {
        Directory.CreateDirectory(directory);
        Contract.Require(project.ProjectVersion == 1, "project_version", "Unsupported project version");
        ValidateAssets(directory, project);
        var previous = Path.Combine(directory, FileName);
        if (File.Exists(previous))
        {
            var bytes = File.ReadAllBytes(previous);
            Contract.AtomicWrite(Path.Combine(directory, "checkpoints", Contract.Hash(bytes) + ".fsthemeproj"), bytes);
        }
        Contract.AtomicWrite(Path.Combine(directory, FileName), Contract.Bytes(project));
    }
    private static void ValidateAssets(string directory, ThemeProject project)
    {
        foreach (var asset in project.Edits.Assets.Concat(project.Undo.SelectMany(c => c.Edits.Assets)).Concat(project.Redo.SelectMany(c => c.Edits.Assets)))
        {
            Contract.PathName(asset.PayloadPath);
            Contract.Require(File.Exists(Contract.Contained(directory, asset.AssetPath)) && File.Exists(Contract.Contained(directory, asset.OriginalPath)), "project_asset", asset.AssetPath);
        }
        foreach (var value in project.Edits.Colors.Values) Contract.Rgba(value);
    }
    public static ProjectAsset ImportAsset(string directory, string input, string payload, string kind)
    {
        Contract.PathName(payload); Directory.CreateDirectory(directory);
        var bytes = File.ReadAllBytes(input); var hash = Contract.Hash(bytes);
        var relative = "assets/" + hash + Path.GetExtension(input).ToLowerInvariant();
        var path = Contract.Contained(directory, relative);
        if (!File.Exists(path)) Contract.AtomicWrite(path, bytes);
        return new(payload, relative, relative, kind);
    }
}
