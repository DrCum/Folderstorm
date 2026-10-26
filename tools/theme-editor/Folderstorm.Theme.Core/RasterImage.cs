// SPDX-License-Identifier: LGPL-2.1-or-later
namespace Folderstorm.Theme;

public readonly record struct RgbaPixel(byte R, byte G, byte B, byte A)
{
    public ThemeColor Color => ThemeColor.Create(R / 255d, G / 255d, B / 255d, A / 255d);
}
public enum ImageInterpolation { Nearest, Bilinear }

// Immutable straight-alpha pixels. Imported bytes remain separate project assets.
public sealed class RasterImage
{
    public const int PixelLimit = 16_000_000;
    private readonly byte[] pixels;
    public int Width { get; }
    public int Height { get; }
    private RasterImage(int width, int height, byte[] ownedPixels) => (Width, Height, pixels) = (width, height, ownedPixels);
    private static int Storage(int width, int height)
    {
        Contract.Require(width > 0 && height > 0 && width <= 16384 && height <= 16384 && (long)width * height <= PixelLimit,
            "image", "Editing requires positive dimensions and at most 16 megapixels");
        return checked(width * height * 4);
    }
    public static RasterImage FromRgba(int width, int height, ReadOnlySpan<byte> bytes)
    {
        Contract.Require(bytes.Length == Storage(width, height), "image", "RGBA byte count does not match the dimensions");
        return new(width, height, bytes.ToArray());
    }
    public byte[] ToRgba() => (byte[])pixels.Clone();
    public RgbaPixel Pixel(int x, int y)
    {
        Contract.Require(x >= 0 && y >= 0 && x < Width && y < Height, "image", "Pixel is outside the image");
        var offset = (y * Width + x) * 4; return new(pixels[offset], pixels[offset + 1], pixels[offset + 2], pixels[offset + 3]);
    }
    private static byte Unit(double value) => Channel(value * 255);
    private static byte Channel(double value) => (byte)Math.Round(Math.Clamp(value, 0, 255), MidpointRounding.AwayFromZero);
    private static void Write(byte[] output, int offset, ThemeColor color)
    { output[offset] = Unit(color.R); output[offset + 1] = Unit(color.G); output[offset + 2] = Unit(color.B); output[offset + 3] = Unit(color.A); }

