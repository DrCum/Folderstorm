// SPDX-License-Identifier: LGPL-2.1-or-later
using System.Xml.Linq;

namespace Folderstorm.Theme;

public sealed record XuiElementInfo(int Index, int Depth, string Type, string Name)
{ public string Label => new string(' ', Depth * 2) + Type + (Name.Length == 0 ? "" : " · " + Name); }
public sealed record XuiPropertyInfo(string Name, string Kind, string? Value, string? BaseValue, bool Editable)
{ public bool Changed => Value != BaseValue; }
public sealed record XuiChange(int Element, string Attribute, string? Value);
public sealed record XuiInspection(string Path, string BaseSource, bool IsOverride, string Sha256, XuiElementInfo[] Elements);

public static class XuiProject
{
    private static string Payload(string path) { Contract.Require(XuiPolicy.Files.Contains(path), "xui_path", path); return "viewer/" + path; }
    public static byte[] Read(string directory, ThemeProject? project, BaseCatalog catalog, string path)
    {
        var payload = Payload(path); var asset = project?.Edits.Assets.SingleOrDefault(asset => asset.PayloadPath == payload);
        return asset == null ? catalog.ResolveBytes(path) : File.ReadAllBytes(Contract.Contained(directory, asset.AssetPath));
    }
    private static XElement Element(XDocument document, int index)
    {
        var elements = document.Root!.DescendantsAndSelf().ToArray(); Contract.Require(index >= 0 && index < elements.Length, "xui_element", "Select a known control"); return elements[index];
    }
    public static XuiInspection Inspect(string directory, ThemeProject? project, BaseCatalog catalog, string path)
    {
        var bytes = Read(directory, project, catalog, path); var document = Contract.Xml(bytes);
        var elements = document.Root!.DescendantsAndSelf().Select((element, index) => new XuiElementInfo(index, element.Ancestors().Count(), element.Name.LocalName, (string?)element.Attribute("name") ?? "")).ToArray();
        return new(path, catalog.Resolve(path) ?? "", project?.Edits.Assets.Any(asset => asset.PayloadPath == Payload(path)) == true, Contract.Hash(bytes), elements);
    }
    public static XuiPropertyInfo[] Properties(string directory, ThemeProject? project, BaseCatalog catalog, string path, int index)
    {
        var current = Element(Contract.Xml(Read(directory, project, catalog, path)), index); var baseline = Element(Contract.Xml(catalog.ResolveBytes(path)), index);
        var names = current.Attributes().Select(attribute => attribute.Name.ToString()).Concat(baseline.Attributes().Select(attribute => attribute.Name.ToString())).Distinct(StringComparer.Ordinal).Order(StringComparer.Ordinal);
        var properties = names.Select(name => new XuiPropertyInfo(name, XuiPolicy.AttributeKind(name), (string?)current.Attribute(name), (string?)baseline.Attribute(name), XuiPolicy.CanEditAttribute(name))).ToList();
        if (baseline.Name.LocalName == "text") properties.Add(new("#text", "text", Text(current), Text(baseline), true));
        return properties.ToArray();
    }
    private static string Text(XElement element) => string.Concat(element.Nodes().OfType<XText>().Select(text => text.Value)).Trim();
    public static byte[] Change(byte[] bytes, byte[] baseline, BaseCatalog catalog, IEnumerable<XuiChange> changes)
    {
        var document = Contract.Xml(bytes); var original = Contract.Xml(baseline); var selected = changes.Take(1025).ToArray();
        Contract.Require(selected.Length <= 1024 && selected.Select(change => (change.Element, change.Attribute)).Distinct().Count() == selected.Length, "xui_change", "Choose up to1024 distinct properties");
        foreach (var change in selected)
        {
            Contract.Require(change.Value == null || System.Text.Encoding.UTF8.GetByteCount(change.Value) <= 4096, "xui_value", "Property value exceeds4096 UTF-8 bytes");
            var element = Element(document, change.Element); var inherited = Element(original, change.Element);
            if (change.Attribute == "#text")
            {
                Contract.Require(inherited.Name.LocalName == "text", "xui_attribute", "Text content is protected on this control");
                foreach (var node in element.Nodes().OfType<XText>().ToArray()) node.Remove();
                element.AddFirst(new XText(change.Value ?? Text(inherited))); continue;
            }
            Contract.Require(XuiPolicy.CanEditAttribute(change.Attribute), "xui_attribute", "Protected property: " + change.Attribute);
            var value = change.Value ?? (string?)inherited.Attribute(change.Attribute);
            if (value != null && XuiPolicy.AttributeKind(change.Attribute) == "color" && value.TrimStart().StartsWith('#')) value = ThemeColor.Parse(value).NativeText;
            element.SetAttributeValue(change.Attribute, value);
        }
        return Contract.XmlBytes(document);
    }
    public static bool Apply(string directory, ThemeProject project, BaseCatalog catalog, string path, IEnumerable<XuiChange> changes, string label)
        => SetXml(directory, project, catalog, path, Change(Read(directory, project, catalog, path), catalog.ResolveBytes(path), catalog, changes), label);
    public static bool SetXml(string directory, ThemeProject project, BaseCatalog catalog, string path, byte[] bytes, string label)
    {
        ProjectStore.EnsureEditable(project); var payload = Payload(path); XuiPolicy.Validate(path, bytes, catalog);
        var current = Contract.Xml(Read(directory, project, catalog, path)); var candidate = Contract.Xml(bytes); var baseline = Contract.Xml(catalog.ResolveBytes(path));
        var previous = project.Edits.Assets.SingleOrDefault(asset => asset.PayloadPath == payload); var inherited = Contract.XmlEquivalent(baseline, candidate);
        if (Contract.XmlEquivalent(current, candidate) && !(inherited && previous != null)) return false;
        var edits = Contract.Read<ProjectEdits>(Contract.Bytes(project.Edits));
        edits.Assets.RemoveAll(asset => asset.PayloadPath == payload);
        if (!inherited)
            edits.Assets.Add(ProjectAssets.Prepare(directory, payload, bytes, previous == null ? catalog.ResolveBytes(path) : null, preservedOriginal: previous?.OriginalPath));
        PackageStore.Compile(directory, project with { Edits = edits }, catalog); project.Checkpoint(label); project.Edits = edits; return true;
    }
    public static bool Reset(string directory, ThemeProject project, BaseCatalog catalog, string path)
    {
        ProjectStore.EnsureEditable(project); var payload = Payload(path); var edits = Contract.Read<ProjectEdits>(Contract.Bytes(project.Edits));
        if (edits.Assets.RemoveAll(asset => asset.PayloadPath == payload) == 0) return false;
        PackageStore.Compile(directory, project with { Edits = edits }, catalog); project.Checkpoint("Reset " + System.IO.Path.GetFileName(path)); project.Edits = edits; return true;
    }
}
