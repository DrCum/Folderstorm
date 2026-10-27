// SPDX-License-Identifier: LGPL-2.1-or-later
using System.Text;
using System.Xml.Linq;

namespace Folderstorm.Theme;

public enum ChangeGroup { Palette, Images, Fonts, Layouts, Host, Preview, Files }
public sealed record ProjectChange(string Id, ChangeGroup Group, string Name, string Before, string After, string Source)
{
    public string Label => Group + " · " + Name;
    public override string ToString() => Label;
}
public sealed record ChangeReview(string EditsHash, ProjectChange[] Entries);
public sealed record ChangeReset(string EditsHash, string? Entry = null, ChangeGroup? Group = null);

public static class ProjectChanges
{
    private const int DisplayLimit = 32768;
    public static ChangeReview Inspect(string directory, ThemeProject project, BaseCatalog catalog)
    {
        ProjectStore.EnsureEditable(project);
        Contract.Require(project.BaseSkin == catalog.Skin && project.BaseScheme == catalog.Scheme, "base", "Review base differs from the project");
        Contract.Require(project.Edits.Assets.Count <= Contract.EntryLimit, "size", "Too many project assets to review");
        var total = project.Edits.Assets.Sum(asset => new FileInfo(Contract.Contained(directory, asset.AssetPath)).Length);
        Contract.Require(total <= Contract.ExpandedLimit, "size", "Project assets exceed512 MiB");
        var entries = new List<ProjectChange>(); var palette = PaletteProject.Read(directory, project, catalog);
        foreach (var name in palette.Literals.Keys.Concat(palette.Links.Keys).Distinct(StringComparer.Ordinal).Order(StringComparer.Ordinal))
        {
            string before, after;
            try { before = catalog.InspectColor(name, new Dictionary<string, string>()).Value.Hex(); }
            catch (ThemeException) { before = "Native inherited value"; }
            try { after = palette.Inspect(name).Value.Hex(); }
            catch (ThemeException) { after = "Native reference"; }
            if (palette.Links.TryGetValue(name, out var target)) after += "\nReference: " + target;
            var origin = project.Edits.Colors.ContainsKey(name) || project.Edits.ColorLinks.ContainsKey(name) ? "Authored override" : "Imported override";
            entries.Add(new("color:" + name, ChangeGroup.Palette, name, before, after, origin));
        }

        var imageAssets = project.Edits.Assets.Where(asset => asset.PayloadPath.StartsWith("viewer/textures/", StringComparison.Ordinal)).ToArray();
        var registration = imageAssets.SingleOrDefault(asset => asset.PayloadPath == "viewer/textures/textures.xml");
        var registrations = registration == null ? [] : Contract.Xml(Read(directory, registration)).Root!.Elements("texture").Select(node => (string)node.Attribute("name")!).ToArray();
        var known = registrations.Concat(project.Edits.Recipes.Keys.Where(catalog.Textures.ContainsKey)).Concat(catalog.Textures.Values.Where(texture =>
            imageAssets.Any(asset => asset.PayloadPath == "viewer/textures/" + texture.Attributes.GetValueOrDefault("file_name"))).Select(texture => texture.Name)).Distinct(StringComparer.Ordinal).Order(StringComparer.Ordinal);
        var usedImages = new HashSet<string>(StringComparer.Ordinal);
        foreach (var name in known)
        {
            var current = ImageProject.Resolve(directory, project, catalog, name); var inherited = catalog.Textures[name];
            var payload = "viewer/textures/" + current.Attributes.GetValueOrDefault("file_name"); usedImages.Add(payload);
            var asset = imageAssets.SingleOrDefault(item => item.PayloadPath == payload);
            var oldFile = inherited.Attributes.GetValueOrDefault("file_name");
            var before = Attributes(inherited.Attributes); var after = Attributes(current.Attributes);
            if (oldFile != null) before += "\n\nPixels SHA256: " + Contract.Hash(catalog.ResolveBytes("textures/" + oldFile));
            if (asset != null) after += "\n\nPixels SHA256: " + Contract.Hash(Read(directory, asset));
            if (project.Edits.Recipes.TryGetValue(name, out var recipe)) after += "\nRecipe: " + recipe.Operation;
            entries.Add(new("image:" + name, ChangeGroup.Images, name, before, after, asset?.AssetPath ?? registration?.AssetPath ?? "Image recipe"));
        }
        foreach (var asset in project.Edits.Assets.OrderBy(asset => asset.PayloadPath, StringComparer.Ordinal))
        {
            if (asset.PayloadPath == PaletteProject.PayloadPath || usedImages.Contains(asset.PayloadPath)) continue;
            var group = GroupOf(asset.PayloadPath);
            // The registration document also shows unreferenced files and raw edits that have no authoring recipe.
            if (group == ChangeGroup.Fonts) continue;
            var before = "Not included";
            var after = FileSummary(directory, asset);
            if (asset.PayloadPath.StartsWith("viewer/xui/", StringComparison.Ordinal) || asset.PayloadPath == "viewer/textures/textures.xml")
            {
                var original = catalog.ResolveBytes(asset.PayloadPath[7..]); var current = Read(directory, asset);
                if (asset.PayloadPath.StartsWith("viewer/xui/", StringComparison.Ordinal)) (before, after) = XuiComparison(original, current);
                else { before = XmlText(original); after = XmlText(current); }
            }
            else if (asset.PayloadPath == "host/palette.json") after = Text(Read(directory, asset));
            entries.Add(new("file:" + asset.PayloadPath, group, asset.PayloadPath, before, after, asset.AssetPath + (asset.PayloadPath == "viewer/textures/textures.xml" ? "\nResetting the registration table resets all artwork overrides and image recipes. Use an image row for a single ID." : "")));
        }
        var fonts = project.Edits.Assets.Where(asset => GroupOf(asset.PayloadPath) == ChangeGroup.Fonts).ToArray();
        if (fonts.Length > 0 || project.Edits.FontConfiguration != null)
        {
            var config = fonts.SingleOrDefault(asset => asset.PayloadPath == "viewer/fonts/fonts.xml");
            var after = config == null ? project.Edits.FontConfiguration is { } bundled ? "Bundled configuration: " + bundled : "Base configuration" : XmlText(Read(directory, config));
            after = "Included font files and licenses:\n" + string.Join("\n\n", fonts.Select(asset => asset.PayloadPath + "\n" + FileSummary(directory, asset))) + "\n\nConfiguration:\n" + after;
            entries.Add(new("fonts:all", ChangeGroup.Fonts, "Font configuration and files", XmlText(FontProject.Read(directory, null, catalog)), Text(after), "Reset restores the base configuration and removes its packaged font/license overrides."));
        }
        foreach (var name in project.Edits.Recipes.Keys.Where(name => !catalog.Textures.ContainsKey(name)).Order(StringComparer.Ordinal))
            entries.Add(new("recipe:" + name, ChangeGroup.Images, name + " recipe", "No recipe", project.Edits.Recipes[name].Operation, "Retained authoring recipe"));
        var identity = EditsHash(project) + string.Concat(project.Edits.Assets.OrderBy(asset => asset.PayloadPath, StringComparer.Ordinal).Select(asset => "\n" + asset.PayloadPath + " " + Contract.Hash(Read(directory, asset))));
        return new(Contract.Hash(Encoding.UTF8.GetBytes(identity)), entries.OrderBy(entry => entry.Group).ThenBy(entry => entry.Name, StringComparer.Ordinal).ToArray());
    }

