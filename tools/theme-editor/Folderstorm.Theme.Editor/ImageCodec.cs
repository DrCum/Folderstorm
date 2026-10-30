// SPDX-License-Identifier: LGPL-2.1-or-later
using System.IO;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace Folderstorm.Theme.Editor;

internal static class ImageCodec
{
    public static RasterImage Decode(byte[] bytes, string? fileName = null)
    {
        if (string.Equals(Path.GetExtension(fileName), ".tga", StringComparison.OrdinalIgnoreCase)) return TgaImage.Decode(bytes);
        using var input = new MemoryStream(bytes, false);
        var decoder = BitmapDecoder.Create(input, BitmapCreateOptions.PreservePixelFormat | BitmapCreateOptions.DelayCreation, BitmapCacheOption.OnDemand);
        var frame = decoder.Frames[0];
        Contract.Require(frame.PixelWidth > 0 && frame.PixelHeight > 0 && frame.PixelWidth <= 16384 && frame.PixelHeight <= 16384 && (long)frame.PixelWidth * frame.PixelHeight <= RasterImage.PixelLimit,
            "image", "This image exceeds the editing limit; preserve it and inspect it in the native viewer");
        var bitmap = new FormatConvertedBitmap(frame, PixelFormats.Bgra32, null, 0);
        var pixels = new byte[checked(frame.PixelWidth * frame.PixelHeight * 4)]; bitmap.CopyPixels(pixels, frame.PixelWidth * 4, 0);
        SwapRedBlue(pixels);
        return RasterImage.FromRgba(frame.PixelWidth, frame.PixelHeight, pixels);
    }
    public static BitmapSource ToBitmap(RasterImage image)
    {
        var pixels = image.ToRgba(); SwapRedBlue(pixels);
        var bitmap = BitmapSource.Create(image.Width, image.Height, 96, 96, PixelFormats.Bgra32, null, pixels, image.Width * 4);
        bitmap.Freeze(); return bitmap;
    }
    public static byte[] EncodePng(RasterImage image)
    {
        var encoder = new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(ToBitmap(image)));
        using var output = new MemoryStream(); encoder.Save(output);
        Contract.Require(output.Length <= 32L * 1024 * 1024, "size", "PNG output exceeds 32 MiB; choose smaller dimensions");
        return output.ToArray();
    }
    private static void SwapRedBlue(byte[] pixels)
    {
        for (var offset = 0; offset < pixels.Length; offset += 4) (pixels[offset], pixels[offset + 2]) = (pixels[offset + 2], pixels[offset]);
    }
}
