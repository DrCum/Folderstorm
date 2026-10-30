// SPDX-License-Identifier: LGPL-2.1-or-later
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;

namespace Folderstorm.Theme.Editor;

public sealed class ColorSpectrum : FrameworkElement
{
    private double hue, saturation, value;
    public event Action<double, double>? Changed;
    public ColorSpectrum() { Focusable = true; Cursor = Cursors.Cross; }
    public void SetColor(HsvColor color) { hue = color.Hue; saturation = color.Saturation; value = color.Value; InvalidateVisual(); }
    protected override void OnRender(DrawingContext drawing)
    {
        base.OnRender(drawing);
        var color = new HsvColor(hue, 1, 1, 1).ToColor();
        var fill = new SolidColorBrush(Color.FromRgb((byte)Math.Round(color.R * 255), (byte)Math.Round(color.G * 255), (byte)Math.Round(color.B * 255)));
        var area = new Rect(0, 0, ActualWidth, ActualHeight);
        drawing.DrawRectangle(fill, null, area);
        drawing.DrawRectangle(new LinearGradientBrush(Colors.White, Colors.Transparent, new Point(0, .5), new Point(1, .5)), null, area);
        drawing.DrawRectangle(new LinearGradientBrush(Color.FromArgb(0, 0, 0, 0), Colors.Black, new Point(.5, 0), new Point(.5, 1)), null, area);
        var point = new Point(saturation * ActualWidth, (1 - value) * ActualHeight);
        drawing.DrawEllipse(null, new Pen(Brushes.Black, 3), point, 6, 6);
        drawing.DrawEllipse(null, new Pen(Brushes.White, 1.5), point, 6, 6);
        if (IsKeyboardFocused) drawing.DrawRectangle(null, new Pen(Brushes.White, 1), area);
    }
    private void Choose(Point point)
    {
        saturation = Math.Clamp(point.X / Math.Max(1, ActualWidth), 0, 1); value = 1 - Math.Clamp(point.Y / Math.Max(1, ActualHeight), 0, 1);
        Changed?.Invoke(saturation, value); InvalidateVisual();
    }
    protected override void OnMouseLeftButtonDown(MouseButtonEventArgs args)
    { base.OnMouseLeftButtonDown(args); Focus(); if (CaptureMouse()) Choose(args.GetPosition(this)); args.Handled = true; }
    protected override void OnMouseMove(MouseEventArgs args)
    { base.OnMouseMove(args); if (IsMouseCaptured && args.LeftButton == MouseButtonState.Pressed) Choose(args.GetPosition(this)); }
    protected override void OnMouseLeftButtonUp(MouseButtonEventArgs args)
    { base.OnMouseLeftButtonUp(args); if (IsMouseCaptured) { Choose(args.GetPosition(this)); ReleaseMouseCapture(); args.Handled = true; } }
    protected override void OnKeyDown(KeyEventArgs args)
    {
        var step = Keyboard.Modifiers.HasFlag(ModifierKeys.Shift) ? .1 : .01;
        if (args.Key is Key.Left or Key.Right or Key.Up or Key.Down)
        {
            saturation = Math.Clamp(saturation + (args.Key == Key.Left ? -step : args.Key == Key.Right ? step : 0), 0, 1);
            value = Math.Clamp(value + (args.Key == Key.Down ? -step : args.Key == Key.Up ? step : 0), 0, 1);
            Changed?.Invoke(saturation, value); InvalidateVisual(); args.Handled = true;
        }
        base.OnKeyDown(args);
    }
}
