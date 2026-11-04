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

internal static class FontWorkbenchChecks
{
    private const BindingFlags Private = BindingFlags.Instance | BindingFlags.NonPublic;
    private static void Assert(bool value, string message) { if (!value) throw new Exception(message); }
    private static T Field<T>(object instance, string name) => (T)instance.GetType().GetField(name, Private)!.GetValue(instance)!;
    private static object? Call(object instance, string name, params object[] args) => instance.GetType().GetMethod(name, Private)!.Invoke(instance, args);
    private static void Wait(Task task)
    {
        var frame = new DispatcherFrame(); var deadline = DateTime.UtcNow.AddSeconds(45); var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(10) };
        timer.Tick += (_, _) => { if (task.IsCompleted || DateTime.UtcNow >= deadline) frame.Continue = false; }; timer.Start();
        if (!task.IsCompleted) Dispatcher.PushFrame(frame); timer.Stop(); Assert(task.IsCompleted, "Font inspection timed out"); task.GetAwaiter().GetResult();
    }
    public static void Run(MainWindow window, string directory, BaseCatalog catalog, ThemeProject project)
    {
        var workspace = Field<FontWorkbench>(window, "FontWorkspace"); workspace.SetProject(catalog, project, directory);
        foreach (var filename in new[] { "InterVariableFont.ttf", "DejaVuSans.ttf" })
        {
            var path = Path.Combine(catalog.Root, "fonts", filename); var face = FontInspection.Read(File.ReadAllBytes(path))[0]; var wpf = new GlyphTypeface(new Uri(path));
            foreach (var item in wpf.CharacterToGlyphMap) Assert(face.Characters.HasGlyph(item.Key) == (item.Value != 0), "Unicode map disagrees with independent WPF reader: " + filename + " U+" + item.Key.ToString("X"));
            foreach (var scalar in new[] { 0x0378, 0x10ffff, 0x20000 }) Assert(face.Characters.HasGlyph(scalar) == (wpf.CharacterToGlyphMap.TryGetValue(scalar, out var glyph) && glyph != 0), "Missing code point disagrees with WPF reader");
        }
        var roles = Field<ListBox>(workspace, "Roles"); roles.SelectedItem = roles.Items.OfType<FontRole>().First(role => role.Name == "SansSerif" && role.Style.Length == 0);
        var references = Field<ListBox>(workspace, "References"); var selected = (FontReference)references.SelectedItem;
        var original = FontProject.Read(directory, project, catalog); var history = project.Undo.Count;
        Field<TextBox>(workspace, "Weight").Text = "650"; Field<TextBox>(workspace, "SizeDelta").Text = ".5";
        Call(workspace, "SetFile", workspace, new RoutedEventArgs());
        Assert(project.Undo.Count == history + 1 && FontProject.References(FontProject.Read(directory, project, catalog), ((FontRole)roles.SelectedItem).Index)[selected.Index].Weight == 650, "Font file UI did not apply one checkpoint");
        Call(window, "Undo", window, new RoutedEventArgs()); Assert(FontProject.Read(directory, project, catalog).SequenceEqual(original) && Field<TextBox>(workspace, "Weight").Text == "400", "Font Undo did not refresh displayed values");
        Call(window, "Redo", window, new RoutedEventArgs()); Assert(Field<TextBox>(workspace, "Weight").Text == "650", "Font Redo did not refresh values");
        var beforeInvalid = Contract.Bytes(project); Field<TextBox>(workspace, "Weight").Text = "-1"; Call(workspace, "SetFile", workspace, new RoutedEventArgs());
        Assert(Contract.Bytes(project).SequenceEqual(beforeInvalid) && Field<TextBlock>(workspace, "Status").Text.Contains("weight", StringComparison.OrdinalIgnoreCase), "Invalid font UI mutated history or lacked error");
        Field<TextBox>(workspace, "Weight").Text = "650";
        var sizeName = Field<ComboBox>(workspace, "SizeName"); sizeName.SelectedItem = sizeName.Items.OfType<string>().First();
        Field<TextBox>(workspace, "NativeSize").Text = "14.5"; Call(workspace, "SetNativeSize", workspace, new RoutedEventArgs());
        Assert(FontProject.Sizes(FontProject.Read(directory, project, catalog))[(string)sizeName.SelectedItem] == 14.5, "Native font size handler failed");
        Console.WriteLine("PASS font role/weight/size handlers validate before one checkpoint and refresh Undo/Redo");
        Wait((Task)Call(workspace, "InspectSelection")!);
        var details = Field<TextBlock>(workspace, "Details").Text; var coverage = Field<TextBlock>(workspace, "Coverage").Text;
        Assert(details.Contains("wght", StringComparison.Ordinal) && details.Contains("Redistribution", StringComparison.Ordinal) && coverage.Contains("Sample glyph mapping", StringComparison.Ordinal), "Font inspection omitted axes, license or glyph mapping");
        var beforeInspection = Contract.Bytes(project); Field<TextBox>(workspace, "SampleText").Text = "a\u0378"; Field<TextBox>(workspace, "SampleText").Text = "Aa Русский \U0010ffff"; Wait((Task)Call(workspace, "InspectSelection")!);
        Assert(Field<TextBlock>(workspace, "Coverage").Text.Contains("U+10FFFF", StringComparison.Ordinal) && !Field<TextBlock>(workspace, "Coverage").Text.Contains("U+0378", StringComparison.Ordinal) && Contract.Bytes(project).SequenceEqual(beforeInspection), "Stale/missing-glyph report changed the document or hid the latest code point");
        Field<TextBox>(workspace, "Search").Text = "not-a-real-role"; Assert(roles.Items.Count == 0 && references.Items.Count == 0 && Field<TextBlock>(workspace, "Specimen").Text.Length == 0, "Empty font search left stale file selection/sample");
        Field<TextBox>(workspace, "Search").Text = "SansSerif"; Assert(roles.Items.Count > 0, "Font role search did not recover"); Wait((Task)Call(workspace, "InspectSelection")!);
        var malformed = Contract.Xml(FontProject.Read(directory, project, catalog)); malformed.Root!.Elements("font").ElementAt(((FontRole)roles.SelectedItem).Index).Elements("file").First().SetAttributeValue("size_delta", "bad");
        typeof(FontWorkbench).GetField("config", Private)!.SetValue(workspace, Contract.XmlBytes(malformed)); Call(workspace, "ShowRole");
        Assert(references.Items.Count == 0 && Field<TextBlock>(workspace, "Status").Text.Contains("Invalid size_delta", StringComparison.Ordinal), "Malformed font value escaped the error display");
        workspace.SetProject(catalog, project, directory); Wait((Task)Call(workspace, "InspectSelection")!);
        Assert(references.Items.Count > 0 && Field<TextBlock>(workspace, "Status").Text.Length == 0, "Recovered font configuration retained a stale error");
        Console.WriteLine("PASS variable/license/missing-glyph inspection and empty role search preserve document");

        var layouts = Path.Combine(directory, "font-layouts"); Directory.CreateDirectory(layouts);
        var content = (FrameworkElement)workspace.Content; workspace.Content = null; ((Panel)content).Background = new SolidColorBrush(Color.FromRgb(32, 29, 42)); content.DataContext = window;
        foreach (var (width, height) in new[] { (1240, 610), (1000, 440) })
        {
            foreach (var bottom in new[] { false, true })
            {
                content.Measure(new Size(width, height)); content.Arrange(new Rect(0, 0, width, height)); content.UpdateLayout();
                var scroll = Field<ScrollViewer>(workspace, "BodyScroll"); if (bottom) scroll.ScrollToEnd(); else scroll.ScrollToTop(); content.UpdateLayout();
                Assert(scroll.ExtentWidth <= scroll.ViewportWidth + 1, "Font editor requires horizontal scrolling");
                var render = new RenderTargetBitmap(width, height, 96, 96, PixelFormats.Pbgra32); render.Render(content); var encoder = new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(render));
                using var output = File.Create(Path.Combine(layouts, $"fonts-{width}-{(bottom ? "details" : "top")}.png")); encoder.Save(output);
            }
        }
        workspace.Content = content; var beforeReset = FontProject.Read(directory, project, catalog); Call(workspace, "ResetFonts", workspace, new RoutedEventArgs());
        Assert(!project.Edits.Assets.Any(asset => asset.PayloadPath.StartsWith("viewer/fonts/", StringComparison.Ordinal)), "Reset fonts retained config");
        Call(window, "Undo", window, new RoutedEventArgs()); Assert(FontProject.Read(directory, project, catalog).SequenceEqual(beforeReset), "Font reset Undo lost edits");
        Console.WriteLine("PASS font workspace full/narrow renders scroll without horizontal clipping and reset Undo restores edits");
    }
}