    public static RasterImage Fill(int width, int height, ThemeColor color) => Gradient(width, height, color, color, false);
    public static RasterImage Gradient(int width, int height, ThemeColor first, ThemeColor last, bool vertical)
    {
        var output = new byte[Storage(width, height)]; var length = vertical ? height : width; var ramp = new byte[length * 4];
        for (var index = 0; index < length; ++index)
        {
            var mix = length == 1 ? 0 : (double)index / (length - 1); var alpha = first.A * (1 - mix) + last.A * mix;
            double Blend(double a, double b) => alpha == 0 ? a * (1 - mix) + b * mix : (a * first.A * (1 - mix) + b * last.A * mix) / alpha;
            var offset = index * 4; ramp[offset] = Unit(Blend(first.R, last.R)); ramp[offset + 1] = Unit(Blend(first.G, last.G));
            ramp[offset + 2] = Unit(Blend(first.B, last.B)); ramp[offset + 3] = Unit(alpha);
        }
        for (var y = 0; y < height; ++y)
            if (!vertical) ramp.CopyTo(output, y * width * 4);
            else for (var x = 0; x < width; ++x) Array.Copy(ramp, y * 4, output, (y * width + x) * 4, 4);
        return new(width, height, output);
    }
    public RasterImage Crop(int x, int y, int width, int height)
    {
        Storage(width, height);
        Contract.Require(x >= 0 && y >= 0 && width <= Width && height <= Height && x <= Width - width && y <= Height - height,
            "image", "Crop rectangle must fit inside the image");
        var output = new byte[Storage(width, height)];
        for (var row = 0; row < height; ++row) Array.Copy(pixels, ((y + row) * Width + x) * 4, output, row * width * 4, width * 4);
        return new(width, height, output);
    }
    public RasterImage Tint(ThemeColor color)
    {
        var output = new byte[pixels.Length];
        for (var offset = 0; offset < output.Length; offset += 4)
        {
            output[offset] = Channel(pixels[offset] * color.R); output[offset + 1] = Channel(pixels[offset + 1] * color.G);
            output[offset + 2] = Channel(pixels[offset + 2] * color.B); output[offset + 3] = Channel(pixels[offset + 3] * color.A);
        }
        return new(Width, Height, output);
    }
    public RasterImage Adjust(double brightness = 0, double contrast = 0, double saturation = 1, double gamma = 1, double alphaScale = 1)
    {
        Contract.Require(double.IsFinite(brightness) && brightness is >= -1 and <= 1 && double.IsFinite(contrast) && contrast is >= -1 and <= 1 &&
            double.IsFinite(saturation) && saturation is >= 0 and <= 2 && double.IsFinite(gamma) && gamma is >= .1 and <= 5 && double.IsFinite(alphaScale) && alphaScale is >= 0 and <= 2,
            "image", "Adjustment is outside its supported range");
        var output = new byte[pixels.Length];
        for (var offset = 0; offset < output.Length; offset += 4)
        {
            var r = pixels[offset] / 255d; var g = pixels[offset + 1] / 255d; var b = pixels[offset + 2] / 255d;
            var luma = .2126 * r + .7152 * g + .0722 * b;
            byte Transform(double channel)
            {
                var value = Math.Clamp(((luma + (channel - luma) * saturation) - .5) * (1 + contrast) + .5 + brightness, 0, 1);
                return Unit(gamma == 1 ? value : Math.Pow(value, 1 / gamma));
            }
            output[offset] = Transform(r); output[offset + 1] = Transform(g); output[offset + 2] = Transform(b); output[offset + 3] = Channel(pixels[offset + 3] * alphaScale);
        }
        return new(Width, Height, output);
    }
    public RasterImage Border(int thickness, ThemeColor color)
    {
        Contract.Require(thickness >= 0 && thickness <= Math.Max(Width, Height), "image", "Border thickness is outside the image bounds");
        var output = ToRgba();
        for (var y = 0; y < Height; ++y)
            for (var x = 0; x < Width; ++x)
                if (x < thickness || y < thickness || x >= Width - thickness || y >= Height - thickness) Write(output, (y * Width + x) * 4, color);
        return new(Width, Height, output);
    }
    public RasterImage Resize(int width, int height, ImageInterpolation interpolation = ImageInterpolation.Bilinear)
    {
        Contract.Require(Enum.IsDefined(interpolation), "image", "Unknown interpolation");
        var storage = Storage(width, height);
        if (width == Width && height == Height) return this;
        var output = new byte[storage];
        for (var y = 0; y < height; ++y)
            for (var x = 0; x < width; ++x)
                Sample((x + .5) * Width / width - .5, (y + .5) * Height / height - .5, 0, Width - 1, 0, Height - 1, interpolation, output, (y * width + x) * 4);
        return new(width, height, output);
    }
    public RasterImage Stretch(int width, int height, SliceMargins margins, ImageInterpolation interpolation = ImageInterpolation.Bilinear)
    {
        margins.Validate(Width, Height); var output = new byte[Storage(width, height)];
        Contract.Require(width >= margins.Left + margins.Right && height >= margins.Top + margins.Bottom && Enum.IsDefined(interpolation), "image", "Preview must fit the fixed slice borders");
        static (double Value, int Min, int Max) Axis(int position, int destination, int source, int before, int after)
        {
            if (position < before) return (position, 0, before - 1);
            if (position >= destination - after) return (source - after + position - (destination - after), source - after, source - 1);
            var stretch = source - before - after;
            if (stretch == 0) { var edge = Math.Clamp(before, 0, source - 1); return (edge, edge, edge); }
            return (before + (position - before + .5) * stretch / (destination - before - after) - .5, before, source - after - 1);
        }
        for (var y = 0; y < height; ++y)
        {
            var sy = Axis(y, height, Height, margins.Top, margins.Bottom);
            for (var x = 0; x < width; ++x)
            {
                var sx = Axis(x, width, Width, margins.Left, margins.Right);
                Sample(sx.Value, sy.Value, sx.Min, sx.Max, sy.Min, sy.Max, interpolation, output, (y * width + x) * 4);
            }
        }
        return new(width, height, output);
    }
    private void Sample(double x, double y, int minX, int maxX, int minY, int maxY, ImageInterpolation interpolation, byte[] output, int target)
    {
        x = Math.Clamp(x, minX, maxX); y = Math.Clamp(y, minY, maxY);
        if (interpolation == ImageInterpolation.Nearest)
        {
            var offset = ((int)Math.Floor(y + .5) * Width + (int)Math.Floor(x + .5)) * 4;
            Array.Copy(pixels, offset, output, target, 4); return;
        }
        var x0 = (int)Math.Floor(x); var y0 = (int)Math.Floor(y); var tx = x - x0; var ty = y - y0;
        var x1 = Math.Min(x0 + 1, maxX); var y1 = Math.Min(y0 + 1, maxY);
        var first = (y0 * Width + x0) * 4; var second = (y0 * Width + x1) * 4;
        var third = (y1 * Width + x0) * 4; var fourth = (y1 * Width + x1) * 4;
        double Weighted(int channel, bool premultiply)
        {
            double Value(int offset) => pixels[offset + channel] * (premultiply ? pixels[offset + 3] : 1d);
            return Value(first) * (1 - tx) * (1 - ty) + Value(second) * tx * (1 - ty) + Value(third) * (1 - tx) * ty + Value(fourth) * tx * ty;
        }
        var alpha = Weighted(3, false);
        for (var channel = 0; channel < 3; ++channel) output[target + channel] = alpha == 0 ? (byte)0 : Channel(Weighted(channel, true) / alpha);
        output[target + 3] = Channel(alpha);
    }
}
