// SPDX-License-Identifier: LGPL-2.1-or-later
using Folderstorm.Theme;
using Folderstorm.Theme.Editor;
using System.Reflection;
using System.Windows;
using System.Windows.Controls;

internal static class WidgetRuleChecks
{
    private const BindingFlags Private = BindingFlags.Instance | BindingFlags.NonPublic;
    private static T Field<T>(object instance, string name) => (T)instance.GetType().GetField(name, Private)!.GetValue(instance)!;
    private static object? Call(object instance, string name, params object[] args) => instance.GetType().GetMethod(name, Private)!.Invoke(instance, args);
    private static void Assert(bool value, string message) { if (!value) throw new Exception(message); }
    public static void Run(MainWindow window, string directory, BaseCatalog catalog, ThemeProject project)
    {
        var workspace = Field<WidgetWorkbench>(window, "WidgetWorkspace"); workspace.SetProject(catalog, project, directory);
        var files = Field<ComboBox>(workspace, "FileChoice"); var controls = Field<ListBox>(workspace, "Controls");
        Field<TextBox>(workspace, "ControlSearch").Text = "";
        foreach (var path in XuiPolicy.Files)
        {
            files.SelectedItem = path; Assert(controls.Items.Count > 0 && Field<TextBlock>(workspace, "Provenance").Text.Contains("skins/", StringComparison.Ordinal), "Approved template failed to load: " + path);
        }
        files.SelectedItem = "xui/en/floater_test_widgets.xml";
        controls.SelectedItem = controls.Items.OfType<XuiElementInfo>().Single(item => item.Name == "test_checkbox");
        var extra = Field<ComboBox>(workspace, "ExtraProperty");
        Assert(!extra.Items.Cast<string>().Contains("image_hover_unselected"), "Button-only hover image offered on checkbox");
        controls.SelectedItem = controls.Items.OfType<XuiElementInfo>().First(item => item.Type == "floater.string");
        Assert(extra.Items.Count == 0, "Protected string parameter offered control geometry");
        controls.SelectedItem = controls.Items.OfType<XuiElementInfo>().Single(item => item.Name == "test_button");
        Assert(extra.Items.Cast<string>().Contains("font.style") && !extra.Items.Cast<string>().Contains("font_style") && !extra.Items.Cast<string>().Contains("label_color_selected_disabled"), "Ignored native spelling remains in inspector");
        extra.SelectedItem = "scale_image";
        var choices = Field<ComboBox>(workspace, "ValueChoice"); Assert(choices.Items.Cast<string>().SequenceEqual(new[] { "true", "false" }), "Visual boolean has no typed choices");
        choices.SelectedItem = "false"; Assert(Field<TextBox>(workspace, "PropertyValue").Text == "false", "Typed choice did not fill draft");
        Console.WriteLine("PASS widget template navigation and native control-specific inspector/boolean choices");

        void Apply(string name, string value)
        {
            Field<ComboBox>(workspace, "PropertyFilter").SelectedIndex = 2;
            var properties = Field<ListBox>(workspace, "Properties"); var found = properties.Items.OfType<WidgetPropertyRow>().FirstOrDefault(row => row.Property.Name == name);
            if (found != null) properties.SelectedItem = found; else extra.SelectedItem = name;
            Assert(Field<TextBlock>(workspace, "PropertyName").Text == name, "Native property missing: " + name);
            Field<TextBox>(workspace, "PropertyValue").Text = value; Call(workspace, "ApplyProperty", workspace, new RoutedEventArgs());
            Assert(Field<TextBlock>(workspace, "Status").Text == "Property applied.", "Native property failed: " + name);
        }
        Apply("label_color_disabled_selected", "#64D8B280"); Field<ComboBox>(workspace, "PreviewState").SelectedItem = "Selected disabled";
        var sample = Field<WidgetSampleData>(Field<WidgetSample>(workspace, "AfterSample"), "data"); Assert(sample.Text.Hex() == "#64D8B280", "Disabled selected sample uses an ignored color property");
        Apply("font.style", "BOLD|ITALIC"); Apply("font.size", "Huge");
        sample = Field<WidgetSampleData>(Field<WidgetSample>(workspace, "AfterSample"), "data");
        Assert(sample.FontStyle == "BOLD|ITALIC" && sample.FontSize == FontProject.Sizes(FontProject.Read(directory, project, catalog))["Huge"] * 96 / 72, "Native font subparameters did not update sample");
        files.SelectedItem = "xui/en/widgets/line_editor.xml"; Apply("background_image_focused", "Inv_FolderClosed");
        Field<ComboBox>(workspace, "PreviewState").SelectedItem = "Focused"; sample = Field<WidgetSampleData>(Field<WidgetSample>(workspace, "AfterSample"), "data");
        Assert(sample.Image != null && sample.Detail.Contains("Inv_FolderClosed", StringComparison.Ordinal), "Focused input sample ignored its native artwork binding");
        Console.WriteLine("PASS widget disabled-selected color, font subparameters and focused-input native image routing");
    }
}
