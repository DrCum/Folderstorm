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
using System.Windows.Threading;

internal static class CompiledPaletteWorkbenchChecks
{
    private const BindingFlags Private = BindingFlags.Instance | BindingFlags.NonPublic;
    private static T Field<T>(object owner, string name) => (T)owner.GetType().GetField(name, Private)!.GetValue(owner)!;
    private static object? Call(object owner, string name, params object?[] args) => owner.GetType().GetMethod(name, Private)!.Invoke(owner, args);
    private static void Assert(bool value, string message) { if (!value) throw new Exception(message); }
    private static void Wait(Task task)
    {
        var frame = new DispatcherFrame(); var deadline = DateTime.UtcNow.AddSeconds(45); var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(10) };
        timer.Tick += (_, _) => { if (task.IsCompleted || DateTime.UtcNow >= deadline) frame.Continue = false; };
        timer.Start(); if (!task.IsCompleted) Dispatcher.PushFrame(frame); timer.Stop(); Assert(task.IsCompleted, "Compiled library selection timed out"); task.GetAwaiter().GetResult();
    }
    public static void Run(MainWindow window, string directory, BaseCatalog catalog)
    {
        var source = Path.Combine(directory, "palette-source"); var project = ProjectStore.Create(catalog, "Imported mint");
        ColorAuthoring.SetLiteral(project, catalog, "ThemeAccent", "#64D8B2");
        var input = PackageStore.Compile(source, project, catalog); var destination = Path.Combine(directory, "palette-import");
        project = PackageStore.ImportProject(destination, input, catalog);
        typeof(MainWindow).GetField("project", Private)!.SetValue(window, project); typeof(MainWindow).GetField("projectDirectory", Private)!.SetValue(window, destination);
        Call(window, "RefreshCatalog");
        var list = Field<ListBox>(window, "ColorList"); list.SelectedItem = list.Items.Cast<ColorRow>().Single(row => row.Name == "ThemeAccent");
        Assert(Field<TextBlock>(window, "ColorState").Text == "Imported override" && ThemeColor.Parse(Field<TextBox>(window, "ColorValue").Text).Hex(false) == "#64D8B2", "Imported inspector value/provenance missing");
        Assert(Field<TextBlock>(window, "ProjectSummary").Text.StartsWith("30 palette overrides", StringComparison.Ordinal), "Project summary missed imported palette overrides: " + Field<TextBlock>(window, "ProjectSummary").Text);
        var beforeSample = Field<PaletteSample>(window, "BeforeSample"); var afterSample = Field<PaletteSample>(window, "AfterSample");
        Assert(Field<IReadOnlyDictionary<string, ThemeColor>>(afterSample, "colors")["ThemeAccent"].Hex(false) == "#64D8B2" && Field<IReadOnlyDictionary<string, ThemeColor>>(beforeSample, "colors")["ThemeAccent"].Hex(false) != "#64D8B2", "Before/after palette ignored imported colors");
        Field<TextBox>(window, "ColorValue").Text = "#FF0000"; Call(window, "SetColor", window, new RoutedEventArgs());
        Assert(project.Undo.Count == 1 && Field<TextBlock>(window, "ColorState").Text == "Override", "Imported Set handler lost one-step history");
        Call(window, "Undo", window, new RoutedEventArgs()); Assert(Field<TextBlock>(window, "ColorState").Text == "Imported override", "Undo did not restore imported provenance");
        Call(window, "Redo", window, new RoutedEventArgs()); Assert(ThemeColor.Parse(Field<TextBox>(window, "ColorValue").Text).Hex(false) == "#FF0000", "Redo did not restore authored color");
        Call(window, "ResetColor", window, new RoutedEventArgs()); Assert(Field<TextBlock>(window, "ColorState").Text == "Inherited" && PaletteProject.Read(destination, project, catalog).Imported.ContainsKey("AccentColor"), "Selected reset removed unrelated imported aliases");
        Call(window, "Undo", window, new RoutedEventArgs()); var before = Contract.Bytes(project);
        Field<TextBox>(window, "ColorValue").Text = "not-a-color"; Call(window, "SetColor", window, new RoutedEventArgs()); Assert(Contract.Bytes(project).SequenceEqual(before), "Rejected inspector color changed project/history");
        Call(window, "ResetPalette", window, new RoutedEventArgs()); Assert(!project.Edits.Assets.Any(asset => asset.PayloadPath == PaletteProject.PayloadPath) && project.Edits.CompiledManifestPath != null, "Palette reset lost manifest anchor or retained raw override");
        Call(window, "Undo", window, new RoutedEventArgs()); Call(window, "Save"); Assert(ProjectStore.Open(destination).Edits.CompiledManifestPath == project.Edits.CompiledManifestPath, "Save lost imported manifest provenance");
        Console.WriteLine("PASS compiled palette inspector, before/after, Set/Undo/Redo/scoped reset/reset-all and rejected edit preserve imported bytes/history");

        Call(window, "Undo", window, new RoutedEventArgs());
        var dialog = new PaletteToolsDialog(null, project, catalog, ["ThemeAccent"], destination);
        Field<ComboBox>(dialog, "mode").SelectedIndex = 1; Field<TextBox>(dialog, "value").Text = "#FF4455"; before = Contract.Bytes(project);
        var files = Directory.EnumerateFiles(destination, "*", SearchOption.AllDirectories).ToDictionary(path => path, Contract.HashFile);
        Field<Button>(dialog, "preview").RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        Assert(Field<Button>(dialog, "apply").IsEnabled && Field<TextBlock>(dialog, "changesText").Text.Contains("#64D8B2", StringComparison.Ordinal) && Contract.Bytes(project).SequenceEqual(before) && files.All(file => Contract.HashFile(file.Key) == file.Value), "Bulk preview ignored imported before values or mutated data");
        var prepared = Field<PaletteChange[]>(dialog, "prepared"); dialog.Close(); var history = project.Undo.Count;
        Call(window, "ApplyPaletteChanges", prepared, "Imported bulk edit"); Assert(project.Undo.Count == history + 1 && PaletteProject.Read(destination, project, catalog).Inspect("ThemeAccent").Value.Hex(false) == "#FF4455", "Bulk imported Apply failed");
        Call(window, "Undo", window, new RoutedEventArgs());
        Field<TabControl>(window, "Workspace").SelectedIndex = 0; Field<Expander>(window, "SetupExpander").IsExpanded = false;
        var visual = (FrameworkElement)window.Content; window.Content = null; ((Panel)visual).Background = window.Background; TextElement.SetForeground(visual, window.Foreground);
        foreach (var (width, height) in new[] { (1240, 760), (1000, 620) })
        {
            visual.Measure(new Size(width, height)); visual.Arrange(new Rect(0, 0, width, height)); visual.UpdateLayout(); var bitmap = new RenderTargetBitmap(width, height, 96, 96, PixelFormats.Pbgra32); bitmap.Render(visual);
            var encoder = new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(bitmap)); using var output = File.Create(Path.Combine(directory, "compiled-palette-layout-" + width + ".png")); encoder.Save(output);
        }
        window.Content = visual;
        Console.WriteLine("PASS compiled bulk palette preview uses imported before values without writes; Apply/Undo and narrow/wide layout checks");

        XuiProject.Apply(destination, project, catalog, "xui/en/widgets/button.xml", [new(0, "label_color", "ThemeAccent")], "Use imported label color");
        var sample = WidgetSampleSource.Read(destination, project, catalog, "xui/en/widgets/button.xml", 0, "Normal");
        Assert(sample.Text.Hex(false) == "#64D8B2", "Widget sample did not use imported palette");
        var archive = Path.Combine(directory, "compiled-widget.fstheme"); PackageStore.Export(archive, PackageStore.Compile(destination, project, catalog));
        var library = new LibraryDialog(null, catalog.Root, (_, _) => Task.CompletedTask, Path.Combine(directory, "empty-library"));
        Call(library, "AddArchive", archive); Wait((Task)Call(library, "SelectAsync")!);
        Assert(Field<Button>(library, "openButton").IsEnabled && Field<TextBlock>(library, "heading").Text == "Imported mint", "XUI archive did not validate through production browser path");
        library.Close(); Assert(!window.IsVisible, "Compiled probe showed a desktop window");
        Console.WriteLine("PASS widget sample resolves imported colors and actual library archive path selects base before validating XUI");
    }
}
