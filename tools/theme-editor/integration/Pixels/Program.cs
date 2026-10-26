// SPDX-License-Identifier: LGPL-2.1-or-later
using Folderstorm.Theme;
using Folderstorm.Theme.Editor;
using System.IO;

internal static class Program
{
    [STAThread]
    private static int Main(string[] args)
    {
        if (args.Length != 1) { Console.Error.WriteLine("Pass the matching viewer or repository root"); return 2; }
        void Assert(bool condition, string message) { if (!condition) throw new Exception(message); }
        try
        {
            byte[] rgba = [255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 64, 17, 31, 47, 0, 70, 80, 90, 255, 1, 2, 3, 1];
            var source = RasterImage.FromRgba(3, 2, rgba); var png = ImageCodec.EncodePng(source); var copy = (byte[])png.Clone();
            var decoded = ImageCodec.Decode(png);
            Assert(decoded.Width == 3 && decoded.Height == 2 && decoded.ToRgba().SequenceEqual(rgba), "PNG RGBA round trip changed channel order, alpha or hidden RGB");
            Assert(png.SequenceEqual(copy), "Decoder modified imported bytes");
            Console.WriteLine("PASS RGBA/alpha/hidden-RGB round trip and immutable input");
            var bitmap = ImageCodec.ToBitmap(source);
            Assert(bitmap.IsFrozen && bitmap.PixelWidth == 3 && bitmap.PixelHeight == 2, "Display bitmap is not detached/frozen");
            Console.WriteLine("PASS detached display bitmap");
            var catalog = new BaseCatalog(args[0]);
            var texture = catalog.Textures["PushButton_Off"];
            Assert(texture.Source != null, "Missing inherited button fixture");
            var original = File.ReadAllBytes(Path.Combine(catalog.Root, texture.Source!)); var originalHash = Contract.Hash(original);
            var inherited = ImageCodec.Decode(original); var inheritedPng = ImageCodec.EncodePng(inherited);
            Assert(ImageCodec.Decode(inheritedPng).ToRgba().SequenceEqual(inherited.ToRgba()) && Contract.Hash(original) == originalHash, "Inherited artwork round trip changed pixels or source bytes");
            Console.WriteLine("PASS inherited button decode/encode pixel fidelity");
            TgaChecks.Run(catalog);
            ImageProjectChecks.Run(catalog);
            ImageResetChecks.Run(catalog);
            Console.WriteLine("7/7 passed. Windowless model/codec checks only; no UI visual acceptance."); return 0;
        }
        catch (Exception error) { Console.Error.WriteLine(error); return 1; }
    }
}
