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

internal static class HostWorkbenchChecks
{
    private const BindingFlags Private = BindingFlags.Instance | BindingFlags.NonPublic;
    private static T Field<T>(object owner, string name) => (T)owner.GetType().GetField(name, Private)!.GetValue(owner)!;
    private static object? Call(object owner, string name, params object?[] args) => owner.GetType().GetMethod(name, Private)!.Invoke(owner, args);
    private static void Assert(bool value, string message) { if (!value) throw new Exception(message); }
    public static void Run(MainWindow window, string root, BaseCatalog catalog)
    {
        var directory = Path.Combine(root, "host-ui"); var project = ProjectStore.Create(catalog, "Host mint");
        ColorAuthoring.SetLiteral(project, catalog, "ThemeAccent", "#FF4477"); project.Undo.Clear();
        typeof(MainWindow).GetField("project", Private)!.SetValue(window, project);
        typeof(MainWindow).GetField("projectDirectory", Private)!.SetValue(window, directory);
        Call(window, "RefreshCatalog"); var host = Field<HostWorkbench>(window, "HostWorkspace");
        var preset = Field<ComboBox>(host, "Preset"); var value = Field<TextBox>(host, "ColorValue");
        var roles = Field<ListBox>(host, "Roles");
        Assert(roles.Items.Count == 7 && roles.SelectedItem is HostColorRow { Role: "accent" }, "Missing seven host roles/default accent");
        preset.SelectedIndex = 1;
        Assert(value.Text == "#6C3FA8" && HostProject.Read(directory, project) == null && project.Undo.Count == 0 && !Directory.Exists(directory), "Preset inspection wrote a host palette");
        value.Text = "#64D8B2"; Call(host, "SetColor", host, new RoutedEventArgs());
        Assert(project.Undo.Count == 1 && HostProject.Read(directory, project)?.Colors["input"] == "#F7F5FB" && value.Text == "#64D8B2", "Set lost previewed preset or failed to refresh: " + project.Undo.Count + "; " + Field<TextBlock>(host, "Status").Text + "; " + Field<TextBlock>(window, "StatusText").Text);
        Call(host, "SetColor", host, new RoutedEventArgs()); Assert(project.Undo.Count == 1, "Equivalent Set added history");
        Call(window, "Undo", window, new RoutedEventArgs()); Assert(HostProject.Read(directory, project) == null && value.Text == "#6C3FA8", "Undo left a host override/sample");
        Call(window, "Redo", window, new RoutedEventArgs()); Assert(value.Text == "#64D8B2" && PaletteProject.Read(directory, project, catalog).Inspect("ThemeAccent").Value.Hex(false) == "#FF4477", "Redo lost host/viewer separation");
        Console.WriteLine("PASS host role/preset inspection is pure; Set/no-op/Undo/Redo refresh sample and preserve independent viewer colors");

        var before = Contract.Bytes(project); value.Text = "#11223344"; Call(host, "SetColor", host, new RoutedEventArgs());
        Assert(before.SequenceEqual(Contract.Bytes(project)) && Field<TextBlock>(host, "Status").Text.Contains("opaque", StringComparison.Ordinal), "Transparent host edit changed project/history");
        Field<TextBox>(window, "PackageVersion").Text = "invalid"; before = Contract.Bytes(project); value.Text = "#335577";
        Call(host, "SetColor", host, new RoutedEventArgs()); Assert(before.SequenceEqual(Contract.Bytes(project)), "Failed compile committed pending metadata or host state");
        Call(window, "SyncMetadata");
        typeof(MainWindow).GetField("imageOperation", Private)!.SetValue(window, true);
        Call(host, "UsePreset", host, new RoutedEventArgs()); Assert(before.SequenceEqual(Contract.Bytes(project)), "Busy host handler bypassed edit guard");
        typeof(MainWindow).GetField("imageOperation", Private)!.SetValue(window, false);
        typeof(MainWindow).GetField("readOnlyReasons", Private)!.SetValue(window, new[] { "Future project" });
        Call(host, "ResetPalette", host, new RoutedEventArgs()); Assert(before.SequenceEqual(Contract.Bytes(project)), "Read-only host handler changed history");
        typeof(MainWindow).GetField("readOnlyReasons", Private)!.SetValue(window, Array.Empty<string>());
        Call(host, "ResetPalette", host, new RoutedEventArgs()); Assert(HostProject.Read(directory, project) == null && project.Edits.HostPaletteVersion == null && PaletteProject.Read(directory, project, catalog).Inspect("ThemeAccent").Value.Hex(false) == "#FF4477", "Reset left host marker or removed viewer edits");
        Call(window, "Undo", window, new RoutedEventArgs()); Assert(value.Text == "#64D8B2", "Reset Undo did not redraw host color");
        Call(host, "FromViewer", host, new RoutedEventArgs()); Assert(value.Text == "#FF4477", "Explicit viewer snapshot missed current accent");
        ColorAuthoring.SetLiteral(project, catalog, "ThemeAccent", "#AA5500"); Call(window, "RefreshDocumentEditors"); Assert(value.Text == "#FF4477", "Host snapshot changed implicitly with viewer colors");
        Call(window, "Save"); var reopened = ProjectStore.Open(directory);
        Assert(HostProject.Read(directory, reopened)!.Colors["accent"] == "#FF4477" && reopened.Undo.Count == project.Undo.Count, "Host save/reopen lost palette/history");
        var knownEdits = project.Edits; project.Edits = project.Edits with { HostPaletteVersion = 2 };
        host.SetProject(catalog, project, directory);
        Assert(value.Text.Length == 0 && Field<Border>(host, "SampleFrame").Visibility == Visibility.Collapsed, "Unsupported palette left a stale color/sample visible");
        project.Edits = knownEdits; Call(window, "RefreshDocumentEditors");
        Console.WriteLine("PASS host alpha/metadata/busy/read-only rejection, scoped Reset/Undo, explicit snapshot and durable Save");

        var picker = new ColorPickerDialog(ThemeColor.Parse("#64D8B2"), Path.Combine(root, "host-swatches.json"), (_, _) => ThemeColor.Parse("#112233"), allowOpacity: false);
        var hex = Field<TextBox>(picker, "Hex"); var alpha = Field<Slider>(picker, "Alpha");
        Assert(!alpha.IsEnabled && alpha.Value == 255 && Field<WrapPanel>(picker, "Swatches").Children.Count == 6, "Opaque picker offered transparency");
        hex.Text = "#11223380"; Assert(!Field<Button>(picker, "UseColor").IsEnabled && picker.SelectedColor.Hex() == "#64D8B2FF", "Opaque picker accepted alpha or changed prior selection");
        hex.Text = "#AABBCC"; alpha.Value = 0;
        Assert(Field<Button>(picker, "UseColor").IsEnabled && picker.SelectedColor.Hex() == "#AABBCCFF" && alpha.Value == 255, "Opaque picker lost RGB or accepted alpha slider");
        Call(picker, "PickScreen", picker, new RoutedEventArgs()); Assert(picker.SelectedColor.Hex() == "#112233FF", "Opaque screen picker did not use RGB result");
        picker.Close(); var rgba = new ColorPickerDialog(ThemeColor.Parse("#11223344"), Path.Combine(root, "rgba-swatches.json"));
        Assert(Field<Slider>(rgba, "Alpha").IsEnabled && rgba.SelectedColor.Hex() == "#11223344", "Host opacity mode changed default RGBA picker"); rgba.Close();
        Console.WriteLine("PASS shared opaque picker rejects alpha, locks opacity and accepts RGB/screen input without changing default RGBA mode");

        Field<Expander>(window, "SetupExpander").IsExpanded = false;
        var tabs = Field<TabControl>(window, "Workspace"); tabs.SelectedIndex = tabs.Items.Count - 1;
        var visual = (FrameworkElement)window.Content; window.Content = null; ((Panel)visual).Background = window.Background; TextElement.SetForeground(visual, window.Foreground);
        foreach (var (width, height) in new[] { (1280, 760), (1060, 640) })
        {
            visual.Measure(new Size(width, height)); visual.Arrange(new Rect(0, 0, width, height)); visual.UpdateLayout();
            Assert(value.ActualWidth >= 200 && Field<Border>(host, "SampleFrame").ActualWidth >= 250, "Host inspector or sample collapsed at minimum width");
            var reset = Field<Button>(host, "ResetHost"); var bottom = reset.TranslatePoint(new Point(0, reset.ActualHeight), host);
            Assert(roles.ActualHeight > 60 && bottom.Y <= host.ActualHeight, "Host roles/reset extend below the workspace instead of scrolling");
            var bitmap = new RenderTargetBitmap(width, height, 96, 96, PixelFormats.Pbgra32); bitmap.Render(visual);
            var encoder = new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(bitmap)); using var output = File.Create(Path.Combine(root, "host-workbench-" + width + ".png")); encoder.Save(output);
        }
        window.Content = visual; Assert(!window.IsVisible && !host.IsVisible, "Windowless host probe showed a desktop window");
        Console.WriteLine("PASS host narrow/wide layouts retain readable role/sample/inspector columns; renders saved without opening windows");
    }
}
