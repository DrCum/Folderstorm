// SPDX-License-Identifier: LGPL-2.1-or-later
using Folderstorm.Theme;
using Folderstorm.Theme.Editor;
using System.IO;
using System.Reflection;
using System.Windows;
using System.Windows.Controls;

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
            Assert(!window.IsVisible, "Probe unexpectedly showed a window");
            Console.WriteLine("4/4 passed. Windowless WPF handler checks only; no desktop visual acceptance. Workspace: " + directory);
            return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
}
