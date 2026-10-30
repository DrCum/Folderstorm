// SPDX-License-Identifier: LGPL-2.1-or-later
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using System.Windows.Threading;

namespace Folderstorm.Theme.Editor;

public readonly record struct ScreenPoint(int X, int Y);
public readonly record struct ScreenBounds(int X, int Y, int Width, int Height)
{
    public bool Contains(ScreenPoint point) => Width > 0 && Height > 0 && point.X >= X && point.Y >= Y && (long)point.X < (long)X + Width && (long)point.Y < (long)Y + Height;
}
public static class ScreenPixel
{
    [StructLayout(LayoutKind.Sequential)] private struct NativePoint { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] private struct NativeRect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] private struct MonitorInfo { public int Size; public NativeRect Monitor, Work; public uint Flags; }
    [DllImport("user32.dll")] private static extern int GetSystemMetrics(int index);
    [DllImport("user32.dll")] private static extern nint GetDC(nint window);
    [DllImport("user32.dll")] private static extern int ReleaseDC(nint window, nint dc);
    [DllImport("gdi32.dll")] private static extern uint GetPixel(nint dc, int x, int y);
    [DllImport("gdi32.dll")] private static extern nint CreateCompatibleDC(nint dc);
    [DllImport("gdi32.dll")] private static extern nint CreateCompatibleBitmap(nint dc, int width, int height);
    [DllImport("gdi32.dll")] private static extern nint SelectObject(nint dc, nint item);
    [DllImport("gdi32.dll")] private static extern bool DeleteObject(nint item);
    [DllImport("gdi32.dll")] private static extern bool DeleteDC(nint dc);
    [DllImport("gdi32.dll", SetLastError = true)] private static extern bool BitBlt(nint destination, int x, int y, int width, int height, nint source, int sourceX, int sourceY, uint operation);
    [DllImport("dwmapi.dll")] private static extern int DwmFlush();
    [DllImport("user32.dll")] private static extern nint MonitorFromPoint(NativePoint point, uint flags);
    [DllImport("user32.dll")] private static extern nint MonitorFromWindow(nint window, uint flags);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern bool GetMonitorInfo(nint monitor, ref MonitorInfo info);
    [DllImport("user32.dll")] private static extern uint GetDpiForWindow(nint window);
    [DllImport("user32.dll", SetLastError = true)] private static extern bool SetWindowPos(nint window, nint after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] private static extern bool ClientToScreen(nint window, ref NativePoint point);
    public static ScreenPoint DecodeMousePoint(long value) => new((short)(value & 0xffff), (short)((value >> 16) & 0xffff));
    public static ScreenPoint? MessagePoint(nint window, nint value)
    {
        var point = DecodeMousePoint(value.ToInt64()); var native = new NativePoint { X = point.X, Y = point.Y };
        return ClientToScreen(window, ref native) ? new ScreenPoint(native.X, native.Y) : null;
    }
    public static ThemeColor FromColorRef(uint value, double alpha)
    {
        Contract.Require(value != uint.MaxValue, "screen_color", "Screen color could not be read. Try Screen pick again.");
        return ThemeColor.Create((value & 255) / 255d, ((value >> 8) & 255) / 255d, ((value >> 16) & 255) / 255d, alpha);
    }
    public static ScreenBounds VirtualBounds() => new(GetSystemMetrics(76), GetSystemMetrics(77), GetSystemMetrics(78), GetSystemMetrics(79));
    public static ScreenPoint FromScreenPoint(Point point)
    {
        Contract.Require(double.IsFinite(point.X) && double.IsFinite(point.Y) && point.X >= int.MinValue && point.X <= int.MaxValue && point.Y >= int.MinValue && point.Y <= int.MaxValue,
            "screen_color", "Invalid screen coordinates");
        return new((int)Math.Floor(point.X), (int)Math.Floor(point.Y));
    }
    public static ThemeColor Read(ScreenPoint point, double alpha)
    {
        Contract.Require(VirtualBounds().Contains(point) && MonitorFromPoint(new() { X = point.X, Y = point.Y }, 0) != 0, "screen_color", "Display changed. Try Screen pick again.");
        Contract.Require(DwmFlush() >= 0, "screen_color", "Screen compositor is unavailable. Try Screen pick again.");
        var dc = GetDC(0); Contract.Require(dc != 0, "screen_color", "Screen is unavailable");
        nint memory = 0, bitmap = 0, previous = 0;
        try
        {
            memory = CreateCompatibleDC(dc); bitmap = CreateCompatibleBitmap(dc, 1, 1);
            Contract.Require(memory != 0 && bitmap != 0, "screen_color", "Screen sample could not be prepared");
            previous = SelectObject(memory, bitmap); Contract.Require(previous != 0 && previous != new nint(-1), "screen_color", "Screen sample could not be selected");
            if (!BitBlt(memory, 0, 0, 1, 1, dc, point.X, point.Y, 0x00CC0020 | 0x40000000)) throw new Win32Exception(Marshal.GetLastWin32Error());
            return FromColorRef(GetPixel(memory, 0, 0), alpha);
        }
        finally
        {
            if (previous != 0 && previous != new nint(-1)) SelectObject(memory, previous);
            if (bitmap != 0) DeleteObject(bitmap); if (memory != 0) DeleteDC(memory); ReleaseDC(0, dc);
        }
    }
    public static void CoverDesktop(Window window)
    {
        var area = VirtualBounds();
        Contract.Require(area.Width > 0 && area.Height > 0, "screen_color", "No display is available");
        if (!SetWindowPos(new WindowInteropHelper(window).Handle, new(-1), area.X, area.Y, area.Width, area.Height, 0x0010 | 0x0200))
            throw new Win32Exception(Marshal.GetLastWin32Error());
    }
    public static Size DialogLimit(ScreenBounds work, double dpi)
    {
        Contract.Require(double.IsFinite(dpi) && dpi > 0 && work.Width > 0 && work.Height > 0, "screen_color", "Invalid display dimensions");
        return new(Math.Max(1, work.Width * 96 / dpi - 24), Math.Max(1, work.Height * 96 / dpi - 24));
    }
    public static void FitDialog(Window window)
    {
        var own = new WindowInteropHelper(window).Handle; var owner = window.Owner == null ? own : new WindowInteropHelper(window.Owner).Handle;
        var monitor = MonitorFromWindow(owner, 2); var info = new MonitorInfo { Size = Marshal.SizeOf<MonitorInfo>() };
        if (!GetMonitorInfo(monitor, ref info)) return;
        var dpi = GetDpiForWindow(owner); var limit = DialogLimit(new(info.Work.Left, info.Work.Top, info.Work.Right - info.Work.Left, info.Work.Bottom - info.Work.Top), dpi == 0 ? 96 : dpi);
        window.MinWidth = Math.Min(window.MinWidth, limit.Width); window.MinHeight = Math.Min(window.MinHeight, limit.Height);
        window.MaxWidth = limit.Width; window.MaxHeight = limit.Height;
    }
}

