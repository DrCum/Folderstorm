// SPDX-License-Identifier: LGPL-2.1-or-later
using Folderstorm.Theme;
using Folderstorm.Theme.Editor;
using System.Reflection;
using System.Windows;
using System.Windows.Controls;

internal static class WidgetWorkbenchChecks
{
    private const BindingFlags Private = BindingFlags.Instance | BindingFlags.NonPublic;
    private static T Field<T>(object instance, string name) => (T)instance.GetType().GetField(name, Private)!.GetValue(instance)!;
    private static object? Call(object instance, string name, params object[] args) => instance.GetType().GetMethod(name, Private)!.Invoke(instance, args);
    private static void Assert(bool value, string message) { if (!value) throw new Exception(message); }
    public static void Run(MainWindow window, string directory, BaseCatalog catalog, ThemeProject project)
    {
        var before = Contract.Bytes(project); var beforeEdits = Contract.Bytes(project.Edits); var history = project.Undo.Count; var fonts = Field<FontWorkbench>(window, "FontWorkspace");
        var list = Field<ComboBox>(window, "XmlFile"); var editor = Field<TextBox>(window, "XmlEditor"); list.SelectedItem = "textures/textures.xml";
        editor.Text = "<textures><texture name='PushButton_Off' file_name='missing-widget-probe.png'/></textures>";
        Call(window, "SetXml", window, new RoutedEventArgs());
        Assert(ReferenceEquals(Field<ThemeProject>(window, "project"), project) && ReferenceEquals(Field<ThemeProject>(fonts, "project"), project) && Contract.Bytes(project).SequenceEqual(before), "Rejected texture XML replaced the shared document or changed history");
        Assert(Field<TextBlock>(window, "StatusText").Text.Contains("Missing image", StringComparison.Ordinal), "Rejected XML did not report its missing image");
        list.SelectedItem = "xui/en/widgets/button.xml"; Call(window, "LoadXml", window, new RoutedEventArgs());
        var xml = Contract.Xml(System.Text.Encoding.UTF8.GetBytes(editor.Text)); xml.Root!.SetAttributeValue("width", "133"); editor.Text = System.Text.Encoding.UTF8.GetString(Contract.XmlBytes(xml));
        Call(window, "SetXml", window, new RoutedEventArgs()); Assert(project.Undo.Count == history + 1 && project.Edits.Assets.Any(asset => asset.PayloadPath == "viewer/xui/en/widgets/button.xml"), "Validated raw XUI was not committed once");
        Call(window, "SetXml", window, new RoutedEventArgs()); Assert(project.Undo.Count == history + 1, "Unchanged raw XUI created history");
        Call(window, "Undo", window, new RoutedEventArgs()); Assert(Contract.Bytes(project.Edits).SequenceEqual(beforeEdits), "Raw XUI Undo did not restore prior edits/inheritance");
        Call(window, "Redo", window, new RoutedEventArgs()); Assert(project.Edits.Assets.Any(asset => asset.PayloadPath == "viewer/xui/en/widgets/button.xml") && ReferenceEquals(Field<ThemeProject>(fonts, "project"), project), "Raw XUI Redo lost the document/override");
        Console.WriteLine("PASS rejected raw texture XML retains shared document; XUI Apply/no-op/Undo/Redo uses staged validation");
    }
}
