// SPDX-License-Identifier: LGPL-2.1-or-later
using Folderstorm.Theme;
using Folderstorm.Theme.Editor;
using System.IO;
using System.Reflection;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;

internal static class WidgetPropertyChecks
{
    private const BindingFlags Private = BindingFlags.Instance | BindingFlags.NonPublic;
    private static T Field<T>(object instance, string name) => (T)instance.GetType().GetField(name, Private)!.GetValue(instance)!;
    private static object? Call(object instance, string name, params object[] args) => instance.GetType().GetMethod(name, Private)!.Invoke(instance, args);
    private static void Assert(bool value, string message) { if (!value) throw new Exception(message); }
    public static void Run(MainWindow window, string directory, BaseCatalog catalog, ThemeProject project)
    {
        var workspace = Field<WidgetWorkbench>(window, "WidgetWorkspace"); workspace.SetProject(catalog, project, directory);
        var files = Field<ComboBox>(workspace, "FileChoice"); files.SelectedItem = "xui/en/floater_test_widgets.xml";
        var controls = Field<ListBox>(workspace, "Controls"); controls.SelectedItem = controls.Items.OfType<XuiElementInfo>().Single(item => item.Name == "test_button");
        var properties = Field<ListBox>(workspace, "Properties"); var value = Field<TextBox>(workspace, "PropertyValue"); var filter = Field<ComboBox>(workspace, "PropertyFilter");
        void Choose(string name)
        {
            filter.SelectedIndex = 2;
            var found = properties.Items.OfType<WidgetPropertyRow>().FirstOrDefault(row => row.Property.Name == name);
            if (found != null) properties.SelectedItem = found; else Field<ComboBox>(workspace, "ExtraProperty").SelectedItem = name;
            Assert(Field<TextBlock>(workspace, "PropertyName").Text == name, "Property not available: " + name);
        }
        Choose("name"); Assert(value.IsReadOnly && !Field<Button>(workspace, "ApplyPropertyButton").IsEnabled, "Protected control name became editable");
        var before = Contract.Bytes(project); Call(workspace, "ApplyProperty", workspace, new RoutedEventArgs()); Assert(Contract.Bytes(project).SequenceEqual(before), "Protected property handler changed document");
        Choose("width"); value.Text = "-1"; Call(workspace, "ApplyProperty", workspace, new RoutedEventArgs()); Assert(Contract.Bytes(project).SequenceEqual(before) && Field<TextBlock>(workspace, "Status").Text.Contains("xui_value", StringComparison.Ordinal), "Invalid property changed document or lacked diagnostic");
        value.Text = "220"; var history = project.Undo.Count; Call(workspace, "ApplyProperty", workspace, new RoutedEventArgs());
        var current = XuiProject.Properties(directory, project, catalog, (string)files.SelectedItem, ((XuiElementInfo)controls.SelectedItem).Index);
        Assert(project.Undo.Count == history + 1 && current.Single(item => item.Name == "width").Value == "220", "Structured Apply did not commit once");
        var after = Field<WidgetSampleData>(Field<WidgetSample>(workspace, "AfterSample"), "data"); var prior = Field<WidgetSampleData>(Field<WidgetSample>(workspace, "BeforeSample"), "data");
        Assert(after.Width == 220 && prior.Width != 220 && Field<Size>(Field<WidgetSample>(workspace, "BeforeSample"), "extent") == Field<Size>(Field<WidgetSample>(workspace, "AfterSample"), "extent"), "Before/after geometry does not share a comparison scale");
        Call(workspace, "ApplyProperty", workspace, new RoutedEventArgs()); Assert(project.Undo.Count == history + 1, "Unchanged property added history");
        Call(window, "Undo", window, new RoutedEventArgs()); Assert(value.Text != "220", "Widget Undo did not refresh property value");
        Call(window, "Redo", window, new RoutedEventArgs()); Assert(value.Text == "220", "Widget Redo did not refresh value");
        Call(workspace, "ResetProperty", workspace, new RoutedEventArgs()); Assert(XuiProject.Properties(directory, project, catalog, (string)files.SelectedItem, ((XuiElementInfo)controls.SelectedItem).Index).Single(item => item.Name == "width").Changed == false, "Property reset failed");
        Console.WriteLine("PASS widget property protection/invalid gating, one-step Apply/no-op/Undo/Redo/reset and shared preview geometry");

        Choose("label_color"); Assert(Field<WrapPanel>(workspace, "PickRow").Visibility == Visibility.Visible && Field<Button>(workspace, "PickColorButton").IsEnabled, "Color picker not available on widget color field");
        Field<ComboBox>(workspace, "ValueChoice").SelectedItem = "ThemeAccent"; Assert(value.Text == "ThemeAccent", "Named color chooser did not fill draft");
        value.Text = "#64D8B280"; Call(workspace, "ApplyProperty", workspace, new RoutedEventArgs());
        after = Field<WidgetSampleData>(Field<WidgetSample>(workspace, "AfterSample"), "data"); Assert(after.Text.Hex() == "#64D8B280", "Hex widget color/opacity missed preview");
        Choose("image_pressed"); value.Text = "Inv_FolderClosed"; Call(workspace, "ApplyProperty", workspace, new RoutedEventArgs());
        Field<ComboBox>(workspace, "PreviewState").SelectedItem = "Pressed";
        after = Field<WidgetSampleData>(Field<WidgetSample>(workspace, "AfterSample"), "data"); prior = Field<WidgetSampleData>(Field<WidgetSample>(workspace, "BeforeSample"), "data");
        Assert(after.Image != null && after.Detail.Contains("Inv_FolderClosed", StringComparison.Ordinal) && !prior.Detail.Contains("Inv_FolderClosed", StringComparison.Ordinal), "Pressed-state image did not follow the selected override");
        foreach (var state in WidgetSampleSource.States) { Field<ComboBox>(workspace, "PreviewState").SelectedItem = state; Assert(Field<WidgetSampleData>(Field<WidgetSample>(workspace, "AfterSample"), "data") != null, "State sample disappeared: " + state); }
        Field<TextBox>(workspace, "ControlSearch").Text = "no-matching-control"; Assert(controls.Items.Count == 0 && !Field<Button>(workspace, "ApplyPropertyButton").IsEnabled && typeof(WidgetSample).GetField("data", Private)!.GetValue(Field<WidgetSample>(workspace, "AfterSample")) == null, "Empty search retained stale controls/sample");
        Field<TextBox>(workspace, "ControlSearch").Text = "test_button"; Assert(controls.Items.Count == 1, "Widget search did not recover");
        filter.SelectedIndex = 1; Assert(properties.Items.OfType<WidgetPropertyRow>().All(row => row.Property.Changed), "Changed-property filter includes inherited fields");
        Console.WriteLine("PASS widget color/reference drafts, hex alpha, image-state routing, all state snapshots and empty/changed filters");

        var layouts = Path.Combine(directory, "widget-layouts"); Directory.CreateDirectory(layouts); Field<ComboBox>(workspace, "PreviewState").SelectedItem = "Pressed";
        foreach (var (width, height) in new[] { (1240, 610), (1000, 440) })
        {
            var content = (FrameworkElement)workspace.Content; workspace.Content = null; ((Panel)content).Background = new SolidColorBrush(Color.FromRgb(32, 29, 42)); content.DataContext = window;
            content.Measure(new Size(width, height)); content.Arrange(new Rect(0, 0, width, height)); content.UpdateLayout();
            var scroll = Field<ScrollViewer>(workspace, "InspectorScroll"); Assert(scroll.ExtentWidth <= scroll.ViewportWidth + 1, "Widget inspector requires horizontal scrolling");
            foreach (var sample in new[] { Field<WidgetSample>(workspace, "BeforeSample"), Field<WidgetSample>(workspace, "AfterSample") }) Assert(sample.ActualHeight <= ((FrameworkElement)VisualTreeHelper.GetParent(sample)).ActualHeight + 1, "Widget preview exceeds its allocated row and is clipped");
            var bitmap = new RenderTargetBitmap(width, height, 96, 96, PixelFormats.Pbgra32); bitmap.Render(content); var encoder = new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(bitmap));
            using (var output = File.Create(Path.Combine(layouts, $"widgets-{width}.png"))) encoder.Save(output); workspace.Content = content;
        }
        var beforeReset = XuiProject.Read(directory, project, catalog, (string)files.SelectedItem); Call(workspace, "ResetFile", workspace, new RoutedEventArgs());
        Assert(!project.Edits.Assets.Any(asset => asset.PayloadPath == "viewer/" + (string)files.SelectedItem), "Widget file reset left override");
        Call(window, "Undo", window, new RoutedEventArgs()); Assert(XuiProject.Read(directory, project, catalog, (string)files.SelectedItem).SequenceEqual(beforeReset), "File reset Undo lost widget edits");
        Assert(Field<TextBlock>(workspace, "Provenance").Text.Contains("Project override", StringComparison.Ordinal), "Widget provenance did not refresh after Undo");
        Console.WriteLine("PASS widget full/narrow layouts, file reset/Undo, property refresh and source provenance");
        var colors = new[] { "#FF0000", "#00FF00", "#0000FF", "#FFFF00", "#FFFFFF", "#00FFFF", "#FF00FF", "#000000", "#FF8000" }.Select(ThemeColor.Parse).ToArray();
        var pixels = new byte[4 * 5 * 4];
        for (var y = 0; y < 5; ++y) for (var x = 0; x < 4; ++x)
        {
            var color = colors[(y < 2 ? 0 : y >= 4 ? 2 : 1) * 3 + (x < 1 ? 0 : x >= 2 ? 2 : 1)]; var offset = (y * 4 + x) * 4;
            pixels[offset] = (byte)Math.Round(color.R * 255); pixels[offset + 1] = (byte)Math.Round(color.G * 255); pixels[offset + 2] = (byte)Math.Round(color.B * 255); pixels[offset + 3] = 255;
        }
        var sliced = new WidgetSample(); sliced.SetSample(new("button", "", 54, 36, ThemeColor.Parse("#FFF"), ThemeColor.Parse("#000"), RasterImage.FromRgba(4, 5, pixels), new(1, 2, 2, 1), "Asymmetric slice fixture", false, false, "SansSerifSmall", 12, ""), new(54, 36));
        sliced.Measure(new Size(78, 60)); sliced.Arrange(new Rect(0, 0, 78, 60)); var rendered = new RenderTargetBitmap(78, 60, 96, 96, PixelFormats.Pbgra32); rendered.Render(sliced); var rgba = new byte[78 * 60 * 4]; rendered.CopyPixels(rgba, 78 * 4, 0);
        void Pixel(int x, int y, byte r, byte g, byte b) { var offset = (y * 78 + x) * 4; Assert(rgba[offset] == b && rgba[offset + 1] == g && rgba[offset + 2] == r && rgba[offset + 3] == 255, "WPF asymmetric slice moved/blended a fixed region at " + x + "," + y); }
        Pixel(12, 12, 255, 0, 0); Pixel(65, 12, 0, 0, 255); Pixel(12, 47, 255, 0, 255); Pixel(65, 47, 255, 128, 0); Pixel(13, 14, 255, 255, 255); Pixel(64, 14, 0, 255, 255); Pixel(12, 14, 255, 255, 0);
        var proof = new PngBitmapEncoder(); proof.Frames.Add(BitmapFrame.Create(rendered)); using (var output = File.Create(Path.Combine(layouts, "widget-asymmetric-slice.png"))) proof.Save(output);
        Console.WriteLine("PASS WPF widget slice rendering preserves asymmetric fixed borders and exact corner/center RGBA pixels");
    }
}
