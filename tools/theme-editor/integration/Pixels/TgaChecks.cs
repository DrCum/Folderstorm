// SPDX-License-Identifier: LGPL-2.1-or-later
using Folderstorm.Theme;
using Folderstorm.Theme.Editor;
using System.IO;

internal static class TgaChecks
{
    private static void Assert(bool condition, string message) { if (!condition) throw new Exception(message); }
    private static void Reject(byte[] bytes)
    {
        try { TgaImage.Decode(bytes); throw new Exception("Malformed TGA was accepted"); }
        catch (ThemeException error) { Assert(error.Code == "image", "Unexpected TGA rejection: " + error.Code); }
    }
    private static byte[] Header(int width, int height, byte type, byte bits, byte descriptor)
    {
        var bytes = new byte[18]; bytes[2] = type; bytes[12] = (byte)width; bytes[13] = (byte)(width >> 8);
        bytes[14] = (byte)height; bytes[15] = (byte)(height >> 8); bytes[16] = bits; bytes[17] = descriptor; return bytes;
    }
    public static void Run(BaseCatalog catalog)
    {
        RgbaPixel[] corners = [new(255, 0, 0, 255), new(0, 255, 0, 255), new(0, 0, 255, 64), new(220, 180, 10, 0)];
        foreach (var top in new[] { false, true })
            foreach (var right in new[] { false, true })
                foreach (var rle in new[] { false, true })
                    foreach (var depth in new byte[] { 24, 32 })
                    {
                        var bytes = new List<byte>(Header(2, 2, rle ? (byte)10 : (byte)2, depth, (byte)((top ? 32 : 0) | (right ? 16 : 0) | (depth == 32 ? 8 : 0))));
                        if (rle) bytes.Add(3);
                        for (var index = 0; index < 4; ++index)
                        {
                            var x = right ? 1 - index % 2 : index % 2; var y = top ? index / 2 : 1 - index / 2; var color = corners[y * 2 + x];
                            bytes.Add(color.B); bytes.Add(color.G); bytes.Add(color.R); if (depth == 32) bytes.Add(color.A);
                        }
                        var image = ImageCodec.Decode(bytes.ToArray(), "fixture.TGA");
                        for (var index = 0; index < 4; ++index)
                            Assert(image.Pixel(index % 2, index / 2) == corners[index] with { A = depth == 32 ? corners[index].A : (byte)255 }, "TGA channel, alpha or origin mismatch");
                        Reject(bytes.Take(bytes.Count - 1).ToArray());
                    }
        var repeated = Header(2, 2, 10, 32, 0).Concat(new byte[] { 0x83, 10, 20, 30, 64 }).ToArray();
        Assert(TgaImage.Decode(repeated).Pixel(1, 1) == new RgbaPixel(30, 20, 10, 64), "RLE run or descriptor-zero alpha mismatch");
        var paletteHeader = Header(2, 1, 1, 8, 32); paletteHeader[0] = 3; paletteHeader[1] = 1; paletteHeader[3] = 5; paletteHeader[5] = 2; paletteHeader[7] = 24;
        var palette = paletteHeader.Concat(new byte[] { 65, 66, 67, 0, 0, 255, 255, 0, 0, 5, 6 }).ToArray();
        Assert(TgaImage.Decode(palette).Pixel(0, 0) == new RgbaPixel(255, 0, 0, 255) && TgaImage.Decode(palette).Pixel(1, 0) == new RgbaPixel(0, 0, 255, 255), "Palette origin/identifier mismatch");
        var badPalette = (byte[])palette.Clone(); badPalette[^1] = 4; Reject(badPalette);
        var gray = Header(2, 1, 3, 8, 32).Concat(new byte[] { 64, 255 }).ToArray();
        Assert(TgaImage.Decode(gray).Pixel(0, 0) == new RgbaPixel(64, 64, 64, 255), "Grayscale mismatch");
        var rgb16 = Header(1, 1, 2, 16, 0).Concat(new byte[] { 0, 0x7c }).ToArray();
        Assert(TgaImage.Decode(rgb16).Pixel(0, 0) == new RgbaPixel(255, 0, 0, 255), "Native RGB555 interpretation mismatch");
        Reject(Header(1, 1, 10, 32, 0).Concat(new byte[] { 0x81, 0, 0, 0, 0 }).ToArray());
        Reject(new byte[17]);
        Console.WriteLine("PASS TGA origins/depth/alpha/palette/RLE fixtures and malformed bounds");
        var records = new List<object>(); var decodedCount = 0;
        foreach (var path in Directory.EnumerateFiles(Path.Combine(catalog.Root, "skins"), "*.tga", SearchOption.AllDirectories).Order(StringComparer.Ordinal))
        {
            var bytes = File.ReadAllBytes(path); var sourceHash = Contract.Hash(bytes);
            var relative = Path.GetRelativePath(catalog.Root, path).Replace('\\', '/');
            if (bytes.Length == 0 && relative == "skins/default/themes/textures/placeholder.tga")
            {
                Reject(bytes); records.Add(new { Path = relative, Status = "empty-placeholder-rejected", SourceSha256 = sourceHash }); continue;
            }
            var image = ImageCodec.Decode(bytes, path); ++decodedCount;
            Assert(Contract.Hash(bytes) == sourceHash, "TGA decoder modified input bytes: " + path);
            records.Add(new { Path = relative, Status = "decoded", image.Width, image.Height, SourceSha256 = sourceHash, RgbaSha256 = Contract.Hash(image.ToRgba()) });
        }
        Assert(records.Count > 0, "No native TGA fixtures found");
        var evidence = Path.Combine(Environment.CurrentDirectory, "artifacts", "tga-" + Guid.NewGuid().ToString("N")); Directory.CreateDirectory(evidence);
        var receipt = Path.Combine(evidence, "native-tga-results.json"); File.WriteAllBytes(receipt, Contract.Bytes(records));
        Console.WriteLine("PASS " + decodedCount + " native TGA files decoded without changing bytes; empty placeholder rejected. Receipt: " + receipt);
    }
}
