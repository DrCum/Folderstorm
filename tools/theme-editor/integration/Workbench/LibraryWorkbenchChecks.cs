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

internal static class LibraryWorkbenchChecks
{
    private const BindingFlags Private = BindingFlags.Instance | BindingFlags.NonPublic;
    private static object? Call(object owner, string name, params object?[] args) => owner.GetType().GetMethod(name, Private)!.Invoke(owner, args);
    private static T Field<T>(object owner, string name) => (T)owner.GetType().GetField(name, Private)!.GetValue(owner)!;
    private static void Assert(bool value, string message) { if (!value) throw new Exception(message); }
    private static void Wait(Task task)
    {
        var frame = new DispatcherFrame(); var deadline = DateTime.UtcNow.AddSeconds(45); var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(10) };
        timer.Tick += (_, _) => { if (task.IsCompleted || DateTime.UtcNow >= deadline) frame.Continue = false; };
        timer.Start(); if (!task.IsCompleted) Dispatcher.PushFrame(frame); timer.Stop(); Assert(task.IsCompleted, "Library selection timed out"); task.GetAwaiter().GetResult();
    }
    private static void Render(Window window, string directory, string name, int width, int height)
    {
        var visual = (FrameworkElement)window.Content; window.Content = null; if (visual is Panel panel) panel.Background = window.Background;
        visual.Measure(new Size(width, height)); visual.Arrange(new Rect(0, 0, width, height)); visual.UpdateLayout();
        var bitmap = new RenderTargetBitmap(width, height, 96, 96, PixelFormats.Pbgra32); bitmap.Render(visual);
        var encoder = new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(bitmap)); using (var output = File.Create(Path.Combine(directory, name + ".png"))) encoder.Save(output);
        window.Content = visual;
    }
    public static void Run(MainWindow window, string directory, BaseCatalog catalog, ThemeProject project)
    {
        var codec = typeof(MainWindow).Assembly.GetType("Folderstorm.Theme.Editor.ImageCodec")!;
        var raster = RasterImage.Fill(180, 110, ThemeColor.Parse("#64D8B2"));
        var png = (byte[])codec.GetMethod("EncodePng", BindingFlags.Static | BindingFlags.Public)!.Invoke(null, [raster])!;
        var before = Contract.Bytes(project.Edits); var history = project.Undo.Count;
        Assert((bool)Call(window, "SetThumbnail", png)! && project.Undo.Count == history + 1, "Thumbnail handler did not add one checkpoint");
        Assert(Field<Image>(window, "ThumbnailImage").Source is BitmapSource { PixelWidth: 180, PixelHeight: 110 }, "Thumbnail handler did not display decoded pixels");
        Assert(!(bool)Call(window, "SetThumbnail", png)! && project.Undo.Count == history + 1, "Repeated thumbnail created history");
        Call(window, "ResetThumbnail", window, new RoutedEventArgs()); Assert(Field<Image>(window, "ThumbnailImage").Source == null && project.Undo.Count == history + 2, "Reset left thumbnail visible");
        Call(window, "Undo", window, new RoutedEventArgs()); Assert(Field<Image>(window, "ThumbnailImage").Source is BitmapSource, "Thumbnail Undo did not redraw");
        Call(window, "Redo", window, new RoutedEventArgs()); Assert(Contract.Bytes(project.Edits).SequenceEqual(before), "Thumbnail Redo lost unrelated state");
        Call(window, "Undo", window, new RoutedEventArgs());
        var unchanged = Contract.Bytes(project); var invalid = new byte[33]; new byte[] { 137, 80, 78, 71, 13, 10, 26, 10 }.CopyTo(invalid, 0); System.Text.Encoding.ASCII.GetBytes("IHDR").CopyTo(invalid, 12); invalid[19] = 1; invalid[23] = 1;
        try { Call(window, "SetThumbnail", invalid); throw new Exception("Truncated PNG decoded"); } catch (TargetInvocationException) { }
        Assert(Contract.Bytes(project).SequenceEqual(unchanged), "Failed codec changed document");
        typeof(MainWindow).GetField("previewOperation", Private)!.SetValue(window, true); Assert(!window.CanOpenDocument && !window.CanEditDocument && !window.CanUndo, "Capture allowed concurrent project changes");
        typeof(MainWindow).GetField("previewOperation", Private)!.SetValue(window, false);
        Console.WriteLine("PASS package thumbnail handlers decode before checkpoint, redraw Set/Reset/Undo/Redo and gate concurrent capture edits");

        var theme = PackageStore.Compile(directory, project, catalog); var library = Path.Combine(directory, "inspection-library"); var path = ThemeLibrary.Publish(theme, library, catalog);
        var hashes = Directory.EnumerateFiles(library, "*", SearchOption.AllDirectories).ToDictionary(file => file, Contract.HashFile);
        var previewCalls = 0; var dialog = new LibraryDialog(null, catalog.Root, (_, _) => { ++previewCalls; return Task.CompletedTask; }, library);
        Wait((Task)Call(dialog, "SelectAsync")!);
        Assert(Field<TextBlock>(dialog, "heading").Text == project.DisplayName && Field<Button>(dialog, "previewButton").IsEnabled && Field<Button>(dialog, "openButton").IsEnabled, "Validated library selection did not enable actions");
        Assert(Field<Image>(dialog, "thumbnail").Source is BitmapSource { PixelWidth: 180 }, "Library thumbnail did not decode");
        Assert(previewCalls == 0 && hashes.All(file => Contract.HashFile(file.Key) == file.Value), "Library inspection applied preview or wrote revision files");
        Wait((Task)Call(dialog, "UseSelectionAsync", false)!); Assert(previewCalls == 1 && !dialog.IsVisible, "Explicit preview callback did not run once");
        foreach (var (width, height) in new[] { (980, 690), (780, 530) }) Render(dialog, directory, "library-layout-" + width, width, height);
        Field<TextBox>(dialog, "search").Text = "no-such-theme"; Assert(Field<ListBox>(dialog, "entries").Items.Count == 0 && !Field<Button>(dialog, "openButton").IsEnabled, "Empty library search left stale selection actions");
        Field<TextBox>(dialog, "search").Text = ""; Wait((Task)Call(dialog, "SelectAsync")!);
        var previewPath = Path.Combine(path, "preview.png"); File.WriteAllText(previewPath, "corrupt");
        Wait((Task)Call(dialog, "SelectAsync")!); Assert(!Field<Button>(dialog, "openButton").IsEnabled && Field<Image>(dialog, "thumbnail").Source == null, "Corrupt revision kept usable thumbnail/actions");
        File.WriteAllBytes(previewPath, png); dialog.Close();
        Console.WriteLine("PASS library selection validates revision and pixels, inspection is read only, explicit preview and search/corrupt selection clear stale actions");
        Field<TabControl>(window, "Workspace").SelectedItem = Field<TabItem>(window, "ProjectTab");
        foreach (var (width, height) in new[] { (1240, 760), (1000, 620) }) Render(window, directory, "project-layout-" + width, width, height);
        Call(window, "Save"); var reopened = ProjectStore.Open(directory); Assert(ThumbnailProject.Read(directory, reopened)!.SequenceEqual(png), "Handler Save lost thumbnail");
        var destination = Path.Combine(directory, "compiled-copy"); PackageStore.ImportProject(destination, theme); Assert(ThumbnailProject.Read(destination, ProjectStore.Open(destination))!.SequenceEqual(png), "Compiled project import changed thumbnail bytes");
        Console.WriteLine("PASS package workspace narrow/wide render, saved thumbnail and compiled import preserve exact PNG bytes");
    }
}
