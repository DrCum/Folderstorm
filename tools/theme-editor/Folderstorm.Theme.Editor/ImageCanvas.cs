// SPDX-License-Identifier: LGPL-2.1-or-later
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace Folderstorm.Theme.Editor;

public sealed class ImageCanvas : FrameworkElement
{
    private RasterImage? image;
    private BitmapSource? bitmap;
    private TextureGeometry? geometry;
    private TextureGeometry? dragStart;
    private int guide = -1;
    public double Zoom { get; private set; } = 1;
    public bool AllowGuideEditing { get; set; } = true;
    public bool IsGuideDragging => guide >= 0;
    public event Action<int, int, RgbaPixel>? PixelHovered;
    public event Action<RgbaPixel>? PixelChosen;
    public event Action<TextureGeometry>? GuidesMoved;
    public event Action<TextureGeometry>? GuidesCommitted;
    public event Action? GuideDragStateChanged;
    public ImageCanvas() { Focusable = true; Cursor = Cursors.Cross; RenderOptions.SetBitmapScalingMode(this, BitmapScalingMode.NearestNeighbor); }
    public void SetImage(RasterImage? value, TextureGeometry? guides)
    {
        var wasDragging = guide >= 0; guide = -1; dragStart = null; if (IsMouseCaptured) ReleaseMouseCapture();
        image = value; bitmap = value == null ? null : ImageCodec.ToBitmap(value); geometry = guides;
        if (wasDragging) GuideDragStateChanged?.Invoke();
        InvalidateMeasure(); InvalidateVisual();
    }
    public void SetZoom(double value)
    {
        Zoom = Math.Clamp(value, .05, 32); InvalidateMeasure(); InvalidateVisual();
    }
    protected override Size MeasureOverride(Size availableSize) => image == null ? new(1, 1) : new(image.Width * Zoom, image.Height * Zoom);
    protected override void OnRender(DrawingContext drawing)
    {
        base.OnRender(drawing); if (image == null || bitmap == null) return;
        drawing.PushTransform(new ScaleTransform(Zoom, Zoom));
        drawing.DrawImage(bitmap, new(0, 0, image.Width, image.Height));
        if (geometry is { } bounds)
        {
            var clip = new Rect(bounds.Clip.Left, image.Height - bounds.Clip.Top, bounds.ClipWidth, bounds.ClipHeight);
            var dim = new SolidColorBrush(Color.FromArgb(140, 0, 0, 0));
            drawing.DrawRectangle(dim, null, new(0, 0, image.Width, clip.Top));
            drawing.DrawRectangle(dim, null, new(0, clip.Bottom, image.Width, image.Height - clip.Bottom));
            drawing.DrawRectangle(dim, null, new(0, clip.Top, clip.Left, clip.Height));
            drawing.DrawRectangle(dim, null, new(clip.Right, clip.Top, image.Width - clip.Right, clip.Height));
            drawing.DrawRectangle(null, new Pen(new SolidColorBrush(Color.FromRgb(100, 216, 178)), 1 / Zoom) { DashStyle = DashStyles.Dash }, clip);
            var pen = new Pen(new SolidColorBrush(Color.FromRgb(240, 204, 83)), 1 / Zoom);
            var left = clip.Left + bounds.Margins.Left; var right = clip.Right - bounds.Margins.Right;
            var top = clip.Top + bounds.Margins.Top; var bottom = clip.Bottom - bounds.Margins.Bottom;
            drawing.DrawLine(pen, new(left, clip.Top), new(left, clip.Bottom)); drawing.DrawLine(pen, new(right, clip.Top), new(right, clip.Bottom));
            drawing.DrawLine(pen, new(clip.Left, top), new(clip.Right, top)); drawing.DrawLine(pen, new(clip.Left, bottom), new(clip.Right, bottom));
        }
        drawing.Pop();
    }
    private (int X, int Y)? PixelAt(MouseEventArgs args)
    {
        if (image == null) return null; var point = args.GetPosition(this);
        var x = (int)Math.Floor(point.X / Zoom); var y = (int)Math.Floor(point.Y / Zoom);
        return x >= 0 && y >= 0 && x < image.Width && y < image.Height ? (x, y) : null;
    }
    protected override void OnMouseLeftButtonDown(MouseButtonEventArgs args)
    {
        base.OnMouseLeftButtonDown(args); if (image == null || PixelAt(args) is not { } pixel) return;
        Focus();
        if (AllowGuideEditing && geometry is { } bounds)
        {
            var point = args.GetPosition(this); var top = image.Height - bounds.Clip.Top; var bottom = image.Height - bounds.Clip.Bottom;
            var candidates = new[] { (bounds.Clip.Left + bounds.Margins.Left) * Zoom, (bounds.Clip.Right - bounds.Margins.Right) * Zoom,
                (top + bounds.Margins.Top) * Zoom, (bottom - bounds.Margins.Bottom) * Zoom };
            var distances = candidates.Select((value, index) => Math.Abs(value - (index < 2 ? point.X : point.Y))).ToArray();
            var nearest = Array.IndexOf(distances, distances.Min());
            if (distances[nearest] <= 6 && pixel.X >= bounds.Clip.Left && pixel.X < bounds.Clip.Right && pixel.Y >= top && pixel.Y < bottom)
            {
                guide = nearest; dragStart = bounds;
                if (!CaptureMouse()) { guide = -1; dragStart = null; return; }
                GuideDragStateChanged?.Invoke(); args.Handled = true; return;
            }
        }
        PixelChosen?.Invoke(image.Pixel(pixel.X, pixel.Y)); args.Handled = true;
    }
    protected override void OnMouseMove(MouseEventArgs args)
    {
        base.OnMouseMove(args); if (image == null) return;
        if (guide >= 0 && geometry is { } bounds)
        {
            var point = args.GetPosition(this); var x = (int)Math.Round(point.X / Zoom); var y = (int)Math.Round(point.Y / Zoom);
            var margins = bounds.Margins;
            margins = guide switch
            {
                0 => margins with { Left = Math.Clamp(x - bounds.Clip.Left, 0, bounds.ClipWidth - margins.Right) },
                1 => margins with { Right = Math.Clamp(bounds.Clip.Right - x, 0, bounds.ClipWidth - margins.Left) },
                2 => margins with { Top = Math.Clamp(y - (image.Height - bounds.Clip.Top), 0, bounds.ClipHeight - margins.Bottom) },
                _ => margins with { Bottom = Math.Clamp(image.Height - bounds.Clip.Bottom - y, 0, bounds.ClipHeight - margins.Top) }
            };
            geometry = bounds with { Margins = margins }; GuidesMoved?.Invoke(geometry.Value); InvalidateVisual(); args.Handled = true;
        }
        else if (PixelAt(args) is { } pixel) PixelHovered?.Invoke(pixel.X, pixel.Y, image.Pixel(pixel.X, pixel.Y));
    }
    protected override void OnMouseLeftButtonUp(MouseButtonEventArgs args)
    {
        base.OnMouseLeftButtonUp(args); if (guide < 0) return;
        guide = -1; dragStart = null; ReleaseMouseCapture();
        GuideDragStateChanged?.Invoke();
        if (geometry is { } bounds) GuidesCommitted?.Invoke(bounds); args.Handled = true;
    }
    protected override void OnKeyDown(KeyEventArgs args)
    {
        if (args.Key == Key.Escape && guide >= 0)
        {
            geometry = dragStart; dragStart = null; guide = -1; ReleaseMouseCapture();
            GuideDragStateChanged?.Invoke();
            if (geometry is { } bounds) GuidesMoved?.Invoke(bounds); InvalidateVisual(); args.Handled = true;
        }
        base.OnKeyDown(args);
    }
    protected override void OnLostMouseCapture(MouseEventArgs args)
    {
        if (guide >= 0) { geometry = dragStart; guide = -1; dragStart = null; GuideDragStateChanged?.Invoke(); if (geometry is { } bounds) GuidesMoved?.Invoke(bounds); InvalidateVisual(); }
        base.OnLostMouseCapture(args);
    }
}
