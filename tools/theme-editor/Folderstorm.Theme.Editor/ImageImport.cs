// SPDX-License-Identifier: LGPL-2.1-or-later
using System.IO;
using System.Windows.Media.Imaging;

namespace Folderstorm.Theme.Editor;

public sealed record ImportedImage(byte[] Original, string Extension, byte[] Png, RasterImage Image);
public static class ImageImport
{
    public static ImportedImage Read(string path)
    {
        using var input = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read);
        Contract.Require(input.Length is > 0 and <= 32L * 1024 * 1024, "size", "Image must be nonempty and at most 32 MiB");
        var bytes = new byte[(int)input.Length]; input.ReadExactly(bytes); return Prepare(bytes, path);
    }
    public static ImportedImage Prepare(byte[] bytes, string name)
    {
        Contract.Require(bytes.Length is > 0 and <= 32 * 1024 * 1024, "size", "Image must be nonempty and at most 32 MiB");
        var extension = Path.GetExtension(name).ToLowerInvariant();
        Contract.Require(extension is ".png" or ".tga" or ".bmp" or ".jpg" or ".jpeg", "image_format", "Choose PNG, TGA, BMP or JPEG artwork");
        if (extension != ".tga")
        {
            using var input = new MemoryStream(bytes, false);
            var decoder = BitmapDecoder.Create(input, BitmapCreateOptions.DelayCreation, BitmapCacheOption.OnDemand);
            Contract.Require(decoder.Frames.Count == 1 && (extension switch
            {
                ".png" => decoder is PngBitmapDecoder, ".bmp" => decoder is BmpBitmapDecoder,
                _ => decoder is JpegBitmapDecoder
            }), "image_format", "Image contents must match a supported single-image file type");
        }
        var image = ImageCodec.Decode(bytes, name);
        return new(bytes, extension, extension == ".png" ? bytes : ImageCodec.EncodePng(image), image);
    }
}
