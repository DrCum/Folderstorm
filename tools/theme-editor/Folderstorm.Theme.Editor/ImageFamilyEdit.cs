// SPDX-License-Identifier: LGPL-2.1-or-later
using System.IO;
using System.Text.Json;

namespace Folderstorm.Theme.Editor;

public sealed record FamilyImage(string Name, string State, RasterImage? Image, TextureGeometry? Geometry, string? Error)
{
    public bool CanEdit => Image != null && Geometry != null && Error == null;
}
public enum FamilyOperation { Tint, Adjust, Resize, Guides }
public sealed record FamilySettings(FamilyOperation Operation, ThemeColor Color, double Brightness = 0, double Contrast = 0,
    double Saturation = 1, double Gamma = 1, double Alpha = 1, int Width = 32, int Height = 24,
    ImageInterpolation Interpolation = ImageInterpolation.Nearest, TextureGeometry? Guides = null);
public sealed record FamilyResult(FamilyImage Source, RasterImage Image, TextureGeometry Geometry, Recipe Recipe)
{
    public bool Changed => Geometry != Source.Geometry || !Image.PixelEquals(Source.Image!);
    public ImageEdit Encode() => new(Source.Name, ImageCodec.EncodePng(Image), Geometry, Recipe);
}
public static class ImageFamilyEdit
{
    public static FamilyImage[] Load(string directory, ThemeProject project, BaseCatalog catalog, string selected)
    {
        var family = TextureFamilies.Describe(selected).Family;
        var names = catalog.Textures.Keys.Where(name => TextureFamilies.Describe(name).Family == family).Order(StringComparer.Ordinal).Take(65).ToArray();
        Contract.Require(names.Length is > 0 and <= 64, "image_batch", "This family needs a smaller selection than 64 images");
        var result = new List<FamilyImage>(); long pixels = 0;
        foreach (var name in names)
        {
            RasterImage? image = null; TextureGeometry? geometry = null; string? error = null;
            try
            {
                var texture = ImageProject.Resolve(directory, project, catalog, name);
                Contract.Require(texture.Attributes.TryGetValue("file_name", out var file), "reference", "No local image file");
                image = ImageCodec.Decode(ImageProject.Read(directory, project, catalog, name), file);
                pixels += (long)image.Width * image.Height;
                Contract.Require(pixels <= RasterImage.PixelLimit, "size", "Family exceeds the 16 megapixel editing limit");
                geometry = TextureGeometry.FromAttributes(image.Width, image.Height, texture.Attributes);
            }
            catch (Exception exception) when (exception is IOException or ThemeException or NotSupportedException or FormatException)
            { error = exception.Message; if (pixels > RasterImage.PixelLimit) image = null; }
            result.Add(new(name, TextureFamilies.Describe(name).State, image, geometry, error));
        }
        return result.ToArray();
    }
    private static Recipe Step(string name, params (string Name, object Value)[] values) => new(name,
        values.ToDictionary(item => item.Name, item => JsonSerializer.SerializeToElement(item.Value, Contract.JsonOptions), StringComparer.Ordinal));
    public static FamilyResult[] Preview(IEnumerable<FamilyImage> selection, FamilySettings settings)
    {
        var entries = selection.Take(65).ToArray();
        Contract.Require(entries.Length is > 0 and <= 64 && entries.All(item => item.CanEdit), "image_batch", "Check editable states first");
        var pixels = settings.Operation == FamilyOperation.Resize ? (long)settings.Width * settings.Height * entries.Length
            : entries.Sum(item => (long)item.Image!.Width * item.Image.Height);
        Contract.Require(pixels is > 0 and <= RasterImage.PixelLimit, "size", "Checked states exceed the 16 megapixel editing limit");
        return entries.Select(entry =>
        {
            var source = entry.Image!; var geometry = entry.Geometry!.Value;
            return settings.Operation switch
            {
                FamilyOperation.Tint => new FamilyResult(entry, source.Tint(settings.Color), geometry, Step("tint", ("color", settings.Color.NativeText))),
                FamilyOperation.Adjust => new(entry, source.Adjust(settings.Brightness, settings.Contrast, settings.Saturation, settings.Gamma, settings.Alpha), geometry,
                    Step("color-adjust", ("brightness", settings.Brightness), ("contrast", settings.Contrast), ("saturation", settings.Saturation), ("gamma", settings.Gamma), ("alpha_scale", settings.Alpha))),
                FamilyOperation.Resize => new(entry, source.Resize(settings.Width, settings.Height, settings.Interpolation), geometry.Resize(settings.Width, settings.Height),
                    Step("resize", ("width", settings.Width), ("height", settings.Height), ("interpolation", settings.Interpolation == ImageInterpolation.Nearest ? "nearest" : "bilinear"))),
                FamilyOperation.Guides => CopyGuides(entry, settings.Guides ?? throw new ThemeException("texture", "Current image has no editable guides")),
                _ => throw new ThemeException("image_batch", "Choose an image operation")
            };
        }).ToArray();
    }
    private static FamilyResult CopyGuides(FamilyImage entry, TextureGeometry guides)
    {
        var geometry = guides.Resize(entry.Image!.Width, entry.Image.Height);
        return new(entry, entry.Image, geometry, Step("nine-slice", ("left", geometry.Margins.Left), ("top", geometry.Margins.Top),
            ("right", geometry.Margins.Right), ("bottom", geometry.Margins.Bottom), ("clip_left", geometry.Clip.Left), ("clip_top", geometry.Clip.Top),
            ("clip_right", geometry.Clip.Right), ("clip_bottom", geometry.Clip.Bottom)));
    }
}
