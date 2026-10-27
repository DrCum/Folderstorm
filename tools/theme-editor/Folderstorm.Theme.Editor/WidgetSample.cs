// SPDX-License-Identifier: LGPL-2.1-or-later
using System.Globalization;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Xml.Linq;

namespace Folderstorm.Theme.Editor;

public sealed record WidgetSampleData(string Type, string Label, double Width, double Height, ThemeColor Text, ThemeColor Background,
    RasterImage? Image, SliceMargins Margins, string Detail, bool Selected, bool Disabled, string Font, double FontSize, string FontStyle);

public static class WidgetSampleSource
{
    public static readonly string[] States = ["Normal", "Hover", "Pressed", "Disabled", "Selected", "Selected hover", "Selected pressed", "Selected disabled", "Inactive"];
    public static WidgetSampleData Read(string directory, ThemeProject? project, BaseCatalog catalog, string path, int index, string state)
    {
        var xml = Contract.Xml(XuiProject.Read(directory, project, catalog, path)); var element = xml.Root!.DescendantsAndSelf().ElementAt(index); var type = element.Name.LocalName;
        var attributes = new Dictionary<string, string>(StringComparer.Ordinal); var template = "xui/en/widgets/" + type + ".xml";
        if (catalog.Resolve(template) != null)
        {
            var defaults = XuiPolicy.Files.Contains(template) ? XuiProject.Read(directory, project, catalog, template) : catalog.ResolveBytes(template);
            foreach (var attribute in Contract.Xml(defaults).Root!.Attributes()) attributes[attribute.Name.LocalName] = attribute.Value;
        }
        foreach (var attribute in element.Attributes()) attributes[attribute.Name.LocalName] = attribute.Value;
        string Value(string name, string fallback = "") => attributes.GetValueOrDefault(name, fallback);
        double Number(string name, double fallback) => double.TryParse(Value(name), NumberStyles.Float, CultureInfo.InvariantCulture, out var value) && double.IsFinite(value) ? Math.Clamp(value, 0, 16384) : fallback;
        ThemeColor ColorValue(string value, string fallback)
        {
            try { return catalog.Colors.ContainsKey(value) ? catalog.InspectColor(value, project?.Edits.Colors ?? new Dictionary<string, string>(), project?.Edits.ColorLinks).Value : ThemeColor.Parse(value); }
            catch (ThemeException) { return ThemeColor.Parse(fallback); }
        }
        var selected = state.StartsWith("Selected", StringComparison.Ordinal); var disabled = state.Contains("disabled", StringComparison.OrdinalIgnoreCase);
        var textName = type == "button" ? selected ? disabled ? "label_color_selected_disabled" : "label_color_selected" : disabled ? "label_color_disabled" : "label_color" : "text_color";
        var text = ColorValue(Value(textName, disabled ? "LabelDisabledColor" : "TextFgColor"), "#F5F3FA");
        var background = ColorValue(Value(type == "floater" ? state == "Inactive" || Value("background_opaque") != "true" ? "bg_alpha_color" : "bg_opaque_color" : "bg_color", "ThemePanel"), "#1A1921");
        var width = Number("width", type == "floater" || type == "panel" ? 300 : type is "text" or "text_editor" ? 220 : 140);
        var height = Number("height", type is "floater" or "panel" ? 180 : type is "text_editor" or "scroll_list" ? 90 : 28);
        var label = Value(type == "floater" ? "title" : "label", element.HasElements ? "" : element.Value.Trim());
        if (label.Length == 0 && !attributes.ContainsKey("label") && type == "button") label = "Button";
        var imageName = "";
        if (type == "button")
        {
            var property = state switch { "Hover" => "image_hover_unselected", "Pressed" => "image_pressed", "Disabled" => "image_disabled", "Selected" => "image_selected", "Selected hover" => "image_hover_selected", "Selected pressed" => "image_pressed_selected", "Selected disabled" => "image_disabled_selected", _ => "image_unselected" };
            imageName = Value(property, Value(selected ? "image_selected" : "image_unselected", Value("image_unselected")));
        }
        else if (type == "floater") imageName = Value(state == "Inactive" || Value("background_opaque") != "true" ? "bg_alpha_image" : "bg_opaque_image");
        else if (type is "icon" or "image") imageName = Value("image_name", Value("image"));
        RasterImage? image = null; SliceMargins margins = default; var detail = type + " · " + width + " × " + height;
        if (imageName.Length > 0)
        {
            try
            {
                var texture = project == null ? catalog.Textures[imageName] : ImageProject.Resolve(directory, project, catalog, imageName);
                var file = texture.Attributes["file_name"]; var source = project == null ? catalog.ResolveBytes("textures/" + file) : ImageProject.Read(directory, project, catalog, imageName);
                var pixels = ImageCodec.Decode(source, file); var geometry = TextureGeometry.FromAttributes(pixels.Width, pixels.Height, texture.Attributes);
                image = pixels.Crop(geometry.Clip.Left, pixels.Height - geometry.Clip.Top, geometry.ClipWidth, geometry.ClipHeight);
                margins = geometry.Margins;
                var tint = type == "button" ? Value(disabled ? "image_color_disabled" : "image_color", "ButtonImageColor") : Value(state == "Inactive" || Value("background_opaque") != "true" ? "bg_alpha_image_overlay" : "bg_opaque_image_overlay", "#FFFFFFFF");
                image = image.Tint(ColorValue(tint, "#FFFFFFFF")); detail += "\n" + imageName;
            }
            catch (Exception error) when (error is ThemeException or System.IO.IOException or NotSupportedException or KeyNotFoundException or FormatException)
            { image = null; detail += "\nImage sample unavailable: " + error.Message; }
        }
        var font = Value("font", "SansSerifSmall"); var points = 9d;
        try { var sizes = FontProject.Sizes(FontProject.Read(directory, project, catalog)); var size = sizes.Keys.OrderByDescending(name => name.Length).FirstOrDefault(name => font.EndsWith(name, StringComparison.Ordinal)); if (size != null) points = sizes[size]; }
        catch (Exception error) when (error is ThemeException or System.IO.IOException or FormatException or ArgumentException) { }
        detail += "\n" + font + " · " + points.ToString(CultureInfo.InvariantCulture) + " pt";
        return new(type, label, width, height, text, background, image, margins, detail, selected, disabled, font, points * 96 / 72, Value("font_style"));
    }
}

