// SPDX-License-Identifier: LGPL-2.1-or-later
using Microsoft.Win32;
using System.Globalization;
using System.IO;
using System.Text.Json;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media.Imaging;

namespace Folderstorm.Theme.Editor;

public sealed record ImageFamilyChoice(string Name, string Label);

public partial class MainWindow
{
    private RasterImage? currentImage, originalImage;
    private TextureGeometry? imageGeometry, appliedImageGeometry;
    private bool imageOperation, changingImageDimensions;
    private string? lastImageGuideError;
    private TextureDefinition? SelectedTexture => TextureList?.SelectedItem as TextureDefinition;
    public bool CanReplaceImage => CanEditDocument && SelectedTexture != null && (ImageComparison?.SelectedIndex ?? 0) == 0;
    public bool CanEditImage => CanReplaceImage && currentImage != null && imageGeometry != null;
    public bool CanRepairImageGuides => CanReplaceImage && currentImage != null && imageGeometry == null;

    private void InitializeImages()
    {
        EditCanvas.PixelHovered += (x, y, pixel) => ImagePixel.Text = $"{x}, {y}  {pixel.Color.Hex()}  {pixel.R} {pixel.G} {pixel.B} {pixel.A}";
        EditCanvas.PixelChosen += pixel => { if (CanEditImage) { ImageTint.Text = DrawingColor.Text = pixel.Color.Hex(); StatusText.Text = "Pixel color picked."; } };
        EditCanvas.GuidesMoved += geometry => { imageGeometry = geometry; SetGuideFields(geometry); RefreshStretchPreviews(); };
        EditCanvas.GuidesCommitted += geometry => _ = Run(() => ApplyGuides(geometry));
        EditCanvas.GuideDragStateChanged += UpdateCommandState;
        EditCanvas.DrawingStateChanged += UpdateCommandState;
        EditCanvas.DrawingCommitted += CommitDrawing;
        EditCanvas.DrawingFailed += message => StatusText.Text = message;
        DrawingSettingsChanged(this, new RoutedEventArgs());
    }
    private void SearchTextures(object sender, TextChangedEventArgs args) => RefreshTextures();
    private void FilterImageFamily(object sender, SelectionChangedEventArgs args) => RefreshTextures();
    private void FilterImageOverrides(object sender, RoutedEventArgs args) => RefreshTextures();
    private void RefreshImageFamilies()
    {
        if (catalog == null) return;
        var selected = ImageFamily.SelectedValue as string ?? "";
        ImageFamily.ItemsSource = new[] { new ImageFamilyChoice("", "All families") }
            .Concat(TextureFamilies.Build(catalog.Textures.Values).Select(family => new ImageFamilyChoice(family.Name, family.Name + " · " + family.Members.Length))).ToArray();
        ImageFamily.SelectedValue = selected;
        if (ImageFamily.SelectedIndex < 0) ImageFamily.SelectedIndex = 0;
    }
    private void RefreshTextures()
    {
        if (catalog == null || TextureList == null || TextureSearch == null) return;
        var selected = SelectedTexture?.Name; var query = TextureSearch.Text;
        var family = ImageFamily?.SelectedValue as string ?? "";
        var overridden = new HashSet<string>(StringComparer.Ordinal);
        if (project != null)
        {
            var asset = project.Edits.Assets.LastOrDefault(item => item.PayloadPath == "viewer/textures/textures.xml");
            if (asset != null)
                try { overridden.UnionWith(Contract.Xml(File.ReadAllBytes(Contract.Contained(projectDirectory, asset.AssetPath))).Root!.Elements("texture").Select(node => (string?)node.Attribute("name")).OfType<string>()); }
                catch (Exception error) when (error is IOException or ThemeException) { StatusText.Text = error.Message; }
        }
        var textures = catalog.Textures.Values.Where(texture => texture.Name.Contains(query, StringComparison.OrdinalIgnoreCase) ||
                texture.Attributes.GetValueOrDefault("file_name", "").Contains(query, StringComparison.OrdinalIgnoreCase))
            .Where(texture => family.Length == 0 || TextureFamilies.Describe(texture.Name).Family == family)
            .Where(texture => ImageOverridesOnly?.IsChecked != true || overridden.Contains(texture.Name) ||
                project?.Edits.Assets.Any(asset => asset.PayloadPath == "viewer/textures/" + texture.Attributes.GetValueOrDefault("file_name")) == true)
            .OrderBy(texture => texture.Name, StringComparer.Ordinal).ToArray();
        TextureList.ItemsSource = textures;
        TextureList.SelectedItem = textures.FirstOrDefault(texture => texture.Name == selected) ?? textures.FirstOrDefault();
        if (textures.Length == 0)
        {
            currentImage = originalImage = null; imageGeometry = appliedImageGeometry = null;
            TextureName.Text = "No matching images"; ImageState.Text = ImageDimensions.Text = ImageRecipeSummary.Text = ImagePixel.Text = TextureSource.Text = "";
            EditCanvas.SetImage(null, null); RefreshStretchPreviews();
        }
        UpdateCommandState();
    }
    private void SelectTexture(object sender, SelectionChangedEventArgs args)
    {
        currentImage = originalImage = null; imageGeometry = appliedImageGeometry = null;
        if (catalog == null || SelectedTexture is not { } selected) { UpdateImageDisplay(); RefreshStretchPreviews(); UpdateCommandState(); return; }
        TextureName.Text = selected.Name; TextureSource.Text = "Requires native resolution";
        var family = TextureFamilies.Describe(selected.Name); ImageState.Text = family.Family + " · " + family.State;
        try
        {
            var texture = project == null ? selected : ImageProject.Resolve(projectDirectory, project, catalog, selected.Name);
            Contract.Require(texture.Attributes.TryGetValue("file_name", out var fileName), "reference", "Image has no local filename");
            var edited = project?.Edits.Assets.LastOrDefault(asset => asset.PayloadPath == "viewer/textures/" + fileName);
            var bytes = project == null ? catalog.ResolveBytes("textures/" + fileName) : ImageProject.Read(projectDirectory, project, catalog, selected.Name);
            currentImage = ImageCodec.Decode(bytes, fileName);
            try
            {
                imageGeometry = appliedImageGeometry = TextureGeometry.FromAttributes(currentImage.Width, currentImage.Height, texture.Attributes);
                if (StatusText.Text == lastImageGuideError) StatusText.Text = "Image loaded.";
                lastImageGuideError = null;
            }
            catch (ThemeException error) { lastImageGuideError = texture.Name + ": " + error.Message; StatusText.Text = lastImageGuideError; }
            originalImage = edited == null ? currentImage : ImageCodec.Decode(File.ReadAllBytes(Contract.Contained(projectDirectory, edited.OriginalPath)), edited.OriginalPath);
            TextureSource.Text = edited == null ? texture.Source ?? fileName : "Project image\nOriginal: " + Path.GetExtension(edited.OriginalPath).TrimStart('.').ToUpperInvariant();
            ImageRecipeSummary.Text = project?.Edits.Recipes.TryGetValue(selected.Name, out var recipe) == true
                ? recipe.Operation == "pipeline" ? recipe.Parameters["steps"].GetArrayLength() + " retained edit steps" : "Edit: " + recipe.Operation : "Inherited image";
            changingImageDimensions = true;
            try
            {
                ImageWidth.Text = currentImage.Width.ToString(CultureInfo.InvariantCulture); ImageHeight.Text = currentImage.Height.ToString(CultureInfo.InvariantCulture);
                CropX.Text = CropY.Text = "0"; CropWidth.Text = ImageWidth.Text; CropHeight.Text = ImageHeight.Text;
                if (imageGeometry is { } geometry) SetGuideFields(geometry);
            }
            finally { changingImageDimensions = false; }
        }
        catch (Exception error) when (error is IOException or NotSupportedException or FormatException or ThemeException)
        { TextureSource.Text += "\n" + error.Message; }
        UpdateImageDisplay(); RefreshStretchPreviews(); UpdateCommandState();
    }
    private void SetGuideFields(TextureGeometry geometry)
    {
        SliceLeft.Text = geometry.Margins.Left.ToString(CultureInfo.InvariantCulture); SliceTop.Text = geometry.Margins.Top.ToString(CultureInfo.InvariantCulture);
        SliceRight.Text = geometry.Margins.Right.ToString(CultureInfo.InvariantCulture); SliceBottom.Text = geometry.Margins.Bottom.ToString(CultureInfo.InvariantCulture);
        ClipX.Text = geometry.Clip.Left.ToString(CultureInfo.InvariantCulture); ClipY.Text = (geometry.Height - geometry.Clip.Top).ToString(CultureInfo.InvariantCulture);
        ClipWidth.Text = geometry.ClipWidth.ToString(CultureInfo.InvariantCulture); ClipHeight.Text = geometry.ClipHeight.ToString(CultureInfo.InvariantCulture);
    }
    private void CompareImage(object sender, SelectionChangedEventArgs args)
    { if (EditCanvas != null) { UpdateImageDisplay(); RefreshStretchPreviews(); UpdateCommandState(); } }
    private void ToggleImageGuides(object sender, RoutedEventArgs args) { if (EditCanvas != null) UpdateImageDisplay(); }
    private void ZoomImage(object sender, SelectionChangedEventArgs args) { if (EditCanvas != null) SetImageZoom(); }
    private void ImageViewportChanged(object sender, SizeChangedEventArgs args) { if (EditCanvas != null && ImageZoom.SelectedIndex == 0) SetImageZoom(); }
    private void UpdateImageDisplay()
    {
        if (EditCanvas == null) return;
        var original = ImageComparison.SelectedIndex == 1; var image = original ? originalImage : currentImage;
        EditCanvas.SetImage(image, !original && ShowImageGuides.IsChecked == true ? imageGeometry : null);
        EditCanvas.AllowGuideEditing = CanEditImage; ImageDimensions.Text = image == null ? "" : $"{image.Width} × {image.Height}";
        SetImageZoom();
    }
    private void SetImageZoom()
    {
        var image = ImageComparison.SelectedIndex == 1 ? originalImage : currentImage;
        if (image == null) return;
        var zoom = ImageZoom.SelectedIndex switch { 1 => 1, 2 => 2, 3 => 4, 4 => 8, 5 => 16, _ => Math.Min((Math.Max(1, ImageViewport.ActualWidth) - 28) / image.Width, (Math.Max(1, ImageViewport.ActualHeight) - 28) / image.Height) };
        EditCanvas.SetZoom(zoom);
    }
    private void RefreshStretchPreviews()
    {
        if (StretchImage1 == null) return;
        var images = new[] { StretchImage1, StretchImage2, StretchImage3 }; var labels = new[] { StretchSize1, StretchSize2, StretchSize3 };
        foreach (var image in images) image.Source = null;
        foreach (var label in labels) label.Text = "";
        if (currentImage == null || imageGeometry is not { } geometry || ImageComparison.SelectedIndex != 0) return;
        var visible = geometry.VisibleImage(currentImage);
        var tiled = ImageSampleMode.SelectedIndex == 1;
        for (var index = 0; index < 3; ++index)
        {
            var width = Math.Max(new[] { 64, 100, 160 }[index], geometry.Margins.Left + geometry.Margins.Right + 1);
            var height = Math.Max(new[] { 24, 32, 48 }[index], geometry.Margins.Top + geometry.Margins.Bottom + 1);
            if ((long)width * height > 2_000_000) { labels[index].Text = "Sample too large"; continue; }
            images[index].Source = ImageCodec.ToBitmap(tiled ? visible.Tile(width, height) : visible.Stretch(width, height, geometry.Margins)); labels[index].Text = $"{width} × {height}";
        }
    }
    private void ImageSampleModeChanged(object sender, SelectionChangedEventArgs args) => RefreshStretchPreviews();
    private void RefreshImageEditors()
    {
        RefreshTextures();
        // Reusing the selected catalog item does not raise SelectionChanged after an edit.
        SelectTexture(this, null!);
    }
    private void ResetImageSelection(IEnumerable<string> names, string label)
    {
        if (!Ready()) return;
        CommitMetadata();
        var reset = ImageProject.Reset(projectDirectory, project!, catalog!, names, (bytes, file) => ImageCodec.EncodePng(ImageCodec.Decode(bytes, file)));
        if (reset) { changed = true; RefreshImageEditors(); SyncMetadata(); }
        StatusText.Text = reset ? label + " restored. Undo is available." : "Already inherited.";
    }
    private async void ResetSelectedImage(object sender, RoutedEventArgs args) => await Run(() =>
    {
        if (CanReplaceImage && SelectedTexture is { } texture) ResetImageSelection([texture.Name], "Image");
    });
    private async void ResetSelectedFamily(object sender, RoutedEventArgs args) => await Run(() =>
    {
        if (!CanReplaceImage || SelectedTexture is not { } texture || catalog == null) return;
        var family = TextureFamilies.Describe(texture.Name).Family;
        ResetImageSelection(catalog.Textures.Keys.Where(name => TextureFamilies.Describe(name).Family == family).ToArray(), family);
    });
    private async void EditStateFamily(object sender, RoutedEventArgs args) => await Run(() =>
    {
        if (!Ready() || !CanReplaceImage || SelectedTexture is not { } texture) return;
        var images = ImageFamilyEdit.Load(projectDirectory, project!, catalog!, texture.Name);
        var dialog = new ImageFamilyDialog(this, TextureFamilies.Describe(texture.Name).Family, images, texture.Name, ImageTint.Text);
        if (dialog.ShowDialog() == true) ApplyFamilyEdits(dialog.Edits, dialog.EditLabel);
    });
    private void ApplyFamilyEdits(ImageEdit[] edits, string label)
    {
        if (!Ready() || !CanReplaceImage) return;
        CommitMetadata();
        var count = ImageProject.SetPngBatch(projectDirectory, project!, catalog!, edits, label);
        if (count > 0) { changed = true; RefreshImageEditors(); SyncMetadata(); }
        StatusText.Text = count == 0 ? "Images already match." : count + " images updated. Undo restores the family edit.";
    }
    private async void ResetAllImages(object sender, RoutedEventArgs args) => await Run(() =>
    {
        if (!Ready()) return;
        CommitMetadata(); var reset = ImageProject.ResetAll(project!, catalog!);
        if (reset) { changed = true; RefreshImageEditors(); SyncMetadata(); }
        StatusText.Text = reset ? "All images restored. Undo is available." : "Images already inherited.";
    });
    private static int ImageNumber(TextBox input)
    {
        Contract.Require(int.TryParse(input.Text, NumberStyles.Integer, CultureInfo.InvariantCulture, out var value), "image", "Use a whole pixel value"); return value;
    }
    private static Recipe ImageStep(string operation, params (string Name, object Value)[] parameters)
        => new(operation, parameters.ToDictionary(parameter => parameter.Name, parameter => JsonSerializer.SerializeToElement(parameter.Value, Contract.JsonOptions), StringComparer.Ordinal));
    private static ThemeColor ImageColor(string value) => ThemeColor.Parse(ThemeColor.Parse(value).NativeText);
    private async Task EditImage(Func<RasterImage, RasterImage> transform, Func<TextureGeometry, TextureGeometry> guides, Recipe operation)
    {
        if (!Ready() || !CanEditImage || currentImage == null || imageGeometry is not { } geometry || SelectedTexture is not { } texture) return;
        CommitMetadata(); var source = currentImage; imageOperation = true; UpdateCommandState(); StatusText.Text = "Editing image…";
        try
        {
            var nextGeometry = guides(geometry); var nextImage = await Task.Run(() => transform(source));
            var png = ImageCodec.EncodePng(nextImage); ImageProject.SetPng(projectDirectory, project!, catalog!, texture.Name, png, nextGeometry, operation);
            changed = true; SelectTexture(this, null!); StatusText.Text = "Image updated. Original retained.";
        }
        catch (Exception error) { StatusText.Text = error.Message; }
        finally { imageOperation = false; UpdateCommandState(); }
    }
    private async void ReplaceImage(object sender, RoutedEventArgs args)
    {
        if (!Ready() || !CanReplaceImage || SelectedTexture is not { } texture) return;
        var picker = new OpenFileDialog { Title = "Import image", Filter = "Images|*.png;*.tga;*.bmp;*.jpg;*.jpeg|PNG|*.png|TGA|*.tga|BMP|*.bmp|JPEG|*.jpg;*.jpeg" };
        if (picker.ShowDialog(this) == true) await Run(() => ImportImageFile(picker.FileName));
    }
    private void ImportImageFile(string path)
    {
        if (!Ready() || !CanReplaceImage || SelectedTexture is not { } texture) return;
        var imported = ImageImport.Read(path); var image = imported.Image;
        var attributes = ImageProject.Resolve(projectDirectory, project!, catalog!, texture.Name).Attributes;
        var geometry = imageGeometry?.Resize(image.Width, image.Height) ?? TextureGeometry.FromAttributes(image.Width, image.Height, attributes);
        CommitMetadata(); ImageProject.SetPng(projectDirectory, project!, catalog!, texture.Name, imported.Png, geometry, ImageStep("replace"), imported.Original, imported.Extension);
        changed = true; RefreshImageEditors(); SyncMetadata(); StatusText.Text = imported.Extension == ".png" ? "PNG imported. Original bytes retained." : "Image imported as PNG. Original " + imported.Extension[1..].ToUpperInvariant() + " retained.";
    }
    private async void RepairImageGuides(object sender, RoutedEventArgs args) => await Run(() =>
    {
        if (!Ready() || !CanRepairImageGuides || currentImage == null || SelectedTexture is not { } texture) return;
        var geometry = new TextureGeometry(currentImage.Width, currentImage.Height, new(0, currentImage.Height, currentImage.Width, 0), new(0, 0, 0, 0));
        CommitMetadata(); ImageProject.SetGeometry(projectDirectory, project!, catalog!, texture.Name, geometry);
        changed = true; RefreshImageEditors(); SyncMetadata(); StatusText.Text = "Guides reset to the full image. Undo restores inherited guides.";
    });
    private async void TintImage(object sender, RoutedEventArgs args)
    {
        try { var tint = ImageColor(ImageTint.Text); await EditImage(image => image.Tint(tint), geometry => geometry, ImageStep("tint", ("color", tint.NativeText))); }
        catch (Exception error) { StatusText.Text = error.Message; }
    }
    private async void ResizeImage(object sender, RoutedEventArgs args)
    {
        try
        {
            var width = ImageNumber(ImageWidth); var height = ImageNumber(ImageHeight);
            var interpolation = ImageInterpolation.SelectedIndex == 0 ? Folderstorm.Theme.ImageInterpolation.Nearest : Folderstorm.Theme.ImageInterpolation.Bilinear;
            await EditImage(image => image.Resize(width, height, interpolation), geometry => geometry.Resize(width, height), ImageStep("resize", ("width", width), ("height", height), ("interpolation", interpolation == Folderstorm.Theme.ImageInterpolation.Nearest ? "nearest" : "bilinear")));
        }
        catch (Exception error) { StatusText.Text = error.Message; }
    }
    private async void CropImage(object sender, RoutedEventArgs args)
    {
        try
        {
            var x = ImageNumber(CropX); var y = ImageNumber(CropY); var width = ImageNumber(CropWidth); var height = ImageNumber(CropHeight);
            await EditImage(image => image.Crop(x, y, width, height), geometry => geometry.Crop(x, y, width, height), ImageStep("crop", ("x", x), ("y", y), ("width", width), ("height", height)));
        }
        catch (Exception error) { StatusText.Text = error.Message; }
    }
    private async void AdjustImage(object sender, RoutedEventArgs args)
    {
        var brightness = ImageBrightness.Value; var contrast = ImageContrast.Value; var saturation = ImageSaturation.Value; var gamma = ImageGamma.Value; var alpha = ImageAlpha.Value;
        await EditImage(image => image.Adjust(brightness, contrast, saturation, gamma, alpha), geometry => geometry,
            ImageStep("color-adjust", ("brightness", brightness), ("contrast", contrast), ("saturation", saturation), ("gamma", gamma), ("alpha_scale", alpha)));
    }
    private async void FillImage(object sender, RoutedEventArgs args)
    {
        try
        {
            var width = ImageNumber(ImageWidth); var height = ImageNumber(ImageHeight); var color = ImageColor(GenerateFirst.Text);
            await EditImage(_ => RasterImage.Fill(width, height, color), geometry => geometry.Resize(width, height), ImageStep("solid-fill", ("width", width), ("height", height), ("color", color.NativeText)));
        }
        catch (Exception error) { StatusText.Text = error.Message; }
    }
    private async void GradientImage(object sender, RoutedEventArgs args)
    {
        try
        {
            var width = ImageNumber(ImageWidth); var height = ImageNumber(ImageHeight); var first = ImageColor(GenerateFirst.Text); var last = ImageColor(GenerateLast.Text); var vertical = GradientDirection.SelectedIndex == 1;
            await EditImage(_ => RasterImage.Gradient(width, height, first, last, vertical), geometry => geometry.Resize(width, height), ImageStep("linear-gradient", ("width", width), ("height", height), ("first", first.NativeText), ("last", last.NativeText), ("vertical", vertical)));
        }
        catch (Exception error) { StatusText.Text = error.Message; }
    }
    private async void BorderImage(object sender, RoutedEventArgs args)
    {
        try { var width = ImageNumber(ImageBorderWidth); var color = ImageColor(GenerateFirst.Text); await EditImage(image => image.Border(width, color), geometry => geometry, ImageStep("border", ("thickness", width), ("color", color.NativeText))); }
        catch (Exception error) { StatusText.Text = error.Message; }
    }
    private async void SetImageGuides(object sender, RoutedEventArgs args) => await Run(() =>
    {
        if (!CanEditImage || currentImage == null) return;
        var x = ImageNumber(ClipX); var y = ImageNumber(ClipY); var width = ImageNumber(ClipWidth); var height = ImageNumber(ClipHeight);
        var clip = new NativeSlice(x, checked(currentImage.Height - y), checked(x + width), checked(currentImage.Height - y - height));
        ApplyGuides(new(currentImage.Width, currentImage.Height, clip, new(ImageNumber(SliceLeft), ImageNumber(SliceTop), ImageNumber(SliceRight), ImageNumber(SliceBottom))));
    });
    private void ApplyGuides(TextureGeometry geometry)
    {
        if (!Ready() || !CanEditImage || SelectedTexture is not { } texture || geometry == appliedImageGeometry) return;
        CommitMetadata(); ImageProject.SetGeometry(projectDirectory, project!, catalog!, texture.Name, geometry); changed = true;
        SelectTexture(this, null!); StatusText.Text = "Image guides applied.";
    }
    private void ImageWidthChanged(object sender, TextChangedEventArgs args)
    {
        if (changingImageDimensions || KeepImageRatio?.IsChecked != true || currentImage == null || ImageHeight == null || !int.TryParse(ImageWidth.Text, out var width) || width <= 0) return;
        changingImageDimensions = true; try { ImageHeight.Text = Math.Max(1, Math.Round((double)width * currentImage.Height / currentImage.Width)).ToString(CultureInfo.InvariantCulture); } finally { changingImageDimensions = false; }
    }
    private void ImageHeightChanged(object sender, TextChangedEventArgs args)
    {
        if (changingImageDimensions || KeepImageRatio?.IsChecked != true || currentImage == null || ImageWidth == null || !int.TryParse(ImageHeight.Text, out var height) || height <= 0) return;
        changingImageDimensions = true; try { ImageWidth.Text = Math.Max(1, Math.Round((double)height * currentImage.Width / currentImage.Height)).ToString(CultureInfo.InvariantCulture); } finally { changingImageDimensions = false; }
    }
}
