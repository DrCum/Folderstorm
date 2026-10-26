// SPDX-License-Identifier: LGPL-2.1-or-later
using System.Buffers.Binary;
using System.Text;
using System.Xml.Linq;

namespace Folderstorm.Theme;

public static class ImageProject
{
    private const string RegistrationPath = "viewer/textures/textures.xml";
    private static string ScopedFile(string name) => "theme/" + Contract.Hash(Encoding.UTF8.GetBytes(name)) + ".png";
    private static XDocument Registrations(string directory, ThemeProject project)
    {
        var asset = project.Edits.Assets.LastOrDefault(item => item.PayloadPath == RegistrationPath);
        if (asset == null) return new(new XElement("textures"));
        var document = Contract.Xml(File.ReadAllBytes(Contract.Contained(directory, asset.AssetPath)));
        Contract.Require(document.Root?.Name == "textures", "texture", "Expected texture registrations"); return document;
    }
    public static TextureDefinition Resolve(string directory, ThemeProject project, BaseCatalog catalog, string name)
    {
        Contract.Require(catalog.Textures.TryGetValue(name, out var inherited), "texture", "Unknown image: " + name);
        var nodes = Registrations(directory, project).Root!.Elements("texture").Where(node => (string?)node.Attribute("name") == name).ToArray();
        Contract.Require(nodes.Length <= 1, "texture", "Duplicate registration: " + name);
        if (nodes.Length == 0) return inherited;
        var attributes = nodes[0].Attributes().ToDictionary(attribute => attribute.Name.LocalName, attribute => attribute.Value, StringComparer.Ordinal);
        return new(name, attributes, attributes.TryGetValue("file_name", out var file) ? catalog.Resolve("textures/" + file) : null);
    }
    public static byte[] Read(string directory, ThemeProject project, BaseCatalog catalog, string name)
    {
        var texture = Resolve(directory, project, catalog, name);
        Contract.Require(texture.Attributes.TryGetValue("file_name", out var file), "reference", "Image has no filename: " + name);
        var asset = project.Edits.Assets.LastOrDefault(item => item.PayloadPath == "viewer/textures/" + file);
        if (asset == null) return catalog.ResolveBytes("textures/" + file);
        var path = Contract.Contained(directory, asset.AssetPath);
        Contract.Require(new FileInfo(path).Length <= Contract.ArchiveLimit, "image", "Image exceeds the editing input limit");
        return File.ReadAllBytes(path);
    }
    private static void Ready(ThemeProject project, BaseCatalog catalog)
    {
        ProjectStore.EnsureEditable(project);
        Contract.Require(project.BaseSkin == catalog.Skin && project.BaseScheme == catalog.Scheme, "base", "Image editor base differs from the project");
    }
    private static ProjectAsset PrepareRegistrations(string directory, ThemeProject project, BaseCatalog catalog, XDocument document)
    {
        var original = project.Edits.Assets.LastOrDefault(asset => asset.PayloadPath == RegistrationPath)?.OriginalPath;
        // Ignore whitespace outside the root so repeated parse/write cycles stay stable.
        var normalized = new XDocument(new XElement(document.Root!));
        return ProjectAssets.Prepare(directory, RegistrationPath, Contract.XmlBytes(normalized),
            original == null ? catalog.ResolveBytes("textures/textures.xml") : null, ".xml", original);
    }
    private static void PutRegistration(XDocument document, string name, Dictionary<string, string> attributes)
    {
        var previous = document.Root!.Elements("texture").Where(node => (string?)node.Attribute("name") == name).ToArray();
        Contract.Require(previous.Length <= 1, "texture", "Duplicate registration: " + name);
        var replacement = new XElement("texture", attributes.OrderBy(entry => entry.Key, StringComparer.Ordinal).Select(entry => new XAttribute(entry.Key, entry.Value)));
        if (previous.Length == 0) document.Root.Add(replacement); else previous[0].ReplaceWith(replacement);
    }
    public static void SetPng(string directory, ThemeProject project, BaseCatalog catalog, string name, byte[] png, TextureGeometry geometry, Recipe operation,
        byte[]? importedOriginal = null, string? importedExtension = null)
    {
        Ready(project, catalog); geometry.Validate(); PackageStore.ValidateImage(png, "image.png");
        Contract.Require(BinaryPrimitives.ReadUInt32BigEndian(png.AsSpan(16, 4)) == geometry.Width && BinaryPrimitives.ReadUInt32BigEndian(png.AsSpan(20, 4)) == geometry.Height,
            "image", "Image output and guides have different dimensions");
        Contract.Require(operation.Operation != "replace" || importedOriginal != null, "recipe", "Replacement must retain its imported original");
        var texture = Resolve(directory, project, catalog, name);
        Contract.Require(texture.Attributes.TryGetValue("file_name", out var oldFile), "reference", "Image has no filename: " + name);
        var prior = project.Edits.Assets.LastOrDefault(asset => asset.PayloadPath == "viewer/textures/" + oldFile);
        var original = importedOriginal ?? (prior == null ? Read(directory, project, catalog, name) : null);
        var originalPath = importedOriginal == null ? prior?.OriginalPath : null;
        var extension = importedOriginal == null ? Path.GetExtension(oldFile) : importedExtension;
        var fileName = ScopedFile(name);
        var nextRecipe = RecipeHistory.Append(importedOriginal == null ? project.Edits.Recipes.GetValueOrDefault(name) : null, operation);
        var image = ProjectAssets.Prepare(directory, "viewer/textures/" + fileName, png, original, string.IsNullOrEmpty(extension) ? ".bin" : extension, originalPath);
        var attributes = geometry.ApplyTo(texture.Attributes); attributes["name"] = name; attributes["file_name"] = fileName;
        var registrations = Registrations(directory, project); PutRegistration(registrations, name, attributes);
        var registration = PrepareRegistrations(directory, project, catalog, registrations);
        var recipes = new Dictionary<string, Recipe>(project.Edits.Recipes, StringComparer.Ordinal) { [name] = nextRecipe };
        var assets = project.Edits.Assets.Where(asset => asset.PayloadPath != image.PayloadPath && asset.PayloadPath != RegistrationPath).Concat([image, registration]).ToList();
        project.Checkpoint(operation.Operation + " " + name); project.Edits = project.Edits with { Assets = assets, Recipes = recipes };
    }
    public static void SetGeometry(string directory, ThemeProject project, BaseCatalog catalog, string name, TextureGeometry geometry)
    {
        Ready(project, catalog); geometry.Validate(); var texture = Resolve(directory, project, catalog, name);
        var registrations = Registrations(directory, project); PutRegistration(registrations, name, geometry.ApplyTo(texture.Attributes));
        var registration = PrepareRegistrations(directory, project, catalog, registrations);
        var operation = new Recipe("nine-slice", new()
        {
            ["left"] = System.Text.Json.JsonSerializer.SerializeToElement(geometry.Margins.Left), ["top"] = System.Text.Json.JsonSerializer.SerializeToElement(geometry.Margins.Top),
            ["right"] = System.Text.Json.JsonSerializer.SerializeToElement(geometry.Margins.Right), ["bottom"] = System.Text.Json.JsonSerializer.SerializeToElement(geometry.Margins.Bottom),
            ["clip_left"] = System.Text.Json.JsonSerializer.SerializeToElement(geometry.Clip.Left), ["clip_top"] = System.Text.Json.JsonSerializer.SerializeToElement(geometry.Clip.Top),
            ["clip_right"] = System.Text.Json.JsonSerializer.SerializeToElement(geometry.Clip.Right), ["clip_bottom"] = System.Text.Json.JsonSerializer.SerializeToElement(geometry.Clip.Bottom)
        });
        var recipes = new Dictionary<string, Recipe>(project.Edits.Recipes, StringComparer.Ordinal) { [name] = RecipeHistory.Append(project.Edits.Recipes.GetValueOrDefault(name), operation) };
        var assets = project.Edits.Assets.Where(asset => asset.PayloadPath != RegistrationPath).Append(registration).ToList();
        project.Checkpoint("Set guides " + name); project.Edits = project.Edits with { Assets = assets, Recipes = recipes };
    }