// Uses the native asset bytes and slice coordinates. WPF text, compositing and control behavior remain approximate.
public sealed class WidgetSample : FrameworkElement
{
    private WidgetSampleData? data;
    private BitmapSource? bitmap;
    private Size extent = new(1, 1);
    public void SetSample(WidgetSampleData? value, Size comparisonExtent)
    {
        data = value; bitmap = value?.Image == null ? null : ImageCodec.ToBitmap(value.Image); extent = comparisonExtent; InvalidateVisual();
    }
    private static Brush Brush(ThemeColor color) => new SolidColorBrush(Color.FromArgb((byte)Math.Round(color.A * 255), (byte)Math.Round(color.R * 255), (byte)Math.Round(color.G * 255), (byte)Math.Round(color.B * 255)));
    protected override void OnRender(DrawingContext drawing)
    {
        base.OnRender(drawing); if (data == null || ActualWidth <= 0 || ActualHeight <= 0 || data.Width <= 0 || data.Height <= 0) return;
        var scale = Math.Min(3, Math.Min(Math.Max(1, ActualWidth - 24) / Math.Max(1, extent.Width), Math.Max(1, ActualHeight - 24) / Math.Max(1, extent.Height)));
        drawing.PushTransform(new TranslateTransform((ActualWidth - data.Width * scale) / 2, (ActualHeight - data.Height * scale) / 2)); drawing.PushTransform(new ScaleTransform(scale, scale));
        var bounds = new Rect(0, 0, data.Width, data.Height); var line = new Pen(new SolidColorBrush(Color.FromRgb(92, 76, 112)), 1); drawing.PushClip(new RectangleGeometry(bounds));
        if (bitmap != null) DrawSliced(drawing, bitmap, bounds, data.Margins);
        else if (data.Type is "floater" or "panel" or "button" or "line_editor" or "search_editor" or "text_editor" or "scroll_list" or "combo_box" or "spinner" or "slider" or "progress_bar" or "color_swatch" or "tab_container")
            drawing.DrawRoundedRectangle(Brush(data.Background), line, bounds, 2, 2);
        var typeface = new Typeface(new FontFamily(data.Font.Contains("Mono", StringComparison.OrdinalIgnoreCase) ? "Consolas" : "Segoe UI"), data.FontStyle.Contains("ITALIC", StringComparison.OrdinalIgnoreCase) ? FontStyles.Italic : FontStyles.Normal, data.FontStyle.Contains("BOLD", StringComparison.OrdinalIgnoreCase) ? FontWeights.Bold : FontWeights.Normal, FontStretches.Normal);
        var text = new FormattedText(data.Label, CultureInfo.CurrentUICulture, FlowDirection.LeftToRight, typeface, data.FontSize, Brush(data.Text), VisualTreeHelper.GetDpi(this).PixelsPerDip)
        { MaxTextWidth = Math.Max(1, data.Width - 12), MaxTextHeight = Math.Max(1, data.Height - 4), Trimming = TextTrimming.CharacterEllipsis };
        if (data.Type == "button") drawing.DrawText(text, new(Math.Max(4, (data.Width - text.Width) / 2), Math.Max(2, (data.Height - text.Height) / 2)));
        else if (data.Type is "check_box" or "radio_item")
        {
            var size = Math.Min(15, data.Height); var box = new Rect(0, (data.Height - size) / 2, size, size);
            if (data.Type == "radio_item") drawing.DrawEllipse(Brush(data.Background), line, new(box.X + size / 2, box.Y + size / 2), size / 2, size / 2); else drawing.DrawRectangle(Brush(data.Background), line, box);
            if (data.Selected) drawing.DrawEllipse(Brush(data.Text), null, new(size / 2, data.Height / 2), size / 4, size / 4);
            drawing.DrawText(text, new(size + 6, Math.Max(0, (data.Height - text.Height) / 2)));
        }
        else if (data.Type == "slider" || data.Type == "progress_bar")
        {
            drawing.DrawRectangle(Brush(data.Text), null, new(3, data.Height / 2 - 2, Math.Max(1, data.Width * .55), 4));
            if (data.Type == "slider") drawing.DrawRectangle(Brush(data.Text), null, new(data.Width * .55 - 3, 2, 6, Math.Max(1, data.Height - 4)));
        }
        else if (data.Type is "floater" or "panel" or "text" or "line_editor" or "search_editor" or "text_editor" or "combo_box" or "spinner" or "tab_container") drawing.DrawText(text, new(6, 4));
        else if (bitmap == null)
        {
            var notice = new FormattedText("Preview in viewer: " + data.Type, CultureInfo.CurrentUICulture, FlowDirection.LeftToRight, new Typeface("Segoe UI"), 12, Brush(data.Text), VisualTreeHelper.GetDpi(this).PixelsPerDip) { MaxTextWidth = Math.Max(1, data.Width) };
            drawing.DrawText(notice, new());
        }
        drawing.Pop(); drawing.Pop(); drawing.Pop();
    }
    private static void DrawSliced(DrawingContext drawing, BitmapSource source, Rect target, SliceMargins margins)
    {
        var horizontal = margins.Left + margins.Right; var vertical = margins.Top + margins.Bottom;
        var sx = new[] { 0, margins.Left, source.PixelWidth - margins.Right, source.PixelWidth }; var sy = new[] { 0, margins.Top, source.PixelHeight - margins.Bottom, source.PixelHeight };
        var shrinkX = horizontal > 0 ? Math.Min(1, target.Width / horizontal) : 1; var shrinkY = vertical > 0 ? Math.Min(1, target.Height / vertical) : 1;
        var dx = new[] { target.Left, target.Left + margins.Left * shrinkX, target.Right - margins.Right * shrinkX, target.Right }; var dy = new[] { target.Top, target.Top + margins.Top * shrinkY, target.Bottom - margins.Bottom * shrinkY, target.Bottom };
        for (var row = 0; row < 3; ++row) for (var column = 0; column < 3; ++column)
        {
            var width = sx[column + 1] - sx[column]; var height = sy[row + 1] - sy[row];
            if (width <= 0 || height <= 0 || dx[column + 1] <= dx[column] || dy[row + 1] <= dy[row]) continue;
            var piece = new CroppedBitmap(source, new Int32Rect(sx[column], sy[row], width, height)); piece.Freeze();
            drawing.DrawImage(piece, new Rect(dx[column], dy[row], dx[column + 1] - dx[column], dy[row + 1] - dy[row]));
        }
    }
}
