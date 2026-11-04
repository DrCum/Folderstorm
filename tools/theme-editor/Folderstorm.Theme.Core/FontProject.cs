// SPDX-License-Identifier: LGPL-2.1-or-later
using System.Globalization;
using System.Xml.Linq;

namespace Folderstorm.Theme;

public sealed record FontRole(int Index, string Name, string Style) { public string Label => Name + (Style.Length == 0 ? "" : " · " + Style); }
public sealed record FontReference(int Index, string File, string Scope, double SizeDelta, int? Weight, bool Collection, string Hinting, bool Bold, string Functor)
{ public string Label => File + " · " + Scope; }
public sealed record FontReferenceEdit(string File, double SizeDelta = 0, int? Weight = null, bool Collection = false, string Hinting = "force_auto", bool Bold = false);
public sealed record FontLicense(string PayloadPath, string Text, bool Exists);

public static class FontProject
{
    public const string ConfigPath = "viewer/fonts/fonts.xml";
    public static byte[] Read(string directory, ThemeProject? project, BaseCatalog catalog)
    {
        var asset = project?.Edits.Assets.SingleOrDefault(asset => asset.PayloadPath == ConfigPath);
        return File.ReadAllBytes(asset == null ? Path.Combine(catalog.Root, "fonts", "fonts.xml") : Contract.Contained(directory, asset.AssetPath));
    }
    public static FontRole[] Roles(byte[] bytes) => Document(bytes).Root!.Elements("font").Select((node, index) => new FontRole(index, (string?)node.Attribute("name") ?? "", (string?)node.Attribute("font_style") ?? "")).ToArray();
    private static XDocument Document(byte[] bytes)
    {
        var document = Contract.Xml(bytes); Contract.Require(document.Root!.Name == "fonts", "font", "Expected fonts configuration"); return document;
    }
    private static XElement Role(XDocument document, int role) { var roles = document.Root!.Elements("font").ToArray(); Contract.Require(role >= 0 && role < roles.Length, "font_role", "Select a known font role"); return roles[role]; }
    private static XElement[] Files(XElement role) => role.Descendants("file").Where(file => file.Ancestors("os").All(os => (string?)os.Attribute("name") == "Windows")).ToArray();
    private static XElement FileAt(XElement role, int index) { var files = Files(role); Contract.Require(index >= 0 && index < files.Length, "font_file", "Select a font file"); return files[index]; }
    public static FontReference[] References(byte[] bytes, int role) => Files(Role(Document(bytes), role)).Select((file, index) =>
        new FontReference(index, file.Value, (string?)file.Parent!.Attribute("name") == "Windows" ? "Windows" : "All platforms", Number(file, "size_delta", 0),
            file.Attribute("font_weight") == null ? null : (int)Number(file, "font_weight", -1), (string?)file.Attribute("load_collection") == "true",
            (string?)file.Attribute("font_hinting") ?? "force_auto", (string?)file.Attribute("flags") == "bold", (string?)file.Attribute("functor") ?? "")).ToArray();
    private static double Number(XElement file, string name, double fallback)
    {
        if (file.Attribute(name) == null) return fallback;
        Contract.Require(double.TryParse(file.Attribute(name)!.Value, NumberStyles.Float, CultureInfo.InvariantCulture, out var number) && double.IsFinite(number), "font", "Invalid " + name);
        return number;
    }
    public static Dictionary<string, double> Sizes(byte[] bytes) => Document(bytes).Root!.Elements("font_size").ToDictionary(node => (string)node.Attribute("name")!, node => Number(node, "size", 12), StringComparer.Ordinal);
    private static void EditFile(XElement file, FontReferenceEdit edit)
    {
        Contract.PathName(edit.File); Contract.Require(!edit.File.Contains('/') && PackageStore.Kind("viewer/fonts/" + edit.File) == "font", "font", "Choose a plain font filename");
        Contract.Require(double.IsFinite(edit.SizeDelta) && edit.SizeDelta is >= -64 and <= 64 && (edit.Weight == null || edit.Weight is >= 1 and <= 1000), "font", "Size adjustment must be -64..64 and weight1..1000");
        Contract.Require(edit.Hinting is "default" or "force_auto" or "no_hinting", "font", "Unknown hinting mode");
        file.Value = edit.File; file.SetAttributeValue("size_delta", edit.SizeDelta == 0 ? null : edit.SizeDelta.ToString(CultureInfo.InvariantCulture));
        file.SetAttributeValue("font_weight", edit.Weight); file.SetAttributeValue("load_collection", edit.Collection ? "true" : null);
        file.SetAttributeValue("font_hinting", edit.Hinting == "force_auto" ? null : edit.Hinting); file.SetAttributeValue("flags", edit.Bold ? "bold" : null);
    }
    public static byte[] SetReference(byte[] bytes, int role, int file, FontReferenceEdit edit)
    { var document = Document(bytes); EditFile(FileAt(Role(document, role), file), edit); return Contract.XmlBytes(document); }
    public static byte[] AddReference(byte[] bytes, int role, FontReferenceEdit edit, bool first = false)
    {
        var document = Document(bytes); var node = Role(document, role); var file = new XElement("file"); EditFile(file, edit);
        if (first) node.AddFirst(file); else { var last = node.Elements("file").LastOrDefault(); if (last == null) node.AddFirst(file); else last.AddAfterSelf(file); }
        return Contract.XmlBytes(document);
    }
    public static byte[] MoveReference(byte[] bytes, int role, int file, int direction)
    {
        Contract.Require(direction is -1 or 1, "font_file", "Move one position"); var document = Document(bytes); var node = Role(document, role); var files = Files(node);
        var source = FileAt(node, file); var target = file + direction;
        Contract.Require(target >= 0 && target < files.Length && source.Parent == files[target].Parent, "font_file", "Reorder within the same platform group");
        source.Remove(); if (direction < 0) files[target].AddBeforeSelf(source); else files[target].AddAfterSelf(source); return Contract.XmlBytes(document);
    }
    public static byte[] RemoveReference(byte[] bytes, int role, int file)
    {
        var document = Document(bytes); var node = Role(document, role); Contract.Require(Files(node).Length > 1, "font_file", "Keep at least one Windows-compatible file in this role");
        FileAt(node, file).Remove(); return Contract.XmlBytes(document);
    }
    public static byte[] SetSize(byte[] bytes, string name, double size)
    {
        Contract.Require(double.IsFinite(size) && size is >= 1 and <= 256, "font", "Font size must be1..256 points"); var document = Document(bytes);
        var node = document.Root!.Elements("font_size").SingleOrDefault(node => (string?)node.Attribute("name") == name);
        Contract.Require(node != null, "font", "Select a known size"); node.SetAttributeValue("size", size.ToString(CultureInfo.InvariantCulture)); return Contract.XmlBytes(document);
    }
    public static bool Apply(string directory, ThemeProject project, BaseCatalog catalog, byte[] config, string label, IEnumerable<ProjectAsset>? extra = null)
    {
        ProjectStore.EnsureEditable(project); var before = Read(directory, project, catalog); var additions = extra?.ToArray() ?? [];
        if (Contract.XmlEquivalent(Document(before), Document(config)) && additions.Length == 0) return false;
        var edits = Contract.Read<ProjectEdits>(Contract.Bytes(project.Edits)); var asset = ProjectAssets.Prepare(directory, ConfigPath, config, before);
        edits.Assets.RemoveAll(item => item.PayloadPath == ConfigPath); edits.Assets.Add(asset);
        foreach (var addition in additions) { edits.Assets.RemoveAll(item => item.PayloadPath == addition.PayloadPath); edits.Assets.Add(addition); }
        var candidate = project with { Edits = edits }; PackageStore.Compile(directory, candidate, catalog);
        project.Checkpoint(label); project.Edits = edits; return true;
    }
    public static bool UseConfiguration(string directory, ThemeProject project, BaseCatalog catalog, string name)
    {
        Contract.Require(catalog.FontConfigurations.Contains(name, StringComparer.Ordinal), "font", "Choose a bundled configuration");
        return Apply(directory, project, catalog, File.ReadAllBytes(Path.Combine(catalog.Root, "fonts", name)), "Use " + name);
    }
    public static bool Embed(string directory, ThemeProject project, BaseCatalog catalog, int role, string extension, byte[] bytes, string? license = null)
    {
        ProjectStore.EnsureEditable(project); Contract.Require(extension.ToLowerInvariant() is ".ttf" or ".otf" or ".ttc" or ".otc", "font", "Use an OpenType font file");
        var faces = FontInspection.Read(bytes); var name = "font-" + Contract.Hash(bytes) + extension.ToLowerInvariant();
        var config = AddReference(Read(directory, project, catalog), role, new(name, Collection: faces.Length > 1), first: true);
        var assets = new List<ProjectAsset> { ProjectAssets.Prepare(directory, "viewer/fonts/" + name, bytes) };
        if (!string.IsNullOrWhiteSpace(license)) assets.Add(ProjectAssets.Prepare(directory, "licenses/" + Path.GetFileNameWithoutExtension(name) + ".txt", System.Text.Encoding.UTF8.GetBytes(license)));
        return Apply(directory, project, catalog, config, "Embed font for " + Roles(config)[role].Label, assets);
    }
    public static bool Reset(ThemeProject project)
    {
        ProjectStore.EnsureEditable(project); bool IsFont(ProjectAsset asset) => asset.PayloadPath.StartsWith("viewer/fonts/", StringComparison.Ordinal) || asset.PayloadPath.StartsWith("licenses/font-", StringComparison.Ordinal);
        if (!project.Edits.Assets.Any(IsFont) && project.Edits.FontConfiguration == null) return false;
        project.Checkpoint("Reset fonts"); project.Edits.Assets.RemoveAll(IsFont); project.Edits.FontConfiguration = null; return true;
    }
    private static string LicensePath(string directory, ThemeProject project, string file)
    {
        Contract.PathName(file); Contract.Require(!file.Contains('/'), "font_license", "Choose an embedded font filename");
        var font = project.Edits.Assets.SingleOrDefault(asset => asset.Kind == "font" && asset.PayloadPath == "viewer/fonts/" + file);
        Contract.Require(font != null, "font_license", "Only embedded fonts have packaged licenses");
        var path = Contract.Contained(directory, font.AssetPath); Contract.Require(new FileInfo(path).Length <= 64L * 1024 * 1024, "size", "Font exceeds64 MiB");
        return "licenses/font-" + Contract.HashFile(path) + ".txt";
    }
    public static FontLicense ReadLicense(string directory, ThemeProject project, string file)
    {
        var path = LicensePath(directory, project, file); var asset = project.Edits.Assets.SingleOrDefault(asset => asset.PayloadPath == path);
        if (asset == null) return new(path, "", false);
        var stored = Contract.Contained(directory, asset.AssetPath); Contract.Require(new FileInfo(stored).Length <= Contract.ManifestLimit, "size", "License exceeds4 MiB");
        try { return new(path, new System.Text.UTF8Encoding(false, true).GetString(File.ReadAllBytes(stored)), true); }
        catch (System.Text.DecoderFallbackException) { throw new ThemeException("font_license", "This license is not UTF-8 and was kept unchanged"); }
    }
    public static bool SetLicense(string directory, ThemeProject project, BaseCatalog catalog, string file, string text)
    {
        ProjectStore.EnsureEditable(project); Contract.Require(!string.IsNullOrWhiteSpace(text), "font_license", "Enter license text, or use Remove license");
        Contract.Require(text.Length <= Contract.ManifestLimit, "size", "License exceeds4 MiB");
        var path = LicensePath(directory, project, file); var bytes = System.Text.Encoding.UTF8.GetBytes(text); var previous = project.Edits.Assets.SingleOrDefault(asset => asset.PayloadPath == path);
        if (previous != null && File.ReadAllBytes(Contract.Contained(directory, previous.AssetPath)).SequenceEqual(bytes)) return false;
        var asset = ProjectAssets.Prepare(directory, path, bytes); var edits = Contract.Read<ProjectEdits>(Contract.Bytes(project.Edits));
        edits.Assets.RemoveAll(asset => asset.PayloadPath == path); edits.Assets.Add(asset); return CommitLicense(directory, project, catalog, edits, "Save font license");
    }
    public static bool RemoveLicense(string directory, ThemeProject project, BaseCatalog catalog, string file)
    {
        ProjectStore.EnsureEditable(project); var path = LicensePath(directory, project, file); var edits = Contract.Read<ProjectEdits>(Contract.Bytes(project.Edits));
        if (edits.Assets.RemoveAll(asset => asset.PayloadPath == path) == 0) return false;
        return CommitLicense(directory, project, catalog, edits, "Remove font license");
    }
    private static bool CommitLicense(string directory, ThemeProject project, BaseCatalog catalog, ProjectEdits edits, string label)
    { PackageStore.Compile(directory, project with { Edits = edits }, catalog); project.Checkpoint(label); project.Edits = edits; return true; }
}
