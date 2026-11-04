// SPDX-License-Identifier: LGPL-2.1-or-later
using Folderstorm.Theme;
using Folderstorm.Theme.Editor;
using System.IO;
using System.Reflection;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Documents;
using System.Windows.Media;
using System.Windows.Media.Imaging;

internal static class ProjectChangesWorkbenchChecks
{
    private const BindingFlags Private = BindingFlags.Instance | BindingFlags.NonPublic;
    private static T Field<T>(object owner, string name) => (T)owner.GetType().GetField(name, Private)!.GetValue(owner)!;
    private static object? Call(object owner, string name, params object?[] args) => owner.GetType().GetMethod(name, Private)!.Invoke(owner, args);
    private static void Assert(bool value, string message) { if (!value) throw new Exception(message); }
    public static void Run(MainWindow window, string directory, BaseCatalog catalog)
    {
        var project = ProjectStore.Create(catalog, "Change workbench");
        project.Edits.Colors["ThemeAccent"] = ThemeColor.Parse("#64D8B2").NativeText;
        XuiProject.Apply(directory, project, catalog, "xui/en/widgets/button.xml", [new(0, "label_color", "ThemeAccent")], "Label color");
        window.GetType().GetField("project", Private)!.SetValue(window, project);
        window.GetType().GetField("projectDirectory", Private)!.SetValue(window, directory); Call(window, "RefreshCatalog");
        var review = ProjectChanges.Inspect(directory, project, catalog); var original = Contract.Bytes(project);
        var dialog = new ProjectChangesDialog(null, review);
        Field<TextBox>(dialog, "search").Text = "button.xml";
        var list = Field<ListBox>(dialog, "entries");
        Assert(list.Items.Count == 1 && Field<TextBox>(dialog, "after").Text.Contains("label_color=\"ThemeAccent\"", StringComparison.Ordinal), "Review search or XML comparison failed");
        Field<ComboBox>(dialog, "group").SelectedItem = "Palette";
        Assert(list.Items.Count == 0 && !Field<Button>(dialog, "reset").IsEnabled && Field<Button>(dialog, "resetAll").IsEnabled, "Empty filter left a stale selected-reset target");
        Field<TextBox>(dialog, "search").Text = "";
        Assert(list.Items.Count == 1 && ((ProjectChange)list.SelectedItem).Name == "ThemeAccent", "Group filter omitted palette");
        var request = (ChangeReset)Call(dialog, "PrepareReset", 0)!;
        Assert(request.Entry == "color:ThemeAccent" && request.Group == null && Contract.Bytes(project).SequenceEqual(original), "Preparing reset changed state or wrong scope");
        var group = (ChangeReset)Call(dialog, "PrepareReset", 1)!; var all = (ChangeReset)Call(dialog, "PrepareReset", 2)!;
        Assert(group.Group == ChangeGroup.Palette && group.Entry == null && all.Entry == null && all.Group == null, "Group/all reset scope wrong");
        var content = (FrameworkElement)dialog.Content; dialog.Content = null; ((Panel)content).Background = dialog.Background; TextElement.SetForeground(content, dialog.Foreground);
        foreach (var (width, height) in new[] { (1120, 680), (860, 520) })
        {
            if (width == 860) Field<ComboBox>(dialog, "group").SelectedItem = "Layouts";
            content.Measure(new Size(width, height)); content.Arrange(new Rect(0, 0, width, height)); content.UpdateLayout();
            var bitmap = new RenderTargetBitmap(width, height, 96, 96, PixelFormats.Pbgra32); bitmap.Render(content);
            var encoder = new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(bitmap)); using var output = File.Create(Path.Combine(directory, "project-changes-" + width + ".png")); encoder.Save(output);
        }
        dialog.Content = content; dialog.Close();
        Assert(Contract.Bytes(project).SequenceEqual(original) && !dialog.IsVisible, "Inspecting/closing review changed project or showed a window");
        Console.WriteLine("PASS changes dialog search/group, base/current XML, empty filtering, exact reset scopes and layouts without writes");

