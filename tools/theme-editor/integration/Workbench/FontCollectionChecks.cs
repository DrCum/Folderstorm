// SPDX-License-Identifier: LGPL-2.1-or-later
using Folderstorm.Theme;
using Folderstorm.Theme.Editor;
using System.IO;
using System.Reflection;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

internal static class FontCollectionChecks
{
    private const BindingFlags Private = BindingFlags.Instance | BindingFlags.NonPublic;
    private static void Assert(bool value, string message) { if (!value) throw new Exception(message); }
    private static T Field<T>(object instance, string name) => (T)instance.GetType().GetField(name, Private)!.GetValue(instance)!;
    private static object? Call(object instance, string name, params object[] args) => instance.GetType().GetMethod(name, Private)!.Invoke(instance, args);
    private static void Wait(Task task)
    {
        var frame = new DispatcherFrame(); var deadline = DateTime.UtcNow.AddSeconds(45); var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(10) };
        timer.Tick += (_, _) => { if (task.IsCompleted || DateTime.UtcNow >= deadline) frame.Continue = false; }; timer.Start();
        if (!task.IsCompleted) Dispatcher.PushFrame(frame); timer.Stop(); Assert(task.IsCompleted, "Collection inspection timed out"); task.GetAwaiter().GetResult();
    }
    private static void Render(FontWorkbench workspace, MainWindow window, string path, int width, int height, bool bottom)
    {
        var content = (FrameworkElement)workspace.Content; workspace.Content = null; ((Panel)content).Background = new SolidColorBrush(Color.FromRgb(32, 29, 42)); content.DataContext = window;
        content.Measure(new Size(width, height)); content.Arrange(new Rect(0, 0, width, height)); content.UpdateLayout(); var scroll = Field<ScrollViewer>(workspace, "BodyScroll");
        if (bottom) scroll.ScrollToEnd(); else scroll.ScrollToTop(); content.UpdateLayout(); Assert(scroll.ExtentWidth <= scroll.ViewportWidth + 1, "Collection/license editor needs horizontal scrolling");
        var bitmap = new RenderTargetBitmap(width, height, 96, 96, PixelFormats.Pbgra32); bitmap.Render(content); var encoder = new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(bitmap));
        using (var output = File.Create(path)) encoder.Save(output); workspace.Content = content;
    }
    public static void Run(MainWindow window, string directory, BaseCatalog catalog, ThemeProject project)
    {
        var workspace = Field<FontWorkbench>(window, "FontWorkspace"); workspace.SetProject(catalog, project, directory); Field<TextBox>(workspace, "Search").Text = "";
        var roles = Field<ListBox>(workspace, "Roles"); roles.SelectedItem = roles.Items.OfType<FontRole>().First(role => role.Name == "SansSerif" && role.Style.Length == 0);
        var available = Field<ComboBox>(workspace, "Available"); var collection = available.Items.OfType<FontChoice>().FirstOrDefault(choice => choice.File.Equals("msgothic.ttc", StringComparison.OrdinalIgnoreCase));
        Assert(collection != null, "The local collection fixture MSGOTHIC.TTC is unavailable");
        available.SelectedItem = collection; Field<CheckBox>(workspace, "Collection").IsChecked = true; Call(workspace, "SetFile", workspace, new RoutedEventArgs()); Wait((Task)Call(workspace, "InspectSelection")!);
        var choices = Field<ComboBox>(workspace, "FaceChoice"); Assert(choices.Items.Count >= 2 && Field<StackPanel>(workspace, "FacePanel").Visibility == Visibility.Visible, "Collection faces not listed");
        var before = Contract.Bytes(project); choices.SelectedIndex = 1; var face = (FontFaceInfo)choices.SelectedItem;
        Assert(Field<TextBlock>(workspace, "Details").Text.Contains(face.Label, StringComparison.Ordinal) && Field<TextBlock>(workspace, "FaceCoverage").Text.StartsWith("Face 1 only:", StringComparison.Ordinal), "Face selection did not update details/coverage");
        Assert(Field<TextBlock>(workspace, "Specimen").FontFamily.Source.Contains(face.Family, StringComparison.Ordinal) && Contract.Bytes(project).SequenceEqual(before), "Face inspection changed native config or missed WPF family");
        Field<TextBox>(workspace, "SampleText").Text = "Aa 中文 \U0010ffff"; Wait((Task)Call(workspace, "InspectSelection")!);
        Assert(choices.SelectedIndex == 1 && Field<TextBlock>(workspace, "FaceCoverage").Text.Contains("U+10FFFF", StringComparison.Ordinal) && Contract.Bytes(project).SequenceEqual(before), "Sample edit reset selected face or changed native configuration");
        Assert(Field<Expander>(workspace, "EmbeddedLicensePanel").Visibility == Visibility.Collapsed && !PackageStore.Compile(directory, project, catalog).Files.Keys.Any(path => path.EndsWith("msgothic.ttc", StringComparison.OrdinalIgnoreCase)), "Installed font was copied or exposed as an embedded-license target");
        var layouts = Path.Combine(directory, "font-collection-layouts"); Directory.CreateDirectory(layouts);
        Render(workspace, window, Path.Combine(layouts, "collection-1000-top.png"), 1000, 510, false);
        Console.WriteLine("PASS real TTC face selection, WPF family/details/coverage and retained inspection choice leave native config/dependencies unchanged");

        var baseConfig = FontProject.Read(directory, project, catalog); var monospace = FontProject.Roles(baseConfig).First(role => role.Name == "Monospace");
        FontProject.Embed(directory, project, catalog, monospace.Index, ".ttf", File.ReadAllBytes(Path.Combine(catalog.Root, "fonts", "InterVariableFont.ttf")), "Original packaged license\n© Maker");
        workspace.SetProject(catalog, project, directory); roles.SelectedItem = roles.Items.OfType<FontRole>().First(role => role.Name == "Monospace"); Wait((Task)Call(workspace, "InspectSelection")!);
        var license = Field<TextBox>(workspace, "PackagedLicense"); Assert(Field<Expander>(workspace, "EmbeddedLicensePanel").Visibility == Visibility.Visible && license.Text.StartsWith("Original packaged license", StringComparison.Ordinal), "Embedded license did not load");
        Assert(Field<StackPanel>(workspace, "FacePanel").Visibility == Visibility.Collapsed, "Single face left stale collection selection visible");
        var history = project.Undo.Count; license.Text = "Revised packaged license\nРусский · 中文"; Call(workspace, "SaveLicense", workspace, new RoutedEventArgs());
        Assert(project.Undo.Count == history + 1 && license.Text.StartsWith("Revised", StringComparison.Ordinal), "License save did not commit once");
        Call(window, "Undo", window, new RoutedEventArgs()); Assert(license.Text.StartsWith("Original", StringComparison.Ordinal), "License Undo did not refresh text: displayed=" + license.Text + "; selected=" + ((FontReference?)Field<ListBox>(workspace, "References").SelectedItem)?.File + "; status=" + Field<TextBlock>(window, "StatusText").Text + "; checkpoint=" + project.Undo.LastOrDefault()?.Label);
        Call(window, "Redo", window, new RoutedEventArgs()); Assert(license.Text.StartsWith("Revised", StringComparison.Ordinal), "License Redo did not refresh text");
        Call(workspace, "RemoveLicense", workspace, new RoutedEventArgs()); Assert(license.Text.Length == 0 && !Field<Button>(workspace, "RemoveLicenseButton").IsEnabled, "Removed license remained displayed");
        Call(window, "Undo", window, new RoutedEventArgs()); Assert(license.Text.StartsWith("Revised", StringComparison.Ordinal) && Field<Button>(workspace, "RemoveLicenseButton").IsEnabled, "License removal Undo lost text or commands");
        history = project.Undo.Count; Call(workspace, "SaveLicense", workspace, new RoutedEventArgs()); Assert(project.Undo.Count == history, "Unchanged license added a checkpoint"); Wait((Task)Call(workspace, "InspectSelection")!);
        foreach (var width in new[] { 1240, 1000 }) Render(workspace, window, Path.Combine(layouts, $"license-{width}-bottom.png"), width, 510, true);
        Console.WriteLine("PASS packaged-license save/no-op/remove and Undo/Redo update text/commands; full/narrow license layouts fit");
    }
}
