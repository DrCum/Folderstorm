// SPDX-License-Identifier: LGPL-2.1-or-later
using System.Globalization;
using System.Windows;
using System.Windows.Media;

namespace Folderstorm.Theme.Editor;

// An approximate WPF color sample. Exact artwork and fonts are tested in the viewer.
public sealed class PaletteSample : FrameworkElement
{
    private IReadOnlyDictionary<string, ThemeColor> colors = new Dictionary<string, ThemeColor>();
    public void SetColors(IReadOnlyDictionary<string, ThemeColor> values) { colors = values; InvalidateVisual(); }
    protected override void OnRender(DrawingContext drawing)
    {
        base.OnRender(drawing);
        var scale = Math.Min(ActualWidth / 480, ActualHeight / 285);
        if (scale <= 0) return;
        drawing.PushTransform(new TranslateTransform((ActualWidth - 480 * scale) / 2, (ActualHeight - 285 * scale) / 2));
        drawing.PushTransform(new ScaleTransform(scale, scale));
        Brush Brush(string name, string fallback)
        {
            var color = colors.TryGetValue(name, out var value) ? value : ThemeColor.Parse(fallback);
            return new SolidColorBrush(Color.FromArgb((byte)Math.Round(color.A * 255), (byte)Math.Round(color.R * 255), (byte)Math.Round(color.G * 255), (byte)Math.Round(color.B * 255)));
        }
        void Box(Rect rectangle, string fill, string fallback, string? line = null)
            => drawing.DrawRoundedRectangle(Brush(fill, fallback), line == null ? null : new Pen(Brush(line, "#302D3B"), 1), rectangle, 3, 3);
        void Text(string text, double x, double y, double size = 13, string color = "ThemeText", string fallback = "#F5F3FA")
            => drawing.DrawText(new FormattedText(text, CultureInfo.CurrentUICulture, FlowDirection.LeftToRight,
                new Typeface("Segoe UI"), size, Brush(color, fallback), VisualTreeHelper.GetDpi(this).PixelsPerDip), new(x, y));
        Box(new(0, 0, 480, 285), "ThemeBg", "#111016");
        Box(new(18, 15, 444, 255), "ThemePanel", "#1A1921", "ThemeLine");
        Box(new(18, 15, 444, 31), "ThemeTitle", "#302140");
        Text("Appearance", 31, 20, 14); Text("×", 436, 19, 16);
        Text("Hello, world", 34, 65, 23); Text("Русский · 中文 · Aa 0123", 34, 101, 13, "ThemeMuted", "#ADA9BA");
        Box(new(34, 142, 118, 32), "SampleButtonFill", "#302D3B", "ThemeLine");
        Text("Button", 70, 148, 13, "ButtonLabelColor");
        Box(new(164, 142, 126, 32), "ThemeHover", "#3A3054", "ThemeLine"); Text("Hover", 207, 148);
        Box(new(307, 148, 17, 17), "ThemeAccent", "#BFA4FC"); Text("✓", 309, 146, 14, "ThemeBg", "#111016"); Text("Selected", 332, 148);
        Box(new(34, 190, 256, 29), "ThemeBg", "#111016", "ThemeLine"); Text("Type something…", 44, 195, 12, "ThemeMuted", "#ADA9BA");
        Text("A link to somewhere", 307, 194, 12, "AnsaStorm_HtmlLink", "#BFA4FC");
        Box(new(34, 238, 256, 6), "ThemeBg", "#111016"); Box(new(34, 238, 155, 6), "ThemeAccent", "#BFA4FC");
        Text("Text on panel", 307, 231, 12, "ThemeText");
        drawing.Pop(); drawing.Pop();
    }
}