        var count = project.Undo.Count; Call(window, "ApplyChangeReset", request);
        Assert(project.Undo.Count == count + 1 && project.Edits.Colors.Count == 0 && project.Edits.Assets.Count == 1, "Reset production handler did not preserve unrelated XML and one checkpoint");
        Call(window, "Undo", window, new RoutedEventArgs());
        Assert(project.Edits.Colors.ContainsKey("ThemeAccent") && ThemeColor.Parse(Field<TextBox>(window, "ColorValue").Text).Hex(false) == "#64D8B2", "Reset Undo did not refresh palette");
        Call(window, "Redo", window, new RoutedEventArgs());
        Assert(!project.Edits.Colors.ContainsKey("ThemeAccent") && Field<TextBlock>(window, "ColorState").Text == "Inherited", "Reset Redo left stale inspector");
        Call(window, "Save"); Assert(!ProjectStore.Open(directory).Edits.Colors.ContainsKey("ThemeAccent"), "Save lost reset state");
        Console.WriteLine("PASS changes reset handler, visible inspector refresh, one-checkpoint Undo/Redo and durable Save");

        Call(window, "Undo", window, new RoutedEventArgs()); review = ProjectChanges.Inspect(directory, project, catalog);
        var before = Contract.Bytes(project); Field<TextBox>(window, "PackageVersion").Text = "invalid";
        try { Call(window, "ApplyChangeReset", new ChangeReset(review.EditsHash)); throw new Exception("Invalid pending metadata accepted"); }
        catch (TargetInvocationException error) when (error.InnerException is ThemeException { Code: "identity" }) { }
        Assert(Contract.Bytes(project).SequenceEqual(before) && Field<TextBox>(window, "PackageVersion").Text == "invalid", "Failed reset changed document/history or discarded draft");
        Field<TextBox>(window, "PackageVersion").Text = "1.2.3";
        Call(window, "ApplyChangeReset", new ChangeReset(review.EditsHash, "color:ThemeAccent"));
        Assert(project.PackageVersion == "1.2.3" && project.Undo.Count == Contract.Read<ThemeProject>(before).Undo.Count + 2, "Valid metadata and appearance edits did not retain separate checkpoints");
        Call(window, "Undo", window, new RoutedEventArgs());
        Assert(project.Edits.Colors.ContainsKey("ThemeAccent") && project.PackageVersion == "1.2.3", "First Undo did not restore appearance while preserving separately committed details");
        Field<ComboBox>(window, "XmlFile").SelectedItem = "xui/en/widgets/button.xml"; Call(window, "LoadXml", window, new RoutedEventArgs());
        Assert(Field<TextBox>(window, "XmlEditor").Text.Contains("label_color=\"ThemeAccent\"", StringComparison.Ordinal), "Raw XML fixture was not loaded");
        review = ProjectChanges.Inspect(directory, project, catalog); Call(window, "ApplyChangeReset", new ChangeReset(review.EditsHash, Group: ChangeGroup.Layouts));
        Assert(Field<TextBox>(window, "XmlEditor").Text == System.Text.Encoding.UTF8.GetString(catalog.ResolveBytes("xui/en/widgets/button.xml")), "Layout reset left stale raw XML");
        Call(window, "Undo", window, new RoutedEventArgs());
        Assert(Field<TextBox>(window, "XmlEditor").Text.Contains("label_color=\"ThemeAccent\"", StringComparison.Ordinal), "Layout reset Undo left stale raw XML");
        Call(window, "Redo", window, new RoutedEventArgs());
        Assert(Field<TextBox>(window, "XmlEditor").Text == System.Text.Encoding.UTF8.GetString(catalog.ResolveBytes("xui/en/widgets/button.xml")), "Layout reset Redo left stale raw XML");
        var empty = new ProjectChangesDialog(null, new("empty", []));
        Assert(!Field<Button>(empty, "reset").IsEnabled && !Field<Button>(empty, "resetGroup").IsEnabled && !Field<Button>(empty, "resetAll").IsEnabled, "Empty project enabled resets"); empty.Close();
        Assert(!window.IsVisible, "Changes probe showed editor");
        Console.WriteLine("PASS invalid draft blocks reset atomically; valid details retain their own history; empty review gates actions");
    }
}
