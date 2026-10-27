// SPDX-License-Identifier: LGPL-2.1-or-later
namespace Folderstorm.Theme;

public readonly record struct HsvColor(double Hue, double Saturation, double Value, double Alpha)
{
    public static HsvColor FromColor(ThemeColor color)
    {
        var max = Math.Max(color.R, Math.Max(color.G, color.B)); var min = Math.Min(color.R, Math.Min(color.G, color.B)); var delta = max - min;
        var hue = delta == 0 ? 0 : max == color.R ? 60 * ((color.G - color.B) / delta % 6) : max == color.G ? 60 * ((color.B - color.R) / delta + 2) : 60 * ((color.R - color.G) / delta + 4);
        return new(hue < 0 ? hue + 360 : hue, max == 0 ? 0 : delta / max, max, color.A);
    }
    public ThemeColor ToColor()
    {
        Contract.Require(double.IsFinite(Hue) && Hue is >= 0 and <= 360 && double.IsFinite(Saturation) && Saturation is >= 0 and <= 1 &&
            double.IsFinite(Value) && Value is >= 0 and <= 1 && double.IsFinite(Alpha) && Alpha is >= 0 and <= 1, "color", "Invalid HSV color");
        var h = Hue == 360 ? 0 : Hue / 60; var c = Value * Saturation; var x = c * (1 - Math.Abs(h % 2 - 1)); var m = Value - c;
        var (r, g, b) = h switch { < 1 => (c, x, 0d), < 2 => (x, c, 0d), < 3 => (0d, c, x), < 4 => (0d, x, c), < 5 => (x, 0d, c), _ => (c, 0d, x) };
        return ThemeColor.Create(Math.Clamp(r + m, 0, 1), Math.Clamp(g + m, 0, 1), Math.Clamp(b + m, 0, 1), Alpha);
    }
}