public sealed class ScreenColorPicker : Window
{
    private readonly double alpha;
    private readonly Canvas canvas = new();
    private readonly Border hint;
    private bool sampling, closed, finishing, presented;
    private DispatcherOperation? closeRequest;
    private ScreenPoint? mouseDownPoint;
    public ThemeColor? Result { get; private set; }
    public Exception? Error { get; private set; }
    public ScreenColorPicker(Window? owner, double opacity)
    {
        if (owner != null) Owner = owner;
        alpha = opacity; Title = "Pick screen color"; WindowStyle = WindowStyle.None; AllowsTransparency = true; ResizeMode = ResizeMode.NoResize;
        // A named task window lets keyboard and assistive tools target this mode without activating its disabled owner.
        ShowInTaskbar = true; Topmost = true; Cursor = Cursors.Cross; Background = new SolidColorBrush(Color.FromArgb(1, 0, 0, 0));
        Width = Height = 1; Content = canvas;
        hint = new Border { Background = new SolidColorBrush(Color.FromRgb(17, 16, 22)), Padding = new Thickness(12, 8, 12, 8), CornerRadius = new CornerRadius(4),
            Child = new TextBlock { Text = "Click to pick · Escape or right-click cancels", Foreground = Brushes.White } };
        canvas.Children.Add(hint);
        SourceInitialized += (_, _) => (PresentationSource.FromVisual(this) as HwndSource)?.AddHook(RecordClick);
        Loaded += (_, _) => { presented = true; try { ScreenPixel.CoverDesktop(this); Activate(); Focus(); } catch (Exception error) { Finish(null, error); } };
        MouseMove += (_, args) => { var point = args.GetPosition(canvas); Canvas.SetLeft(hint, Math.Clamp(point.X + 24, 0, Math.Max(0, ActualWidth - hint.ActualWidth))); Canvas.SetTop(hint, Math.Clamp(point.Y + 24, 0, Math.Max(0, ActualHeight - hint.ActualHeight))); };
        PreviewKeyDown += (_, args) => { if (args.Key == Key.Escape) { args.Handled = true; Finish(null); } };
        MouseRightButtonDown += (_, args) => { args.Handled = true; Finish(null); };
        Deactivated += (_, _) => { if (presented && IsVisible) Finish(null); };
        Closed += (_, _) => closed = true;
        MouseLeftButtonDown += async (_, args) =>
        {
            if (sampling) return; args.Handled = true; sampling = true;
            try
            {
                // Prefer the message's clicked pixel over the global cursor, which may have moved while input was queued.
                var point = mouseDownPoint ?? ScreenPixel.FromScreenPoint(PointToScreen(args.GetPosition(this))); mouseDownPoint = null; Opacity = 0;
                // Keep the modal window alive while its transparent frame reaches the compositor.
                await Dispatcher.InvokeAsync(() => { }, DispatcherPriority.ApplicationIdle);
                if (closed || finishing) return;
                var result = await Task.Run(() => ScreenPixel.Read(point, alpha));
                Finish(result);
            }
            catch (Exception error) { Finish(null, error); }
        };
    }
    private nint RecordClick(nint window, int message, nint wParam, nint lParam, ref bool handled)
    {
        if (message == 0x0201 && !sampling && !finishing) mouseDownPoint = ScreenPixel.MessagePoint(window, lParam);
        return 0;
    }
    private void Finish(ThemeColor? result, Exception? error = null)
    {
        if (closed || finishing) return;
        finishing = true; Result = result; Error = error;
        // Deactivation can arrive after ShowDialog has stopped being modal. Close once, outside the input callback.
        closeRequest = Dispatcher.InvokeAsync(() => { if (!closed) Close(); });
    }
    protected override void OnClosing(CancelEventArgs args) { finishing = true; base.OnClosing(args); }
    public static ThemeColor? Pick(Window owner, ThemeColor current)
    {
        var picker = new ScreenColorPicker(owner, current.A); picker.ShowDialog();
        if (picker.Error != null) throw picker.Error; return picker.Result;
    }
}
