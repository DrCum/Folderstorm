// SPDX-License-Identifier: LGPL-2.1-or-later
using Folderstorm.Theme;
using Folderstorm.Theme.Editor;
using System.IO;
using System.Reflection;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

internal static class PaletteExtrasChecks
{
    private const BindingFlags Private = BindingFlags.Instance | BindingFlags.NonPublic;
    private static void Assert(bool value, string message) { if (!value) throw new Exception(message); }
    private static T Field<T>(ColorPickerDialog picker, string name) => (T)typeof(ColorPickerDialog).GetField(name, Private)!.GetValue(picker)!;
    private static void Wait(Task task)
    {
        var frame = new DispatcherFrame(); var deadline = DateTime.UtcNow.AddSeconds(12);
        var timer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(10) };
        timer.Tick += (_, _) => { if (task.IsCompleted || DateTime.UtcNow >= deadline) frame.Continue = false; };
        timer.Start(); if (!task.IsCompleted) Dispatcher.PushFrame(frame); timer.Stop();
        Assert(task.IsCompleted, "Swatch operation timed out"); task.GetAwaiter().GetResult();
    }
    public static void Run(string root)
    {
        var directory = Path.Combine(root, "palette-extras"); Directory.CreateDirectory(directory);
        var store = new ColorSwatchStore(Path.Combine(directory, "saved.json")); var color = ThemeColor.Parse("#11223300");
        Assert(store.Save(color).Colors.SequenceEqual(new[] { color.Hex() }) && new ColorSwatchStore(store.FilePath).Load().Colors[0] == "#11223300", "Saved swatch lost transparent RGB or did not persist");
        var bytes = File.ReadAllBytes(store.FilePath); var stamp = File.GetLastWriteTimeUtc(store.FilePath);
        store.Save(color); Assert(bytes.SequenceEqual(File.ReadAllBytes(store.FilePath)) && stamp == File.GetLastWriteTimeUtc(store.FilePath), "Duplicate swatch rewrote preferences");
        for (var index = 1; index < ColorSwatchStore.Limit; ++index) store.Save(ThemeColor.Create(index / 255d, .4, .6));
        bytes = File.ReadAllBytes(store.FilePath);
        try { store.Save(ThemeColor.Parse("#FEFD80")); throw new Exception("Full swatch list silently discarded a color"); } catch (ThemeException error) when (error.Code == "swatches_full") { }
        Assert(bytes.SequenceEqual(File.ReadAllBytes(store.FilePath)), "Full swatch list was changed");
        store.Remove(color); Assert(store.Save(ThemeColor.Parse("#FEFD80")).Colors.Length == 32, "Explicit swatch removal did not free a slot");
        var futurePath = Path.Combine(directory, "future.json"); File.WriteAllText(futurePath, "{\"version\":2,\"colors\":[\"#12345678\"],\"future\":true}");
        var future = new ColorSwatchStore(futurePath); bytes = File.ReadAllBytes(futurePath);
        Assert(!future.Load().CanWrite, "Unknown swatch format was writable");
        try { future.Save(color); throw new Exception("Unknown swatches overwritten"); } catch (ThemeException error) when (error.Code == "swatches_readonly") { }
        Assert(bytes.SequenceEqual(File.ReadAllBytes(futurePath)), "Unknown swatch bytes were lost");
        var malformedPath = Path.Combine(directory, "malformed.json"); File.WriteAllText(malformedPath, "{\"version\":1,\"colors\":[null]}");
        Assert(!new ColorSwatchStore(malformedPath).Load().CanWrite, "Null swatch was accepted");
        var concurrentPath = Path.Combine(directory, "concurrent.json");
        Task.WhenAll(Enumerable.Range(1, 8).Select(index => Task.Run(() => new ColorSwatchStore(concurrentPath).Save(ThemeColor.Create(index / 255d, .7, .8))))).GetAwaiter().GetResult();
        Assert(new ColorSwatchStore(concurrentPath).Load().Colors.Length == 8, "Concurrent lab saves lost swatches");
        Console.WriteLine("PASS swatch persistence/alpha/hidden RGB, duplicate no-op, capacity, unknown/malformed preservation and concurrent merge");

        var pickerPath = Path.Combine(directory, "picker.json"); var samplerMode = 0;
        ThemeColor? Sample(Window _, ThemeColor current) => samplerMode switch { 0 => null, 1 => ScreenPixel.FromColorRef(0x00563412, current.A), _ => throw new ThemeException("screen_color", "Injected sample failure") };
        var picker = new ColorPickerDialog(ThemeColor.Parse("#11223380"), pickerPath, Sample);
        Task Change(bool remove) => (Task)typeof(ColorPickerDialog).GetMethod("ChangeSwatch", Private)!.Invoke(picker, [remove])!;
        void Pick() => typeof(ColorPickerDialog).GetMethod("PickScreen", Private)!.Invoke(picker, [picker, new RoutedEventArgs()]);
        Wait(Change(false));
        Assert(Field<WrapPanel>(picker, "SavedColors").Children.Count == 1 && Field<Button>(picker, "RemoveSwatch").IsEnabled, "Save swatch handler did not refresh choices/commands");
        var reopened = new ColorPickerDialog(ThemeColor.Parse("#FFFFFF"), pickerPath, Sample);
        ((Button)Field<WrapPanel>(reopened, "SavedColors").Children[0]).RaiseEvent(new RoutedEventArgs(Button.ClickEvent));
        Assert(reopened.SelectedColor.Hex() == "#11223380", "Saved swatch did not restore color/opacity"); reopened.Close();
        Wait(Change(true)); Assert(Field<WrapPanel>(picker, "SavedColors").Children.Count == 0, "Remove swatch handler left stale choices");
        Field<TextBox>(picker, "Hex").Text = "#invalid";
        Assert(!Field<Button>(picker, "SaveSwatch").IsEnabled && !Field<Button>(picker, "UseColor").IsEnabled, "Invalid color enabled saving/accepting");
        var readonlyPicker = new ColorPickerDialog(ThemeColor.Parse("#12345680"), futurePath, Sample);
        Assert(Field<Button>(readonlyPicker, "UseColor").IsEnabled && !Field<Button>(readonlyPicker, "SaveSwatch").IsEnabled, "Unknown saved preferences prevented ordinary color selection"); readonlyPicker.Close();
        Console.WriteLine("PASS saved-swatch picker save/select/remove refresh, reopen and invalid/read-only gating");

        Field<TextBox>(picker, "Hex").Text = "#11223380"; var original = picker.SelectedColor;
        Pick(); Assert(picker.SelectedColor == original && Field<TextBox>(picker, "Hex").Text == original.Hex(), "Injected screen Cancel changed the color");
        samplerMode = 1; Pick(); Assert(picker.SelectedColor.Hex() == "#12345680" && Field<Slider>(picker, "Alpha").Value == 128, "Injected screen sample changed opacity or left stale fields");
        samplerMode = 2; var accepted = picker.SelectedColor; Pick();
        Assert(picker.SelectedColor == accepted && Field<TextBlock>(picker, "ErrorText").Visibility == Visibility.Visible && Field<Button>(picker, "UseColor").IsEnabled, "Screen error lost the previous selection");
        Console.WriteLine("PASS injected screen-sample RGB/alpha, Cancel and error picker handlers; no desktop pixel was read");
        picker.Close();

        Assert(ScreenPixel.FromColorRef(0x00F08022, .25).Hex() == "#2280F040" && ScreenPixel.FromColorRef(0x00F08022, 0).Hex() == "#2280F000", "COLORREF channel order or hidden RGB changed");
        try { ScreenPixel.FromColorRef(uint.MaxValue, 1); throw new Exception("Invalid native sample accepted"); } catch (ThemeException error) when (error.Code == "screen_color") { }
        var bounds = new ScreenBounds(-3840, -2160, 5760, 3240);
        Assert(ScreenPixel.FromScreenPoint(new Point(-.25, 300.75)) == new ScreenPoint(-1, 300), "Clicked screen pixels were rounded across a boundary");
        Assert(ScreenPixel.DecodeMousePoint((300L << 16) | 1700) == new ScreenPoint(1700, 300) && ScreenPixel.DecodeMousePoint((20L << 16) | 0xfffb) == new ScreenPoint(-5, 20), "Queued mouse message coordinates were decoded incorrectly");
        Assert(bounds.Contains(new(-3840, -2160)) && bounds.Contains(new(-1, -1)) && !bounds.Contains(new(1920, 1080)) && !bounds.Contains(new(-3841, 0)), "Physical virtual-screen boundaries failed");
        Assert(ScreenPixel.DialogLimit(new(0, 0, 1920, 1080), 192) == new Size(936, 516), "200% dialog work-area conversion failed");
        var full = new ColorPickerDialog(ThemeColor.Parse("#64D8B280"), store.FilePath, Sample);
        var visual = (FrameworkElement)full.Content; full.Content = null; ((Panel)visual).Background = full.Background;
        foreach (var (width, height, bottom) in new[] { (470, 680, false), (470, 680, true), (360, 360, true) })
        {
            visual.Measure(new Size(width, height)); visual.Arrange(new Rect(0, 0, width, height)); visual.UpdateLayout();
            if (bottom) { Field<ScrollViewer>(full, "PickerScroll").ScrollToEnd(); visual.UpdateLayout(); }
            var use = Field<Button>(full, "UseColor"); var position = use.TranslatePoint(new Point(), visual);
            Assert(position.X >= 0 && position.Y >= 0 && position.X + use.ActualWidth <= width && position.Y + use.ActualHeight <= height, "Color picker confirmation buttons were clipped");
            var image = new RenderTargetBitmap(width, height, 96, 96, PixelFormats.Pbgra32); image.Render(visual);
            var encoder = new PngBitmapEncoder(); encoder.Frames.Add(BitmapFrame.Create(image));
            using var output = File.Create(Path.Combine(directory, $"picker-{width}-{height}-{(bottom ? "saved" : "top")}.png")); encoder.Save(output);
        }
        Assert(!full.IsVisible, "Picker probe unexpectedly showed a window"); full.Close();
        Console.WriteLine("PASS physical coordinate/RGB/DPI arithmetic and bounded picker layouts with 32 swatches and visible confirmation buttons");
        var overlay = new ScreenColorPicker(null, .5); var closedCount = 0;
        void Finish(ThemeColor? result, Exception? error = null) => typeof(ScreenColorPicker).GetMethod("Finish", Private)!.Invoke(overlay, [result, error]);
        overlay.Closed += (_, _) => ++closedCount;
        overlay.Closing += (_, _) => Finish(null, new Exception("Late deactivation"));
        var completed = ThemeColor.Parse("#12345680"); Finish(completed); Finish(null);
        var closeRequest = (DispatcherOperation)typeof(ScreenColorPicker).GetField("closeRequest", Private)!.GetValue(overlay)!; Wait(closeRequest.Task);
        Finish(null, new Exception("After close"));
        Assert(closedCount == 1 && overlay.Result == completed && overlay.Error == null && !overlay.IsVisible, "Late deactivation reentered close or replaced the completed result");
        Console.WriteLine("PASS screen-picker completion closes once outside callbacks and ignores deactivation during/after nonmodal close; no desktop pixel read");
    }
}
