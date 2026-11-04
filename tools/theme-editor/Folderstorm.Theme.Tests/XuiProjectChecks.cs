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
        test("xui removed geometry, protected empty attributes and text byte limits stay atomic", () =>
        {
            var directory = Path.Combine(root, "xui-presence"); var project = ProjectStore.Create(catalog, "XUI presence");
            var original = XuiProject.Read(directory, project, catalog, path); var document = Contract.Xml(original);
            var button = document.Root!.Elements("button").Single(element => (string?)element.Attribute("name") == "test_button");
            button.Attribute("width")!.Remove(); Assert(XuiProject.SetXml(directory, project, catalog, path, Contract.XmlBytes(document), "Inherit geometry"), "Removed width rejected");
            var before = Contract.Bytes(project); button.SetAttributeValue("commit_callback", "");
            Reject("xui_attribute", () => XuiProject.SetXml(directory, project, catalog, path, Contract.XmlBytes(document), "Protected empty value"));
            Assert(Contract.Bytes(project).SequenceEqual(before), "Empty protected attribute changed project/history");
            var text = XuiProject.Inspect(directory, project, catalog, path).Elements.First(element => element.Type == "text");
            Assert(XuiProject.Apply(directory, project, catalog, path, [new(text.Index, "#text", new string('中', 1365))], "Text boundary"), "4095-byte text rejected");
            before = Contract.Bytes(project);
            Reject("xui_value", () => XuiProject.Apply(directory, project, catalog, path, [new(text.Index, "#text", new string('中', 1366))], "Text overflow"));
            var raw = Contract.Xml(XuiProject.Read(directory, project, catalog, path)); raw.Root!.Elements("text").First().Value = new string('A', 4097);
            Reject("xui_value", () => XuiProject.SetXml(directory, project, catalog, path, Contract.XmlBytes(raw), "Raw text overflow"));
            Assert(Contract.Bytes(project).SequenceEqual(before), "Oversized structured/raw text changed project/history");
        });
        test("xui widget matrix uses native property names, types and exact template paths", () =>
        {
            Assert(XuiPolicy.Files.Count == 27 && XuiPolicy.CanEditAttribute("button", "label_color_disabled_selected") && XuiPolicy.CanEditAttribute("button", "font.style") &&
                !XuiPolicy.CanEditAttribute("button", "font_style") && !XuiPolicy.CanEditAttribute("button", "label_color_selected_disabled") &&
                !XuiPolicy.CanEditAttribute("check_box", "image_hover_unselected") && !XuiPolicy.CanEditAttribute("floater.string", "width") && !XuiPolicy.CanEditAttribute("floater", "follows"), "Ignored or wrong-control native property became editable");
            const string template = "xui/en/widgets/button.xml"; var source = catalog.ResolveBytes(template);
            void Check(string name, string value) => XuiPolicy.Validate(template, XuiProject.Change(source, source, catalog, [new(0, name, value)]), catalog);
            Check("follows", "left|right|top"); Check("layout", "topleft"); Check("font.style", "BOLD|ITALIC"); Check("scale_image", "false"); Check("hover_glow_amount", "0.5");
            Reject("xui_value", () => Check("follows", "left|diagonal")); Reject("xui_value", () => Check("follows", "left|")); Reject("xui_value", () => Check("layout", "invalid"));
            Reject("xui_value", () => Check("scale_image", "yes")); Reject("xui_value", () => Check("hover_glow_amount", "NaN")); Reject("xui_value", () => Check("hover_glow_amount", "1.01"));
            Reject("reference", () => Check("image_color", "MissingColor")); Reject("xui_attribute", () => Check("font_style", "BOLD"));
            var directory = Path.Combine(root, "xui-matrix"); var project = ProjectStore.Create(catalog, "Widget matrix");
            foreach (var file in XuiPolicy.Files.Order(StringComparer.Ordinal))
            {
                var original = catalog.ResolveBytes(file); var changes = new List<XuiChange> { new(0, "tool_tip", "Widget matrix check") };
                if (file == template) changes.AddRange([new(0, "label_color_disabled_selected", "#64D8B280"), new(0, "image_color", "ThemeAccent"), new(0, "font.style", "BOLD|ITALIC"), new(0, "follows", "left|right|top"), new(0, "scale_image", "false"), new(0, "hover_glow_amount", "0.5")]);
                if (file == "xui/en/widgets/line_editor.xml") changes.Add(new(0, "background_image_focused", "Inv_FolderClosed"));
                if (file == "xui/en/widgets/tab_container.xml")
                {
                    var first = Contract.Xml(original).Root!.DescendantsAndSelf().Select((element, index) => (element, index)).First(item => item.element.Name.LocalName == "first_tab");
                    changes.Add(new(first.index, "tab_top_image_flash", "Inv_FolderClosed"));
                }
                var bytes = XuiProject.Change(original, original, catalog, changes); XuiPolicy.Validate(file, bytes, catalog);
                project.Edits.Assets.Add(ProjectAssets.Prepare(directory, "viewer/" + file, bytes, original));
            }
            ProjectStore.Save(directory, project); var theme = PackageStore.Compile(directory, project, catalog); var revision = ThemeLibrary.Publish(theme, Path.Combine(root, "xui-matrix-library"));
            Assert(theme.Manifest.Files.Count(file => file.Kind == "xui") == 27, "Template package dropped a supported file");
            Contract.AtomicWrite(Path.Combine(directory, "result.json"), Contract.Bytes(new { revision = theme.Revision, path = revision, policy = XuiPolicy.Fingerprint }));
        });
        test("xui preview rejects a mismatched native policy before publishing or launching", () =>
        {
            byte[] Capabilities(object version, string? hash) => Contract.Bytes(new { preview_version = version, source_commit = new string('a', 40), xui_rules_sha256 = hash });
            PreviewSession.ValidateCapabilities(Capabilities(1, XuiPolicy.Fingerprint), true);
            PreviewSession.ValidateCapabilities(Capabilities(1, null), false);
            Reject("preview_unsupported", () => PreviewSession.ValidateCapabilities(Capabilities(1, null), true));
            Reject("preview_unsupported", () => PreviewSession.ValidateCapabilities(Capabilities(1, new string('0', 64)), true));
            Reject("preview_unsupported", () => PreviewSession.ValidateCapabilities(Capabilities("1", XuiPolicy.Fingerprint), true));
            Reject("preview_unsupported", () => PreviewSession.ValidateCapabilities(Contract.Bytes(new { preview_version = 1 }), false));
        });
    }
}