    public static bool Reset(string directory, ThemeProject project, BaseCatalog catalog, IEnumerable<string> names,
        Func<byte[], string, byte[]>? encodePng = null)
    {
        Ready(project, catalog);
        var selected = names.Distinct(StringComparer.Ordinal).Order(StringComparer.Ordinal).ToArray();
        Contract.Require(selected.Length <= catalog.Textures.Count && selected.All(catalog.Textures.ContainsKey), "texture", "Choose known image IDs to reset");
        if (selected.Length == 0) return false;
        var registrations = Registrations(directory, project);
        var assets = project.Edits.Assets.Where(asset => asset.PayloadPath != RegistrationPath).ToList();
        var recipes = new Dictionary<string, Recipe>(project.Edits.Recipes, StringComparer.Ordinal);
        foreach (var name in selected)
        {
            var inherited = catalog.Textures[name];
            var nodes = registrations.Root!.Elements("texture").Where(node => (string?)node.Attribute("name") == name).ToArray();
            Contract.Require(nodes.Length <= 1, "texture", "Duplicate registration: " + name);
            foreach (var node in nodes) node.Remove();
            recipes.Remove(name);

            if (!inherited.Attributes.TryGetValue("file_name", out var file) || !assets.Any(asset => asset.PayloadPath == "viewer/textures/" + file)) continue;
            // A shared physical override must stay for other IDs. Give this ID a private copy of the inherited pixels.
            var original = catalog.ResolveBytes("textures/" + file);
            var png = original;
            if (!file.EndsWith(".png", StringComparison.OrdinalIgnoreCase))
            {
                Contract.Require(encodePng != null, "image", "Resetting this shared image requires PNG conversion: " + name);
                png = encodePng!(original, file);
            }
            PackageStore.ValidateImage(png, "inherited.png");
            var image = ProjectAssets.Prepare(directory, "viewer/textures/" + ScopedFile(name), png, original, Path.GetExtension(file));
            assets.RemoveAll(asset => asset.PayloadPath == image.PayloadPath); assets.Add(image);
            var attributes = new Dictionary<string, string>(inherited.Attributes, StringComparer.Ordinal) { ["file_name"] = ScopedFile(name) };
            PutRegistration(registrations, name, attributes);
        }

        var registered = registrations.Root!.Elements("texture").ToDictionary(node => (string)node.Attribute("name")!, StringComparer.Ordinal);
        var referenced = new HashSet<string>(StringComparer.Ordinal);
        foreach (var texture in catalog.Textures.Values)
        {
            var file = registered.TryGetValue(texture.Name, out var node) ? (string?)node.Attribute("file_name") : texture.Attributes.GetValueOrDefault("file_name");
            if (file != null) referenced.Add("viewer/textures/" + file);
        }
        foreach (var node in registered.Values)
            if ((string?)node.Attribute("file_name") is { } file) referenced.Add("viewer/textures/" + file);
        var scoped = selected.Select(name => "viewer/textures/" + ScopedFile(name)).ToHashSet(StringComparer.Ordinal);
        assets.RemoveAll(asset => scoped.Contains(asset.PayloadPath) && !referenced.Contains(asset.PayloadPath));
        if (registrations.Root.Elements("texture").Any()) assets.Add(PrepareRegistrations(directory, project, catalog, registrations));
        var edits = project.Edits with { Assets = assets, Recipes = recipes };
        if (Contract.Bytes(edits).SequenceEqual(Contract.Bytes(project.Edits))) return false;
        project.Checkpoint(selected.Length == 1 ? "Reset image " + selected[0] : "Reset image group"); project.Edits = edits; return true;
    }

    public static bool ResetAll(ThemeProject project, BaseCatalog catalog)
    {
        Ready(project, catalog);
        if (!project.Edits.Assets.Any(asset => asset.PayloadPath.StartsWith("viewer/textures/", StringComparison.Ordinal)) && project.Edits.Recipes.Count == 0) return false;
        project.Checkpoint("Reset all images");
        project.Edits = project.Edits with
        {
            Assets = project.Edits.Assets.Where(asset => !asset.PayloadPath.StartsWith("viewer/textures/", StringComparison.Ordinal)).ToList(),
            Recipes = new(StringComparer.Ordinal)
        };
        return true;
    }
}
