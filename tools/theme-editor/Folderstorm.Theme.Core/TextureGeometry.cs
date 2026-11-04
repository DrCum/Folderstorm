// SPDX-License-Identifier: LGPL-2.1-or-later
using System.Globalization;

namespace Folderstorm.Theme;

public readonly record struct TextureGeometry(int Width, int Height, NativeSlice Clip, SliceMargins Margins)
{
    public int ClipWidth => Clip.Right - Clip.Left;
    public int ClipHeight => Clip.Top - Clip.Bottom;
    public void Validate()
    {
        Contract.Require(Width > 0 && Height > 0 && Width <= 16384 && Height <= 16384 && (long)Width * Height <= RasterImage.PixelLimit,
            "image", "Texture exceeds the editing dimensions");
        Contract.Require(Clip.Left >= 0 && Clip.Bottom >= 0 && Clip.Right <= Width && Clip.Top <= Height && ClipWidth > 0 && ClipHeight > 0,
            "texture", "Clip rectangle must be nonempty and inside the source image");
        Margins.Validate(ClipWidth, ClipHeight);
    }
    public static TextureGeometry FromAttributes(int width, int height, IReadOnlyDictionary<string, string> attributes)
    {
        int Value(string key, int fallback)
        {
            if (!attributes.TryGetValue(key, out var text)) return fallback;
            Contract.Require(int.TryParse(text, NumberStyles.Integer, CultureInfo.InvariantCulture, out var value), "texture", "Image guides require whole pixels: " + key);
            return value;
        }
        var clip = new NativeSlice(Value("clip.left", 0), Value("clip.top", height), Value("clip.right", width), Value("clip.bottom", 0));
        if (clip == default) clip = new(0, height, width, 0); // Native LLRect::null uses the original image bounds.
        var margins = SliceMargins.FromAttributes(clip.Right - clip.Left, clip.Top - clip.Bottom, attributes);
        var result = new TextureGeometry(width, height, clip, margins); result.Validate(); return result;
    }
    public RasterImage VisibleImage(RasterImage source)
    {
        Validate(); Contract.Require(source.Width == Width && source.Height == Height, "image", "Image dimensions changed since the guides were read");
        return source.Crop(Clip.Left, Height - Clip.Top, ClipWidth, ClipHeight);
    }
    public TextureGeometry Resize(int width, int height)
    {
        Validate();
        Contract.Require(width > 0 && height > 0, "image", "Choose positive image dimensions");
        static int Scale(int value, int oldSize, int newSize) => (int)Math.Round((double)value * newSize / oldSize, MidpointRounding.AwayFromZero);
        var left = Math.Clamp(Scale(Clip.Left, Width, width), 0, width - 1); var right = Math.Clamp(Scale(Clip.Right, Width, width), left + 1, width);
        var bottom = Math.Clamp(Scale(Clip.Bottom, Height, height), 0, height - 1); var top = Math.Clamp(Scale(Clip.Top, Height, height), bottom + 1, height);
        var clip = new NativeSlice(left, top, right, bottom);
        var result = new TextureGeometry(width, height, clip, Margins.Resize(ClipWidth, ClipHeight, right - left, top - bottom));
        result.Validate(); return result;
    }
    public TextureGeometry Crop(int x, int y, int width, int height)
    {
        Validate();
        Contract.Require(x >= 0 && y >= 0 && width > 0 && height > 0 && width <= Width && height <= Height && x <= Width - width && y <= Height - height,
            "image", "Crop rectangle must fit inside the source image");
        var oldTop = Height - Clip.Top; var oldBottom = Height - Clip.Bottom;
        var left = Math.Max(Clip.Left, x); var right = Math.Min(Clip.Right, x + width);
        var top = Math.Max(oldTop, y); var bottom = Math.Min(oldBottom, y + height);
        Contract.Require(left < right && top < bottom, "texture", "Crop would remove the entire logical image");
        var clip = new NativeSlice(left - x, height - (top - y), right - x, height - (bottom - y));
        var newWidth = right - left; var newHeight = bottom - top;
        var marginLeft = Math.Clamp(Margins.Left - (left - Clip.Left), 0, newWidth);
        var marginTop = Math.Clamp(Margins.Top - (top - oldTop), 0, newHeight);
        var marginRight = Math.Clamp(Margins.Right - (Clip.Right - right), 0, newWidth - marginLeft);
        var marginBottom = Math.Clamp(Margins.Bottom - (oldBottom - bottom), 0, newHeight - marginTop);
        var result = new TextureGeometry(width, height, clip, new(marginLeft, marginTop, marginRight, marginBottom)); result.Validate(); return result;
    }
    public Dictionary<string, string> ApplyTo(IReadOnlyDictionary<string, string> inherited)
    {
        Validate(); var attributes = new Dictionary<string, string>(inherited, StringComparer.Ordinal);
        void Set(string prefix, NativeSlice rectangle)
        {
            attributes[prefix + "left"] = rectangle.Left.ToString(CultureInfo.InvariantCulture); attributes[prefix + "top"] = rectangle.Top.ToString(CultureInfo.InvariantCulture);
            attributes[prefix + "right"] = rectangle.Right.ToString(CultureInfo.InvariantCulture); attributes[prefix + "bottom"] = rectangle.Bottom.ToString(CultureInfo.InvariantCulture);
        }
        if (Clip == new NativeSlice(0, Height, Width, 0))
            foreach (var edge in new[] { "left", "top", "right", "bottom" }) attributes.Remove("clip." + edge);
        else Set("clip.", Clip);
        Set("scale.", Margins.ToNative(ClipWidth, ClipHeight)); return attributes;
    }
}
