// SPDX-License-Identifier: LGPL-2.1-or-later
namespace Folderstorm.Theme;

public static class TgaImage
{
    public static RasterImage Decode(byte[] bytes)
    {
        Contract.Require(bytes.Length is >= 18 and <= 128 * 1024 * 1024, "image", "TGA header is missing or the file exceeds 128 MiB");
        int Word(int position) => bytes[position] | bytes[position + 1] << 8;
        var mapType = bytes[1]; var type = bytes[2]; var firstEntry = Word(3); var mapLength = Word(5); var mapBits = bytes[7];
        var width = Word(12); var height = Word(14); var bits = bytes[16]; var descriptor = bytes[17];
        Contract.Require(width > 0 && height > 0 && width <= 16384 && height <= 16384 && (long)width * height <= RasterImage.PixelLimit, "image", "TGA exceeds the editing dimensions");
        Contract.Require(type is 1 or 2 or 3 or 9 or 10 or 11 && mapType is 0 or 1 && (descriptor & 0xc0) == 0,
            "image", "Unsupported TGA type, palette or interleaving");
        var mapped = type is 1 or 9; var grayscale = type is 3 or 11; var rle = type is 9 or 10 or 11;
        Contract.Require(mapped ? mapType == 1 && bits is 8 or 16 : grayscale ? bits is 8 or 16 : bits is 15 or 16 or 24 or 32,
            "image", "Unsupported TGA pixel depth");
        var position = 18 + bytes[0];
        Contract.Require(position <= bytes.Length, "image", "Truncated TGA identifier");
        byte ReadByte()
        {
            Contract.Require(position < bytes.Length, "image", "Truncated TGA pixels"); return bytes[position++];
        }
        static byte Expand5(int value) => (byte)((value * 255 + 15) / 31);
        RgbaPixel ReadColor(int depth, bool gray)
        {
            if (gray || depth == 8)
            {
                var luminance = ReadByte(); return new(luminance, luminance, luminance, depth == 16 ? ReadByte() : (byte)255);
            }
            if (depth is 15 or 16)
            {
                var value = ReadByte() | ReadByte() << 8;
                // Match LLImageTGA: the high 16-bit flag is not used as alpha.
                return new(Expand5((value >> 10) & 31), Expand5((value >> 5) & 31), Expand5(value & 31), 255);
            }
            var blue = ReadByte(); var green = ReadByte(); var red = ReadByte();
            // LLImageTGA preserves 32-bit alpha even when descriptor bits are zero.
            return new(red, green, blue, depth == 32 ? ReadByte() : (byte)255);
        }
        RgbaPixel[] palette = [];
        if (mapType == 1)
        {
            Contract.Require(mapLength > 0 && firstEntry + mapLength <= 65536 && mapBits is 8 or 15 or 16 or 24 or 32,
                "image", "Invalid TGA palette");
            palette = mapped ? new RgbaPixel[mapLength] : [];
            for (var index = 0; index < mapLength; ++index)
            {
                var color = ReadColor(mapBits, false); if (mapped) palette[index] = color;
            }
        }
        RgbaPixel ReadPixel()
        {
            if (!mapped) return ReadColor(bits, grayscale);
            var index = (int)ReadByte(); if (bits == 16) index |= ReadByte() << 8;
            index -= firstEntry;
            Contract.Require(index >= 0 && index < palette.Length, "image", "TGA palette index is outside the color map");
            return palette[index];
        }
        var count = checked(width * height); var pixels = new byte[count * 4]; var written = 0;
        void Put(RgbaPixel color)
        {
            var x = written % width; var y = written / width;
            if ((descriptor & 0x10) != 0) x = width - 1 - x;
            if ((descriptor & 0x20) == 0) y = height - 1 - y;
            var offset = (y * width + x) * 4;
            pixels[offset] = color.R; pixels[offset + 1] = color.G; pixels[offset + 2] = color.B; pixels[offset + 3] = color.A; ++written;
        }
        while (written < count)
        {
            if (!rle) { Put(ReadPixel()); continue; }
            var header = ReadByte(); var length = (header & 0x7f) + 1;
            Contract.Require(length <= count - written, "image", "TGA run exceeds the image bounds");
            if ((header & 0x80) != 0)
            {
                var color = ReadPixel(); for (var index = 0; index < length; ++index) Put(color);
            }
            else for (var index = 0; index < length; ++index) Put(ReadPixel());
        }
        return RasterImage.FromRgba(width, height, pixels);
    }
}
