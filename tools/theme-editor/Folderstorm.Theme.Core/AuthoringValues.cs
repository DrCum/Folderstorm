// SPDX-License-Identifier: LGPL-2.1-or-later
using System.Globalization;

namespace Folderstorm.Theme;

public readonly record struct ThemeColor
{
    public double R { get; }
    public double G { get; }
    public double B { get; }
    public double A { get; }
    private ThemeColor(double r, double g, double b, double a) => (R, G, B, A) = (r, g, b, a);
    public static ThemeColor Create(double r, double g, double b, double a = 1)
    {
        Contract.Require(new[] { r, g, b, a }.All(v => double.IsFinite(v) && v is >= 0 and <= 1), "color", "Channels must be between 0 and 1");
        return new(r, g, b, a);
    }
    public static ThemeColor Parse(string value)
    {
        value = value.Trim();
        if (!value.StartsWith('#'))
        {
            var channels = Contract.Rgba(value);
            return Create(channels[0], channels[1], channels[2], channels[3]);
        }
        var hex = value[1..];
        Contract.Require(hex.Length is 3 or 4 or 6 or 8 && hex.All(Uri.IsHexDigit), "color", "Use #RGB, #RGBA, #RRGGBB or #RRGGBBAA");
        if (hex.Length is 3 or 4) hex = string.Concat(hex.Select(c => new string(c, 2)));
        double Channel(int index) => byte.Parse(hex.AsSpan(index, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture) / 255d;
        return Create(Channel(0), Channel(2), Channel(4), hex.Length == 8 ? Channel(6) : 1);
    }
    public string NativeText => Contract.ColorText([R, G, B, A]);
    public string Hex(bool alpha = true)
    {
        string Channel(double value) => ((byte)Math.Round(value * 255, MidpointRounding.AwayFromZero)).ToString("X2", CultureInfo.InvariantCulture);
        return "#" + Channel(R) + Channel(G) + Channel(B) + (alpha ? Channel(A) : "");
    }
    public ThemeColor Over(ThemeColor background)
    {
        var alpha = A + background.A * (1 - A);
        if (alpha == 0) return default;
        var foregroundAlpha = A;
        double Composite(double foreground, double behind) => Math.Clamp((foreground * foregroundAlpha + behind * background.A * (1 - foregroundAlpha)) / alpha, 0, 1);
        return Create(Composite(R, background.R), Composite(G, background.G), Composite(B, background.B), alpha);
    }
    public static double Contrast(ThemeColor foreground, ThemeColor background, ThemeColor canvas)
    {
        Contract.Require(canvas.A == 1, "color", "Contrast needs an opaque canvas behind transparent colors");
        var behind = background.Over(canvas);
        var front = foreground.Over(behind);
        static double Linear(double channel) => channel <= .04045 ? channel / 12.92 : Math.Pow((channel + .055) / 1.055, 2.4);
        static double Luminance(ThemeColor color) => .2126 * Linear(color.R) + .7152 * Linear(color.G) + .0722 * Linear(color.B);
        var first = Luminance(front); var second = Luminance(behind);
        return (Math.Max(first, second) + .05) / (Math.Min(first, second) + .05);
    }
}

public readonly record struct NativeSlice(int Left, int Top, int Right, int Bottom);
public readonly record struct SliceMargins(int Left, int Top, int Right, int Bottom)
{
    public void Validate(int width, int height)
    {
        Contract.Require(width > 0 && height > 0 && Left >= 0 && Right >= 0 && Top >= 0 && Bottom >= 0 &&
            Right <= width && Bottom <= height && Left <= width - Right && Top <= height - Bottom,
            "texture", "Slice margins must fit inside the image");
    }
    public static SliceMargins FromNative(int width, int height, NativeSlice rectangle)
    {
        Contract.Require(width > 0 && height > 0 && rectangle.Left >= 0 && rectangle.Bottom >= 0 &&
            rectangle.Left <= rectangle.Right && rectangle.Right <= width && rectangle.Bottom <= rectangle.Top && rectangle.Top <= height,
            "texture", "Native slice bounds must be ordered inside the image");
        var result = new SliceMargins(rectangle.Left, height - rectangle.Top, width - rectangle.Right, rectangle.Bottom);
        result.Validate(width, height); return result;
    }
    public NativeSlice ToNative(int width, int height)
    {
        Validate(width, height); return new(Left, height - Top, width - Right, Bottom);
    }
    public static SliceMargins FromAttributes(int width, int height, IReadOnlyDictionary<string, string> attributes)
    {
        int Value(string name, int fallback)
        {
            if (!attributes.TryGetValue("scale." + name, out var text)) return fallback;
            Contract.Require(int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out var value), "texture", "Slice coordinates must be whole pixels");
            return value;
        }
        return FromNative(width, height, new(Value("left", 0), Value("top", height), Value("right", width), Value("bottom", 0)));
    }
    public SliceMargins Resize(int oldWidth, int oldHeight, int newWidth, int newHeight)
    {
        Validate(oldWidth, oldHeight);
        Contract.Require(newWidth > 0 && newHeight > 0, "texture", "Choose positive image dimensions");
        static int Scale(int value, int oldSize, int newSize) => (int)Math.Round((double)value * newSize / oldSize, MidpointRounding.AwayFromZero);
        var left = Scale(Left, oldWidth, newWidth); var top = Scale(Top, oldHeight, newHeight);
        return new(left, top, Math.Min(newWidth - left, Scale(Right, oldWidth, newWidth)), Math.Min(newHeight - top, Scale(Bottom, oldHeight, newHeight)));
    }
}

public sealed record TextureFamily(string Name, TextureFamilyMember[] Members);
public sealed record TextureFamilyMember(string State, TextureDefinition Texture);
public static class TextureFamilies
{
    private static readonly (string Suffix, string State)[] States =
    [
        ("_Selected_Disabled", "Selected disabled"), ("_Disabled_Selected", "Selected disabled"),
        ("_Selected_Over", "Selected hover"), ("_Over_Selected", "Selected hover"),
        ("_Selected_Press", "Selected pressed"), ("_Press_Selected", "Selected pressed"),
        ("_On_Selected", "Selected"),
        ("_Disabled", "Disabled"), ("_Selected", "Selected"), ("_Inactive", "Inactive"), ("_Active", "Active"),
        ("_Pressed", "Pressed"), ("_Press", "Pressed"), ("_Focused", "Focused"), ("_Unread", "Unread"),
        ("_Hover", "Hover"), ("_Over", "Hover"), ("_Off", "Normal"), ("_On", "Selected")
    ];
    public static (string Family, string State) Describe(string logicalName)
    {
        foreach (var (suffix, state) in States)
            if (logicalName.Length > suffix.Length && logicalName.EndsWith(suffix, StringComparison.Ordinal))
                return (logicalName[..^suffix.Length], state);
        return (logicalName, "Image");
    }
    public static TextureFamily[] Build(IEnumerable<TextureDefinition> textures) => textures
        .Select(texture => (Description: Describe(texture.Name), Texture: texture))
        .GroupBy(item => item.Description.Family, StringComparer.Ordinal)
        .OrderBy(group => group.Key, StringComparer.Ordinal)
        .Select(group => new TextureFamily(group.Key, group.Select(item => new TextureFamilyMember(item.Description.State, item.Texture))
            .OrderBy(item => item.Texture.Name, StringComparer.Ordinal).ToArray())).ToArray();
}