    public static bool Reset(string directory, ThemeProject project, BaseCatalog catalog, ChangeReset request, Func<byte[], string, byte[]>? encodePng = null)
    {
        ProjectStore.EnsureEditable(project);
        Contract.Require(request.Entry == null || request.Group == null, "changes_selection", "Choose an override, a group or all overrides");
        var review = Inspect(directory, project, catalog);
        Contract.Require(request.EditsHash == review.EditsHash, "changes_stale", "The project changed. Open Changes again.");
        var selected = request.Entry == null ? null : review.Entries.SingleOrDefault(entry => entry.Id == request.Entry);
        Contract.Require(request.Entry == null || selected != null, "changes_selection", "Choose a current override");
        Contract.Require(request.Group == null || Enum.IsDefined(request.Group.Value), "changes_selection", "Choose a known group");
        var edits = Contract.Read<ProjectEdits>(Contract.Bytes(project.Edits));
        // Existing resource editors run against an isolated document. Only the final validated assignment touches history.
        var candidate = project with { Edits = edits, Undo = [], Redo = [] };
        if (selected != null)
        {
            if (selected.Id.StartsWith("color:", StringComparison.Ordinal)) PaletteProject.Apply(candidate, catalog, [new(selected.Name)], "Reset color", directory);
            else if (selected.Id.StartsWith("image:", StringComparison.Ordinal)) ImageProject.Reset(directory, candidate, catalog, [selected.Name], encodePng);
            else if (selected.Id == "fonts:all") RemoveGroup(candidate, ChangeGroup.Fonts);
            else if (selected.Id == "file:viewer/textures/textures.xml") RemoveGroup(candidate, ChangeGroup.Images);
            else if (selected.Id == "file:host/palette.json") RemoveGroup(candidate, ChangeGroup.Host);
            else if (selected.Id.StartsWith("recipe:", StringComparison.Ordinal)) candidate.Edits.Recipes.Remove(selected.Id[7..]);
            else candidate.Edits.Assets.RemoveAll(asset => asset.PayloadPath == selected.Id[5..]);
        }
        else if (request.Group is { } group) RemoveGroup(candidate, group);
        else candidate.Edits = new ProjectEdits { CompiledManifestPath = project.Edits.CompiledManifestPath };
        if (Contract.Bytes(candidate.Edits).SequenceEqual(Contract.Bytes(project.Edits))) return false;
        PackageStore.Compile(directory, candidate, catalog);
        var label = selected == null ? request.Group is { } scope ? "Reset " + scope + " overrides" : "Reset all overrides" : "Reset " + selected.Name;
        project.Checkpoint(label); project.Edits = candidate.Edits; return true;
    }
    private static void RemoveGroup(ThemeProject project, ChangeGroup group)
    {
        if (group == ChangeGroup.Palette) { project.Edits.Colors.Clear(); project.Edits.ColorLinks.Clear(); }
        if (group == ChangeGroup.Images) project.Edits.Recipes.Clear();
        if (group == ChangeGroup.Fonts) project.Edits.FontConfiguration = null;
        if (group == ChangeGroup.Host) project.Edits = project.Edits with { HostPaletteVersion = null };
        project.Edits.Assets.RemoveAll(asset => GroupOf(asset.PayloadPath) == group);
    }
    private static ChangeGroup GroupOf(string path) => path switch
    {
        PaletteProject.PayloadPath => ChangeGroup.Palette,
        "preview.png" => ChangeGroup.Preview,
        _ when path.StartsWith("viewer/textures/", StringComparison.Ordinal) => ChangeGroup.Images,
        _ when path.StartsWith("viewer/fonts/", StringComparison.Ordinal) || path.StartsWith("licenses/font-", StringComparison.Ordinal) => ChangeGroup.Fonts,
        _ when path.StartsWith("viewer/xui/", StringComparison.Ordinal) => ChangeGroup.Layouts,
        _ when path.StartsWith("host/", StringComparison.Ordinal) => ChangeGroup.Host,
        _ => ChangeGroup.Files
    };
    public static string EditsHash(ThemeProject project) => Contract.Hash(Contract.Bytes(project.Edits));
    private static byte[] Read(string directory, ProjectAsset asset) => LibraryCatalog.ReadBounded(Contract.Contained(directory, asset.AssetPath), asset.Kind == "font" ? 64L * 1024 * 1024 : asset.Kind == "image" ? 32L * 1024 * 1024 : Contract.ManifestLimit);
    private static string FileSummary(string directory, ProjectAsset asset) { var bytes = Read(directory, asset); return $"{bytes.LongLength:N0} bytes\nSHA256: {Contract.Hash(bytes)}\nOriginal: {asset.OriginalPath}"; }
    private static string Attributes(Dictionary<string, string> attributes) => string.Join('\n', attributes.OrderBy(item => item.Key, StringComparer.Ordinal).Select(item => item.Key + " = " + item.Value));
    private static (string Before, string After) XuiComparison(byte[] original, byte[] current)
    {
        var baseline = Contract.Xml(original).Root!.DescendantsAndSelf().ToArray(); var edited = Contract.Xml(current).Root!.DescendantsAndSelf().ToArray();
        var left = new List<string>(); var right = new List<string>();
        if (baseline.Length == edited.Length)
            for (var index = 0; index < baseline.Length && left.Count < 1024; ++index)
            {
                var first = baseline[index]; var second = edited[index];
                var location = index + ": " + first.Name.LocalName + ((string?)first.Attribute("name") is { } name ? " (" + name + ")" : "");
                foreach (var attribute in first.Attributes().Select(value => value.Name).Concat(second.Attributes().Select(value => value.Name)).Distinct().OrderBy(value => value.ToString(), StringComparer.Ordinal))
                {
                    var before = (string?)first.Attribute(attribute); var after = (string?)second.Attribute(attribute);
                    if (before == after) continue;
                    left.Add(location + "\n" + attribute + " = " + (before ?? "[inherited default]") + "\n"); right.Add(location + "\n" + attribute + " = " + (after ?? "[inherited default]") + "\n");
                }
                if (first.Name.LocalName == "text")
                {
                    var before = string.Concat(first.Nodes().OfType<XText>().Select(node => node.Value)).Trim(); var after = string.Concat(second.Nodes().OfType<XText>().Select(node => node.Value)).Trim();
                    if (before != after) { left.Add(location + "\nText = " + before + "\n"); right.Add(location + "\nText = " + after + "\n"); }
                }
            }
        var note = left.Count == 0 ? "No attribute/text changes identified.\n\n" : "Changed properties\n\n";
        return (Text(note + string.Join('\n', left) + "\nFull XML\n" + XmlText(original)), Text(note + string.Join('\n', right) + "\nFull XML\n" + XmlText(current)));
    }
    private static string XmlText(byte[] bytes) => Text(Contract.Xml(bytes).ToString(SaveOptions.None));
    private static string Text(byte[] bytes) => Text(Encoding.UTF8.GetString(bytes));
    private static string Text(string text) => text.Length <= DisplayLimit ? text : text[..DisplayLimit] + "\n[Display limited to32 KiB. Source bytes are unchanged.]";
}
