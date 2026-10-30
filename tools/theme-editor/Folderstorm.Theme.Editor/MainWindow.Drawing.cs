// SPDX-License-Identifier: LGPL-2.1-or-later
using System.Windows;
using System.Windows.Controls;

namespace Folderstorm.Theme.Editor;

public partial class MainWindow
{
    private void PickFieldColor(object sender, RoutedEventArgs args)
    {
        if (sender is not Button { Tag: TextBox input } || !input.IsEnabled) return;
        ThemeColor color;
        try { color = ThemeColor.Parse(input.Text); } catch (ThemeException) { color = ThemeColor.Parse("#FFFFFFFF"); }
        var picker = new ColorPickerDialog(color) { Owner = this };
        if (picker.ShowDialog() == true) input.Text = picker.SelectedColor.Hex();
    }
    private void DrawingSettingsChanged(object sender, RoutedEventArgs args)
    {
        if (EditCanvas == null || DrawingColor == null || DrawingSize == null || ImageTool == null || PixelGrid == null) return;
        EditCanvas.Tool = (PixelTool)Math.Max(0, ImageTool.SelectedIndex);
        EditCanvas.ToolTip = EditCanvas.Tool == PixelTool.Circle ? "Drag from center to edge. Escape cancels." :
            EditCanvas.Tool == PixelTool.Inspect ? "Click to pick a pixel color. Drag a yellow guide to move it." : "Drag to draw pixels. Escape cancels.";
        EditCanvas.ShowPixelGrid = PixelGrid.IsChecked == true;
        try
        {
            EditCanvas.DrawingColor = ThemeColor.Parse(DrawingColor.Text);
            EditCanvas.BrushSize = DrawingSize.SelectedIndex switch { 1 => 2, 2 => 4, 3 => 8, _ => 1 };
            EditCanvas.AllowDrawing = CanEditImage;
        }
        catch (ThemeException) { EditCanvas.AllowDrawing = false; }
        EditCanvas.AllowGuideEditing = CanEditImage && ShowImageGuides.IsChecked == true && EditCanvas.Tool == PixelTool.Inspect;
        EditCanvas.InvalidateVisual();
    }
    private void CommitDrawing(RasterImage image, Recipe recipe)
    {
        _ = Run(() =>
        {
            try
            {
                if (!Ready() || !CanEditImage || SelectedTexture is not { } texture || imageGeometry is not { } geometry) return;
                CommitMetadata(); ImageProject.SetPng(projectDirectory, project!, catalog!, texture.Name, ImageCodec.EncodePng(image), geometry, recipe);
                changed = true; StatusText.Text = "Drawing applied. Undo is available.";
            }
            finally { RefreshImageEditors(); SyncMetadata(); }
        });
    }
    private async void ClearDrawing(object sender, RoutedEventArgs args)
        => await EditImage(image => RasterImage.Fill(image.Width, image.Height, ThemeColor.Parse("#00000000")), geometry => geometry,
            ImageStep("solid-fill", ("color", "#00000000")));
}
