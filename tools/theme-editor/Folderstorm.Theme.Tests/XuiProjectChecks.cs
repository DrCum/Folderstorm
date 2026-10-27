// SPDX-License-Identifier: LGPL-2.1-or-later
using Folderstorm.Theme;
using System.Xml.Linq;

internal static class XuiProjectChecks
{
    public static void Register(Action<string, Action> test, string root, BaseCatalog catalog)
    {
        void Assert(bool condition, string message) { if (!condition) throw new Exception(message); }
        void Reject(string code, Action action) { try { action(); throw new Exception("Expected " + code); } catch (ThemeException error) { Assert(error.Code == code, error.Message); } }
        const string path = "xui/en/floater_test_widgets.xml";
        test("xui control/property inspection exposes provenance and preserves protected data", () =>
        {
            var directory = Path.Combine(root, "xui-inspect"); var project = ProjectStore.Create(catalog, "Widget inspection"); var before = Contract.Bytes(project);
            var inspection = XuiProject.Inspect(directory, project, catalog, path); var button = inspection.Elements.Single(element => element.Type == "button" && element.Name == "test_button");
            var properties = XuiProject.Properties(directory, project, catalog, path, button.Index);
            Assert(!inspection.IsOverride && inspection.BaseSource == catalog.Resolve(path) && inspection.Sha256 == Contract.Hash(catalog.ResolveBytes(path)) && button.Depth > 0, "Widget provenance/hierarchy changed");
            Assert(properties.Single(property => property.Name == "name") is { Editable: false, Kind: "protected" } && properties.Single(property => property.Name == "label").Editable && XuiPolicy.AttributeKind("image_pressed") == "image", "Protected or typed properties incorrect");
            Assert(Contract.Bytes(project).SequenceEqual(before) && !Directory.Exists(directory), "Inspect wrote project files/history");
            Reject("xui_path", () => XuiProject.Read(directory, project, catalog, "xui/en/floater_login.xml"));
        });
        test("xui property batches validate atomically and restore inheritance with one Undo step", () =>
        {
            var directory = Path.Combine(root, "xui-edit"); var project = ProjectStore.Create(catalog, "Widget edits"); project.Edits.Colors["ThemeAccent"] = "1 0 0 1";
            var button = XuiProject.Inspect(directory, project, catalog, path).Elements.Single(element => element.Name == "test_button");
            var changes = new[] { new XuiChange(button.Index, "label", "Pixel button · 中文"), new XuiChange(button.Index, "width", "133"), new XuiChange(button.Index, "label_color", "#64D8B280") };
            Assert(XuiProject.Apply(directory, project, catalog, path, changes, "Edit sample") && project.Undo.Count == 1, "Widget batch did not commit once");
            var rows = XuiProject.Properties(directory, project, catalog, path, button.Index);
            Assert(rows.Single(property => property.Name == "width").Value == "133" && rows.Single(property => property.Name == "label_color").Value == ThemeColor.Parse("#64D8B280").NativeText, "Widget value/hex normalization failed");
            var original = project.Edits.Assets.Single(asset => asset.Kind == "xui").OriginalPath; Assert(File.ReadAllBytes(Contract.Contained(directory, original)).SequenceEqual(catalog.ResolveBytes(path)), "Original widget bytes changed");
            Assert(!XuiProject.Apply(directory, project, catalog, path, changes, "No-op") && project.Undo.Count == 1, "Widget no-op created history");
            var before = Contract.Bytes(project); Reject("xui_attribute", () => XuiProject.Apply(directory, project, catalog, path, [new(button.Index, "label", "Must roll back"), new(button.Index, "name", "changed")], "Invalid"));
            Assert(Contract.Bytes(project).SequenceEqual(before), "Protected-property batch changed project/history");
            Reject("xui_value", () => XuiProject.Apply(directory, project, catalog, path, [new(button.Index, "width", "-1")], "Invalid width"));
            Reject("reference", () => XuiProject.Apply(directory, project, catalog, path, [new(button.Index, "image_pressed", "MissingImage")], "Invalid reference"));
            Reject("xui_change", () => XuiProject.Apply(directory, project, catalog, path, [new(button.Index, "width", "1"), new(button.Index, "width", "2")], "Duplicate"));
            Assert(Contract.Bytes(project).SequenceEqual(before), "Invalid widget values changed project/history");
            ProjectStore.Save(directory, project); var reopened = ProjectStore.Open(directory); Assert(XuiProject.Read(directory, reopened, catalog, path).SequenceEqual(XuiProject.Read(directory, project, catalog, path)), "Widget save/reopen changed effective bytes");
            Assert(XuiProject.Apply(directory, reopened, catalog, path, changes.Select(change => change with { Value = null }), "Restore properties") && !reopened.Edits.Assets.Any(asset => asset.Kind == "xui") && reopened.Edits.Colors.Count == 1, "Property inheritance reset retained override or lost other edits");
            Assert(reopened.UndoEdit() && XuiProject.Inspect(directory, reopened, catalog, path).IsOverride, "Widget reset Undo failed");
            var theme = PackageStore.Compile(directory, reopened, catalog); var revision = ThemeLibrary.Publish(theme, Path.Combine(root, "xui-library"));
            Contract.AtomicWrite(Path.Combine(directory, "result.json"), Contract.Bytes(new { revision = theme.Revision, path = revision }));
        });
        test("xui text edits, raw replacement and reset retain required controls and earlier assets", () =>
        {
            var directory = Path.Combine(root, "xui-text"); var project = ProjectStore.Create(catalog, "Widget text");
            var inspection = XuiProject.Inspect(directory, project, catalog, path); var text = inspection.Elements.First(element => element.Type == "text");
            Assert(XuiProject.Apply(directory, project, catalog, path, [new(text.Index, "#text", "Text < & > · Русский")], "Edit text"), "Text edit failed");
            Assert(XuiProject.Properties(directory, project, catalog, path, text.Index).Single(property => property.Name == "#text").Value == "Text < & > · Русский", "Text escaping changed content");
            var before = Contract.Bytes(project); var invalid = Contract.Xml(XuiProject.Read(directory, project, catalog, path)); invalid.Root!.Add(new XElement("button", new XAttribute("name", "extra")));
            Reject("xui_structure", () => XuiProject.SetXml(directory, project, catalog, path, Contract.XmlBytes(invalid), "Invalid raw XML")); Assert(Contract.Bytes(project).SequenceEqual(before), "Raw structural edit mutated history");
            Assert(XuiProject.Reset(directory, project, catalog, path) && project.Edits.Assets.Count == 0 && !XuiProject.Reset(directory, project, catalog, path) && project.UndoEdit(), "File reset/no-op/Undo failed");
            Assert(XuiProject.Inspect(directory, project, catalog, path).Elements.Select(element => (element.Type, element.Name)).SequenceEqual(inspection.Elements.Select(element => (element.Type, element.Name))), "Control names/tree changed");
        });
        test("xui Unicode attribute limits match the native UTF-8 byte budget", () =>
        {
            var directory = Path.Combine(root, "xui-byte-boundary"); var project = ProjectStore.Create(catalog, "Unicode byte boundary");
            var button = XuiProject.Inspect(directory, project, catalog, path).Elements.Single(element => element.Name == "test_button");
            var allowed = new string('中', 1365); Assert(System.Text.Encoding.UTF8.GetByteCount(allowed) == 4095, "Boundary fixture size");
            Assert(XuiProject.Apply(directory, project, catalog, path, [new(button.Index, "label", allowed)], "CJK label"), "Native-size CJK label rejected");
            var before = Contract.Bytes(project); Reject("xui_value", () => XuiProject.Apply(directory, project, catalog, path, [new(button.Index, "label", new string('中', 1366))], "Too large"));
            Assert(Contract.Bytes(project).SequenceEqual(before), "Oversized UTF-8 attribute changed history");
            var theme = PackageStore.Compile(directory, project, catalog); var revision = ThemeLibrary.Publish(theme, Path.Combine(root, "xui-byte-library"));
            Contract.AtomicWrite(Path.Combine(directory, "result.json"), Contract.Bytes(new { revision = theme.Revision, path = revision }));
        });
    }
}
