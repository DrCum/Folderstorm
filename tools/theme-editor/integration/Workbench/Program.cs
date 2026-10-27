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
        if (args.Length is < 1 or > 2 || args.Length == 2 && args[1] is not ("--fonts-only" or "--xui-only")) { Console.Error.WriteLine("Pass the matching viewer root, optionally --fonts-only or --xui-only"); return 2; }
        try
        {
            // Exercise production handlers and controls without showing windows or running the desktop/viewer.
            var app = new App(); app.InitializeComponent();
            SynchronizationContext.SetSynchronizationContext(new System.Windows.Threading.DispatcherSynchronizationContext());
            var window = new MainWindow();
            var catalog = new BaseCatalog(args[0]); var project = ProjectStore.Create(catalog, "Reset display probe");
            var directory = Path.Combine(Environment.CurrentDirectory, "artifacts", "reset-workbench-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(directory);
            Set(window, "catalog", catalog); Set(window, "project", project); Set(window, "projectDirectory", directory);
            Call(window, "RefreshCatalog");
            if (args.Length == 2 && args[1] == "--xui-only")
            {
                WidgetWorkbenchChecks.Run(window, directory, catalog, project);
                WidgetPropertyChecks.Run(window, directory, catalog, project);
                Console.WriteLine("5/5 passed. Focused windowless XUI handler checks. Workspace: " + directory); return 0;
            }
            if (args.Length == 2)
            {
                FontWorkbenchChecks.Run(window, directory, catalog, project);
                FontCollectionChecks.Run(window, directory, catalog, project);
                Assert(!window.IsVisible, "Font probe unexpectedly showed a window");
                Console.WriteLine("5/5 passed. Focused windowless font-workbench checks. Workspace: " + directory); return 0;
            }
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
            Assert(Field<TextBlock>(window, "ExternalChangeStatus").Text.Contains("Project image changed", StringComparison.Ordinal), "Stale working copy was advertised as up to date");
            var beforeConflict = Contract.Bytes(project);
            Call(window, "ReloadExternalCopy", window, new RoutedEventArgs());
            Assert(Contract.Bytes(project).SequenceEqual(beforeConflict) && Field<TextBlock>(window, "StatusText").Text.Contains("Image changed", StringComparison.Ordinal), "Stale external reload overwrote an undone image");
            Call(window, "StopExternalSessions");
            Assert(File.Exists(external.WorkingPath) && !window.CanReloadExternal, "Ending external tracking deleted the working file or left Reload active");
            Console.WriteLine("PASS external reload redraw/history, stale-edit refusal and retained working files");
            var familyImages = ImageFamilyEdit.Load(directory, project, catalog, "PushButton_Off");
            var family = new ImageFamilyDialog(null, "PushButton", familyImages, "PushButton_Off", "#64D8B280");
            T FamilyField<T>(string name) => (T)typeof(ImageFamilyDialog).GetField(name, Private)!.GetValue(family)!;
            foreach (var (name, card) in FamilyField<Dictionary<string, (CheckBox Check, Image After, TextBlock Dimensions)>>("cards"))
                if (name is not "PushButton_Off" and not "PushButton_On") card.Check.IsChecked = false;
            void FamilyPreview()
            {
                var task = (Task)typeof(ImageFamilyDialog).GetMethod("PreviewAsync", Private)!.Invoke(family, [])!;
                var frame = new System.Windows.Threading.DispatcherFrame(); var deadline = DateTime.UtcNow.AddSeconds(30);
                var timer = new System.Windows.Threading.DispatcherTimer { Interval = TimeSpan.FromMilliseconds(10) };
                timer.Tick += (_, _) => { if (task.IsCompleted || DateTime.UtcNow >= deadline) frame.Continue = false; };
                timer.Start(); if (!task.IsCompleted) System.Windows.Threading.Dispatcher.PushFrame(frame); timer.Stop();
                Assert(task.IsCompleted, "Family preview timed out"); task.GetAwaiter().GetResult();
            }
            var familyBefore = Contract.Bytes(project); FamilyPreview();
            Assert(FamilyField<Button>("apply").IsEnabled && FamilyField<FamilyResult[]>("prepared").Length == 2 && Contract.Bytes(project).SequenceEqual(familyBefore), "Family preview changed document or failed to enable Apply");
            FamilyField<TextBox>("tint").Text = "#FF669980";
            Assert(!FamilyField<Button>("apply").IsEnabled && FamilyField<FamilyResult[]>("prepared").Length == 0, "Changing family settings left stale Apply available");
            FamilyField<ComboBox>("operation").SelectedIndex = 2; FamilyField<TextBox>("width").Text = "0"; FamilyPreview();
            Assert(!FamilyField<Button>("apply").IsEnabled && Contract.Bytes(project).SequenceEqual(familyBefore), "Invalid family preview mutated project or enabled Apply");
            FamilyField<ComboBox>("operation").SelectedIndex = 0; FamilyPreview();
            var familyResults = FamilyField<FamilyResult[]>("prepared");
            var visual = (FrameworkElement)family.Content; family.Content = null;
            ((Panel)visual).Background = family.Background;
            foreach (var (logicalWidth, logicalHeight, scale) in new[] { (1060, 680, 1d), (820, 500, 1d), (1060, 680, 1.5), (1060, 680, 2d) })
            {
                visual.Measure(new Size(logicalWidth, logicalHeight)); visual.Arrange(new Rect(0, 0, logicalWidth, logicalHeight)); visual.UpdateLayout();
                var render = new RenderTargetBitmap((int)(logicalWidth * scale), (int)(logicalHeight * scale), 96 * scale, 96 * scale, System.Windows.Media.PixelFormats.Pbgra32);
                render.Render(visual); var encoder = new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(render));
                using var output = File.Create(Path.Combine(directory, $"family-layout-{logicalWidth}-{scale.ToString(System.Globalization.CultureInfo.InvariantCulture)}.png")); encoder.Save(output);
            }
            family.Close();
            Assert(Contract.Bytes(project).SequenceEqual(familyBefore), "Cancelling family dialog changed project");
            var familyHistory = project.Undo.Count; var currentBeforeFamily = Field<RasterImage>(window, "currentImage");
            Call(window, "ApplyFamilyEdits", familyResults.Select(item => item.Encode()).ToArray(), "Tint button states");
            Assert(project.Undo.Count == familyHistory + 1 && Field<RasterImage>(window, "currentImage").PixelEquals(familyResults.Single(item => item.Source.Name == "PushButton_Off").Image), "Family Apply did not redraw or created multiple checkpoints");
            Call(window, "Undo", window, new RoutedEventArgs());
            Assert(Field<RasterImage>(window, "currentImage").PixelEquals(currentBeforeFamily), "Family Undo left stale pixels");
            Call(window, "Redo", window, new RoutedEventArgs());
            Assert(Field<RasterImage>(window, "currentImage").PixelEquals(familyResults.Single(item => item.Source.Name == "PushButton_Off").Image), "Family Redo left stale pixels");
            Assert(!family.IsVisible, "Family probe unexpectedly showed a window");
            Console.WriteLine("PASS family dialog preview/settings/invalid/cancel gating and Apply/Undo/Redo redraw with one checkpoint");
            var importSource = catalog.ResolveBytes("textures/" + catalog.Textures["Inv_FolderClosed"].Attributes["file_name"]);
            var importPath = Path.Combine(directory, "input.tga"); File.WriteAllBytes(importPath, importSource);
            var expectedImport = ImageImport.Read(importPath); var beforeImport = Field<RasterImage>(window, "currentImage"); var importHistory = project.Undo.Count;
            Call(window, "ImportImageFile", importPath);
            Assert(project.Undo.Count == importHistory + 1 && Field<RasterImage>(window, "currentImage").PixelEquals(expectedImport.Image), "TGA import did not redraw or created extra checkpoints");
            var importedAsset = project.Edits.Assets.Single(item => item.Kind == "image" && item.PayloadPath.EndsWith(Contract.Hash(System.Text.Encoding.UTF8.GetBytes("PushButton_Off")) + ".png", StringComparison.Ordinal));
            Assert(importedAsset.OriginalPath.EndsWith(".tga", StringComparison.Ordinal) && File.ReadAllBytes(Path.Combine(directory, importedAsset.OriginalPath)).SequenceEqual(importSource), "TGA import handler lost original bytes");
            Call(window, "Undo", window, new RoutedEventArgs()); Assert(Field<RasterImage>(window, "currentImage").PixelEquals(beforeImport), "Import Undo left stale pixels");
            Call(window, "Redo", window, new RoutedEventArgs()); Assert(Field<RasterImage>(window, "currentImage").PixelEquals(expectedImport.Image), "Import Redo left stale pixels");
            Console.WriteLine("PASS TGA import production handler original retention, redraw and one-checkpoint Undo/Redo");
            string? legacyName = null;
            foreach (var candidate in catalog.Textures.Values.OrderBy(item => item.Name, StringComparer.Ordinal))
            {
                if (!candidate.Attributes.Keys.Any(key => key.StartsWith("scale.", StringComparison.Ordinal) || key.StartsWith("clip.", StringComparison.Ordinal)) || !candidate.Attributes.TryGetValue("file_name", out var file)) continue;
                try
                {
                    var data = catalog.ResolveBytes("textures/" + file);
                    var decodedImage = (RasterImage)codec.GetMethod("Decode", BindingFlags.Static | BindingFlags.Public)!.Invoke(null, [data, file])!;
                    try { _ = TextureGeometry.FromAttributes(decodedImage.Width, decodedImage.Height, candidate.Attributes); }
                    catch (ThemeException) { legacyName = candidate.Name; break; }
                }
                catch (Exception error) when (error is IOException or ThemeException or NotSupportedException or FormatException or TargetInvocationException) { }
            }
            Assert(legacyName != null, "Expected legacy guide fixture");
            Field<TextBox>(window, "TextureSearch").Text = legacyName!; Field<ListBox>(window, "TextureList").SelectedItem = catalog.Textures[legacyName!]; Call(window, "SelectTexture", window, null);
            Assert(window.CanRepairImageGuides && !window.CanEditImage, "Legacy guides were silently normalized or repair was unavailable");
            var beforeRepair = Field<RasterImage>(window, "currentImage"); var repairHistory = project.Undo.Count;
            Call(window, "RepairImageGuides", window, new RoutedEventArgs());
            Assert(window.CanEditImage && !window.CanRepairImageGuides && project.Undo.Count == repairHistory + 1 && Field<RasterImage>(window, "currentImage").PixelEquals(beforeRepair), "Guide repair changed artwork or failed to enable editing");
            Call(window, "Undo", window, new RoutedEventArgs()); Assert(window.CanRepairImageGuides && !window.CanEditImage && Field<RasterImage>(window, "currentImage").PixelEquals(beforeRepair), "Guide Undo did not restore inherited invalid bounds");
            Call(window, "Redo", window, new RoutedEventArgs()); Assert(window.CanEditImage && !window.CanRepairImageGuides, "Guide Redo did not restore editing");
            Field<ComboBox>(window, "ImageComparison").SelectedIndex = 1; Assert(!window.CanRepairImageGuides && !window.CanEditImage, "Original comparison allowed guide repair/editing");
            Console.WriteLine("PASS explicit legacy guide repair availability, pixel preservation and Undo/Redo; fixture=" + legacyName);
            var paletteDialog = new PaletteToolsDialog(null, project, catalog, ["ThemeAccent", "ButtonLabelColor"]);
            T PaletteField<T>(string name) => (T)typeof(PaletteToolsDialog).GetField(name, Private)!.GetValue(paletteDialog)!;
            var paletteBefore = Contract.Bytes(project);
            PaletteField<ComboBox>("preset").SelectedIndex = 2;
            PaletteField<Button>("preview").RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Assert(PaletteField<Button>("apply").IsEnabled && Contract.Bytes(project).SequenceEqual(paletteBefore), "Preset preview failed or changed the document");
            PaletteField<ComboBox>("mode").SelectedIndex = 2; PaletteField<ComboBox>("link").SelectedItem = "ThemeText";
            PaletteField<Button>("preview").RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Assert(PaletteField<Button>("apply").IsEnabled, "Bulk link preview failed");
            PaletteField<ComboBox>("link").SelectedItem = "ThemeMuted";
            Assert(!PaletteField<Button>("apply").IsEnabled, "Changing the link target left stale Apply active");
            PaletteField<ComboBox>("mode").SelectedIndex = 1;
            Assert(!PaletteField<Button>("apply").IsEnabled, "Changing palette mode left stale Apply active");
            PaletteField<TextBox>("value").Text = "#oops";
            PaletteField<Button>("preview").RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            Assert(!PaletteField<Button>("apply").IsEnabled && Contract.Bytes(project).SequenceEqual(paletteBefore), "Invalid bulk color enabled Apply or changed document");
            PaletteField<TextBox>("value").Text = "#64D8B280";
            PaletteField<Button>("preview").RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
            var paletteChanges = PaletteField<PaletteChange[]>("prepared");
            Assert(paletteChanges.Length == 2 && PaletteField<Button>("apply").IsEnabled, "Bulk color preview omitted selected colors");
            var paletteVisual = (FrameworkElement)paletteDialog.Content; paletteDialog.Content = null; ((Panel)paletteVisual).Background = paletteDialog.Background;
            foreach (var (logicalWidth, logicalHeight) in new[] { (900, 620), (760, 460) })
            {
                paletteVisual.Measure(new Size(logicalWidth, logicalHeight)); paletteVisual.Arrange(new Rect(0, 0, logicalWidth, logicalHeight)); paletteVisual.UpdateLayout();
                var render = new RenderTargetBitmap(logicalWidth, logicalHeight, 96, 96, System.Windows.Media.PixelFormats.Pbgra32); render.Render(paletteVisual);
                var encoder = new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(render));
                using var output = File.Create(Path.Combine(directory, $"palette-layout-{logicalWidth}.png")); encoder.Save(output);
            }
            paletteDialog.Close(); Assert(Contract.Bytes(project).SequenceEqual(paletteBefore), "Palette dialog Cancel changed project");
            var paletteHistory = project.Undo.Count;
            Call(window, "ApplyPaletteChanges", paletteChanges, "Bulk palette probe");
            Assert(project.Undo.Count == paletteHistory + 1 && catalog.InspectColor("ThemeAccent", project.Edits.Colors, project.Edits.ColorLinks).Value.Hex() == "#64D8B280", "Bulk palette handler failed or added extra checkpoints");
            Call(window, "Undo", window, new RoutedEventArgs()); Assert(Contract.Bytes(project.Edits).SequenceEqual(Contract.Bytes(Contract.Read<ThemeProject>(paletteBefore).Edits)), "Bulk palette Undo lost prior edits");
            Call(window, "Redo", window, new RoutedEventArgs()); Assert(catalog.InspectColor("ButtonLabelColor", project.Edits.Colors, project.Edits.ColorLinks).Value.Hex() == "#64D8B280", "Bulk palette Redo lost selected value");
            Assert(!paletteDialog.IsVisible, "Palette probe unexpectedly showed a window");
            Console.WriteLine("PASS palette preset/bulk preview, invalid/settings/cancel gating, one-checkpoint Apply and Undo/Redo");
            var beforeExtras = Contract.Bytes(project); PaletteExtrasChecks.Run(directory);
            Assert(Contract.Bytes(project).SequenceEqual(beforeExtras), "Picker extras changed the project");
            FontWorkbenchChecks.Run(window, directory, catalog, project);
            FontCollectionChecks.Run(window, directory, catalog, project);
            WidgetWorkbenchChecks.Run(window, directory, catalog, project);
            WidgetPropertyChecks.Run(window, directory, catalog, project);
            Assert(!window.IsVisible, "Probe unexpectedly showed a window");
            Console.WriteLine("28/28 passed. Windowless WPF handler checks only; no desktop visual acceptance. Workspace: " + directory);
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
}
