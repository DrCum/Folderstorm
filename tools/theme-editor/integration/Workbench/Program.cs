// SPDX-License-Identifier: LGPL-2.1-or-later
using Folderstorm.Theme;
using Folderstorm.Theme.Editor;
using System.IO;
using System.Reflection;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media.Imaging;

internal static class Program
{
    private static readonly BindingFlags Private = BindingFlags.Instance | BindingFlags.NonPublic;
    private static object? Call(MainWindow window, string method, params object?[] args)
        => typeof(MainWindow).GetMethod(method, Private)!.Invoke(window, args);
    private static T Field<T>(MainWindow window, string name) => (T)typeof(MainWindow).GetField(name, Private)!.GetValue(window)!;
    private static void Set(MainWindow window, string name, object value) => typeof(MainWindow).GetField(name, Private)!.SetValue(window, value);
    private static void Assert(bool condition, string message) { if (!condition) throw new Exception(message); }

    [STAThread]
    private static int Main(string[] args)
    {
        if (args.Length != 1) { Console.Error.WriteLine("Pass the matching viewer root"); return 2; }
        try
        {
            // Exercise production handlers and controls without showing windows or running the desktop/viewer.
            var app = new App(); app.InitializeComponent();
            var window = new MainWindow();
            var catalog = new BaseCatalog(args[0]); var project = ProjectStore.Create(catalog, "Reset display probe");
            var directory = Path.Combine(Environment.CurrentDirectory, "artifacts", "reset-workbench-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(directory);
            Set(window, "catalog", catalog); Set(window, "project", project); Set(window, "projectDirectory", directory);
            Call(window, "RefreshCatalog");
            var texture = catalog.Textures["PushButton_Off"];
            Field<ListBox>(window, "TextureList").SelectedItem = texture;
            var inherited = Field<RasterImage>(window, "currentImage");
            var geometry = TextureGeometry.FromAttributes(inherited.Width, inherited.Height, texture.Attributes);
            var edited = RasterImage.Fill(inherited.Width, inherited.Height, ThemeColor.Parse("#64D8B280"));
            var codec = typeof(MainWindow).Assembly.GetType("Folderstorm.Theme.Editor.ImageCodec")!;
            var png = (byte[])codec.GetMethod("EncodePng", BindingFlags.Static | BindingFlags.Public)!.Invoke(null, [edited])!;
            void ApplyEdit()
            {
                ImageProject.SetPng(directory, project, catalog, texture.Name, png, geometry, new("solid-fill", new() { ["color"] = System.Text.Json.JsonSerializer.SerializeToElement("#64D8B280") }));
                Call(window, "SelectTexture", window, null);
                Assert(Field<RasterImage>(window, "currentImage").ToRgba().SequenceEqual(edited.ToRgba()), "Fixture did not display edited pixels");
            }
            void CheckReset(string handler)
            {
                ApplyEdit(); var history = project.Undo.Count;
                Call(window, handler, window, new RoutedEventArgs());
                Assert(!project.Edits.Recipes.ContainsKey(texture.Name) && project.Undo.Count == history + 1, handler + " did not reset the model");
                Assert(Field<RasterImage>(window, "currentImage").ToRgba().SequenceEqual(inherited.ToRgba()), handler + " left edited pixels in the displayed image");
                var canvas = Field<ImageCanvas>(window, "EditCanvas");
                var drawn = (RasterImage)typeof(ImageCanvas).GetField("image", Private)!.GetValue(canvas)!;
                Assert(drawn.ToRgba().SequenceEqual(inherited.ToRgba()), handler + " left stale canvas pixels");
                Assert(Field<TextBlock>(window, "ImageRecipeSummary").Text == "Inherited image", handler + " left stale recipe text");
                Call(window, "Undo", window, new RoutedEventArgs());
                Assert(Field<RasterImage>(window, "currentImage").ToRgba().SequenceEqual(edited.ToRgba()), handler + " Undo did not redraw the edit");
                Call(window, "Redo", window, new RoutedEventArgs());
                Assert(Field<RasterImage>(window, "currentImage").ToRgba().SequenceEqual(inherited.ToRgba()), handler + " Redo did not redraw inheritance");
                Console.WriteLine("PASS " + handler + " reloads selected image/canvas/recipe and Undo/Redo");
            }
            CheckReset("ResetSelectedImage"); CheckReset("ResetSelectedFamily"); CheckReset("ResetAllImages");
            ApplyEdit(); Field<CheckBox>(window, "ImageOverridesOnly").IsChecked = true;
            Call(window, "ResetSelectedImage", window, new RoutedEventArgs());
            Assert(Field<ListBox>(window, "TextureList").Items.Count == 0 && Field<TextBlock>(window, "TextureName").Text == "No matching images", "Reset left stale override search results");
            Call(window, "Undo", window, new RoutedEventArgs());
            Assert(Field<ListBox>(window, "TextureList").Items.Count == 1 && Field<RasterImage>(window, "currentImage").ToRgba().SequenceEqual(edited.ToRgba()), "Undo did not refill the override list and image");
            Console.WriteLine("PASS reset/Undo refreshes override-filtered results and canvas");
            Field<CheckBox>(window, "ImageOverridesOnly").IsChecked = false;
            var drawingSource = Field<RasterImage>(window, "currentImage"); var drawingHistory = project.Undo.Count;
            var stroke = new PixelStroke(drawingSource, PixelTool.Line, new(1, 1), ThemeColor.Parse("#EF1234FF")); stroke.Move(new(7, 7));
            Call(window, "CommitDrawing", stroke.Preview, stroke.Recipe);
            Assert(project.Undo.Count == drawingHistory + 1 && Field<RasterImage>(window, "currentImage").PixelEquals(stroke.Preview), "Drawing handler did not store/redraw exactly one gesture");
            Call(window, "Undo", window, new RoutedEventArgs());
            Assert(Field<RasterImage>(window, "currentImage").PixelEquals(drawingSource), "Drawing Undo did not restore the prior pixels");
            Call(window, "Redo", window, new RoutedEventArgs());
            Assert(Field<RasterImage>(window, "currentImage").PixelEquals(stroke.Preview), "Drawing Redo did not restore the new pixels");
            Console.WriteLine("PASS drawing commit redraws exact pixels with one checkpoint and Undo/Redo");
            var picker = new ColorPickerDialog(ThemeColor.Parse("#11223344"));
            T PickerField<T>(string name) => (T)typeof(ColorPickerDialog).GetField(name, Private)!.GetValue(picker)!;
            var hex = PickerField<TextBox>("Hex"); var alpha = PickerField<Slider>("Alpha");
            Assert(hex.Text == "#11223344" && alpha.Value == 68, "Picker lost its initial color/alpha");
            hex.Text = "#FF000080"; PickerField<Slider>("Hue").Value = 180;
            Assert(picker.SelectedColor.Hex() == "#00FFFF80" && alpha.Value == 128, "Picker hue selection changed alpha or displayed channels");
            hex.Text = "#Oops"; Assert(!PickerField<Button>("UseColor").IsEnabled, "Invalid hex enabled Use color");
            hex.Text = "#64D8B200"; Assert(picker.SelectedColor.Hex() == "#64D8B200" && PickerField<Button>("UseColor").IsEnabled, "Picker lost transparent RGB or did not recover valid hex");
            Assert(!picker.IsVisible, "Probe unexpectedly showed the picker");
            Console.WriteLine("PASS picker initial/edited RGBA, hue, transparency and invalid input gating");
            var drawingCanvas = Field<ImageCanvas>(window, "EditCanvas"); var drawingCommits = 0;
            drawingCanvas.DrawingCommitted += (_, _) => ++drawingCommits;
            void CancelGesture(string handler, RoutedEventArgs eventArgs)
            {
                var original = Field<RasterImage>(window, "currentImage"); var document = Contract.Bytes(project);
                var pending = new PixelStroke(original, PixelTool.Pen, new(0, 0), ThemeColor.Parse("#00AACCFF")); pending.Move(new(5, 0));
                Assert(pending.HasChanges, "Cancellation fixture did not change pixels");
                drawingCanvas.SetImage(pending.Preview, geometry);
                typeof(ImageCanvas).GetField("stroke", Private)!.SetValue(drawingCanvas, pending);
                Call(window, "UpdateCommandState");
                Assert(drawingCanvas.IsInteracting && !window.CanEditDocument && !window.CanOpenDocument && !window.CanUndo, "Pending gesture allowed document changes");
                typeof(ImageCanvas).GetMethod(handler, Private)!.Invoke(drawingCanvas, [eventArgs]);
                var restored = (RasterImage)typeof(ImageCanvas).GetField("image", Private)!.GetValue(drawingCanvas)!;
                var bitmap = (BitmapSource)typeof(ImageCanvas).GetField("bitmap", Private)!.GetValue(drawingCanvas)!;
                var pixels = new byte[original.Width * original.Height * 4]; bitmap.CopyPixels(pixels, original.Width * 4, 0);
                for (var index = 0; index < pixels.Length; index += 4) (pixels[index], pixels[index + 2]) = (pixels[index + 2], pixels[index]);
                Assert(!drawingCanvas.IsInteracting && restored.PixelEquals(original) && pixels.SequenceEqual(original.ToRgba()), handler + " left preview pixels/bitmap on the canvas");
                Assert(drawingCommits == 0 && Contract.Bytes(project).SequenceEqual(document) && window.CanEditDocument && window.CanOpenDocument, handler + " changed history or failed to restore commands");
                Console.WriteLine("PASS " + handler + " cancels pending drawing, restores bitmap/commands and leaves history unchanged");
            }
            using (var keySource = new HwndSource(new HwndSourceParameters("Hidden cancellation probe") { Width = 1, Height = 1, WindowStyle = 0 }))
                CancelGesture("OnKeyDown", new KeyEventArgs(Keyboard.PrimaryDevice, keySource, 0, Key.Escape) { RoutedEvent = Keyboard.KeyDownEvent });
            CancelGesture("OnLostMouseCapture", new MouseEventArgs(Mouse.PrimaryDevice, 0));
            // Keep working files outside the project, as in the real app.
            Set(window, "externalWorkRoot", directory + "-working");
            var external = (ExternalImageSession)Call(window, "PrepareExternalCopy")!;
            var beforeExternal = Field<RasterImage>(window, "currentImage"); var externalHistory = project.Undo.Count;
            var externalImage = RasterImage.Fill(beforeExternal.Width, beforeExternal.Height, ThemeColor.Parse("#3366CC80"));
            var externalPng = (byte[])codec.GetMethod("EncodePng", BindingFlags.Static | BindingFlags.Public)!.Invoke(null, [externalImage])!;
            Contract.AtomicWrite(external.WorkingPath, externalPng);
            Call(window, "ReloadExternalCopy", window, new RoutedEventArgs());
            Assert(project.Undo.Count == externalHistory + 1 && Field<RasterImage>(window, "currentImage").PixelEquals(externalImage), "External reload handler did not redraw exactly one imported edit");
            Call(window, "Undo", window, new RoutedEventArgs());
            Assert(Field<RasterImage>(window, "currentImage").PixelEquals(beforeExternal), "External Undo did not restore previous pixels");
            var beforeConflict = Contract.Bytes(project);
            Call(window, "ReloadExternalCopy", window, new RoutedEventArgs());
            Assert(Contract.Bytes(project).SequenceEqual(beforeConflict) && Field<TextBlock>(window, "StatusText").Text.Contains("Image changed", StringComparison.Ordinal), "Stale external reload overwrote an undone image");
            Call(window, "StopExternalSessions");
            Assert(File.Exists(external.WorkingPath) && !window.CanReloadExternal, "Ending external tracking deleted the working file or left Reload active");
            Console.WriteLine("PASS external reload redraw/history, stale-edit refusal and retained working files");
            Assert(!window.IsVisible, "Probe unexpectedly showed a window");
            Console.WriteLine("9/9 passed. Windowless WPF handler checks only; no desktop visual acceptance. Workspace: " + directory);
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
}
