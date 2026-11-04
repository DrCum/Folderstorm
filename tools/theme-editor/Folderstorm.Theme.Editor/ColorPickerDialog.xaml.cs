// SPDX-License-Identifier: LGPL-2.1-or-later
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.IO;

namespace Folderstorm.Theme.Editor;

public partial class ColorPickerDialog : Window
{
    private bool updating;
    private HsvColor hsv;
    private readonly ColorSwatchStore swatchStore;
    private readonly Func<Window, ThemeColor, ThemeColor?> screenPicker;
    private readonly bool allowOpacity;
    private string? selectedSaved;
    private bool savingSwatch, closed;
    private SwatchState saved = new([], true);
    public ThemeColor SelectedColor { get; private set; }
    public ColorPickerDialog(ThemeColor initial, string? swatchPath = null, Func<Window, ThemeColor, ThemeColor?>? sampler = null, bool allowOpacity = true)
    {
        this.allowOpacity = allowOpacity; ValidateColor(initial);
        swatchStore = new(swatchPath ?? ColorSwatchStore.DefaultPath); screenPicker = sampler ?? ScreenColorPicker.Pick;
        InitializeComponent(); SelectedColor = initial; hsv = HsvColor.FromColor(initial);
        Alpha.IsEnabled = allowOpacity;
        if (!allowOpacity) { Alpha.ToolTip = AlphaLabel.ToolTip = "Host colors use opaque RGB."; Hex.ToolTip = "Opaque #RRGGBB. Host controls do not use transparency."; }
        Spectrum.Changed += (saturation, value) => { hsv = hsv with { Saturation = saturation, Value = value }; SelectedColor = hsv.ToColor(); UpdateFields(); };
        foreach (var hex in new[] { "#FFFFFFFF", "#000000FF", "#BFA4FCFF", "#64D8B2FF", "#FFCC66FF", "#FF6699FF", "#00000000" })
        {
            var color = ThemeColor.Parse(hex);
            if (!allowOpacity && color.A != 1) continue;
            var swatch = new Button { Width = 36, Height = 30, Padding = new Thickness(0), Margin = new Thickness(0, 0, 7, 0), ToolTip = hex,
                Content = new Border { Width = 22, Height = 16, Background = Brush(color), BorderBrush = Brushes.Gray, BorderThickness = new Thickness(1) } };
            System.Windows.Automation.AutomationProperties.SetName(swatch, "Choose " + hex);
            swatch.Click += (_, _) => { SelectedColor = color; hsv = HsvColor.FromColor(color); UpdateFields(); };
            Swatches.Children.Add(swatch);
        }
        UpdateFields();
        SetSavedState(swatchStore.Load());
        SourceInitialized += (_, _) => ScreenPixel.FitDialog(this);
        Closed += (_, _) => closed = true;
    }
    private void ValidateColor(ThemeColor color) => Contract.Require(allowOpacity || color.A == 1, "host_palette", "Host colors must be opaque");
    private static SolidColorBrush Brush(ThemeColor color) => new(Color.FromArgb((byte)Math.Round(color.A * 255), (byte)Math.Round(color.R * 255), (byte)Math.Round(color.G * 255), (byte)Math.Round(color.B * 255)));
    private void UpdateFields()
    {
        updating = true;
        try
        {
            Hex.Text = SelectedColor.Hex(); Hue.Value = hsv.Hue; Alpha.Value = Math.Round(SelectedColor.A * 255);
            AlphaLabel.Text = Math.Round(SelectedColor.A * 100) + "%"; ChosenSwatch.Background = Brush(SelectedColor); Spectrum.SetColor(hsv);
            UseColor.IsEnabled = true; ErrorText.Visibility = Visibility.Collapsed;
            UpdateSwatchCommands();
        }
        finally { updating = false; }
    }
    private void HueChanged(object sender, RoutedPropertyChangedEventArgs<double> args)
    {
        if (updating || Spectrum == null || Hex == null) return;
        hsv = hsv with { Hue = args.NewValue }; SelectedColor = hsv.ToColor(); UpdateFields();
    }
    private void AlphaChanged(object sender, RoutedPropertyChangedEventArgs<double> args)
    {
        if (updating || Spectrum == null || Hex == null) return;
        if (!allowOpacity) { UpdateFields(); return; }
        hsv = hsv with { Alpha = args.NewValue / 255 }; SelectedColor = hsv.ToColor(); UpdateFields();
    }
    private void HexChanged(object sender, TextChangedEventArgs args)
    {
        if (updating || UseColor == null || Alpha == null) return;
        try
        {
            var color = ThemeColor.Parse(Hex.Text); ValidateColor(color); SelectedColor = color; hsv = HsvColor.FromColor(SelectedColor);
            updating = true;
            try { Hue.Value = hsv.Hue; Alpha.Value = Math.Round(hsv.Alpha * 255); AlphaLabel.Text = Math.Round(hsv.Alpha * 100) + "%"; ChosenSwatch.Background = Brush(SelectedColor); Spectrum.SetColor(hsv); }
            finally { updating = false; }
            UseColor.IsEnabled = true; ErrorText.Visibility = Visibility.Collapsed;
            UpdateSwatchCommands();
        }
        catch (ThemeException error) { selectedSaved = null; UseColor.IsEnabled = false; ErrorText.Text = error.Message; ErrorText.Visibility = Visibility.Visible; UpdateSwatchCommands(); }
    }
    private void UpdateSwatchCommands()
    {
        if (SaveSwatch == null) return;
        if (selectedSaved != SelectedColor.Hex()) selectedSaved = null;
        SaveSwatch.IsEnabled = UseColor.IsEnabled && saved.CanWrite && !savingSwatch;
        RemoveSwatch.IsEnabled = saved.CanWrite && selectedSaved != null && !savingSwatch;
        ScreenPick.IsEnabled = !savingSwatch;
        foreach (Button button in SavedColors.Children)
            if ((string?)button.ToolTip == selectedSaved) button.BorderBrush = new SolidColorBrush(Color.FromRgb(191, 164, 252));
            else button.ClearValue(BorderBrushProperty);
    }
    private void SetSavedState(SwatchState state)
    {
        saved = state; SavedColors.Children.Clear();
        foreach (var hex in state.Colors)
        {
            var color = ThemeColor.Parse(hex);
            var button = new Button { Width = 32, Height = 28, Padding = new Thickness(0), Margin = new Thickness(0, 0, 6, 6), ToolTip = hex,
                Content = new Border { Width = 22, Height = 16, Background = (Brush)FindResource("Checkerboard"), Child = new Border { Background = Brush(color) } } };
            System.Windows.Automation.AutomationProperties.SetName(button, "Saved " + hex);
            button.IsEnabled = allowOpacity || color.A == 1;
            button.Click += (_, _) => { selectedSaved = hex; SelectedColor = color; hsv = HsvColor.FromColor(color); UpdateFields(); };
            SavedColors.Children.Add(button);
        }
        SavedStatus.Text = state.Message ?? state.Colors.Length + "/32 saved colors"; UpdateSwatchCommands();
    }
    private async Task ChangeSwatch(bool remove)
    {
        if (savingSwatch) return;
        try
        {
            var color = remove ? ThemeColor.Parse(selectedSaved ?? throw new ThemeException("swatches", "Select a saved color first")) : ThemeColor.Parse(Hex.Text);
            savingSwatch = true; UpdateSwatchCommands(); SavedStatus.Text = "Saving swatches…";
            var state = await Task.Run(() => remove ? swatchStore.Remove(color) : swatchStore.Save(color));
            if (closed) return;
            selectedSaved = remove ? null : color.Hex(); SetSavedState(state);
        }
        catch (Exception error) when (error is ThemeException or IOException or UnauthorizedAccessException or ArgumentException)
        { if (!closed) SavedStatus.Text = error.Message; }
        finally { savingSwatch = false; if (!closed) UpdateSwatchCommands(); }
    }
    private async void SaveChosenSwatch(object sender, RoutedEventArgs args) => await ChangeSwatch(false);
    private async void RemoveChosenSwatch(object sender, RoutedEventArgs args) => await ChangeSwatch(true);
    private void PickScreen(object sender, RoutedEventArgs args)
    {
        if (savingSwatch) return;
        try
        {
            var result = screenPicker(this, SelectedColor); if (result == null) return;
            ValidateColor(result.Value); selectedSaved = null; SelectedColor = result.Value; hsv = HsvColor.FromColor(SelectedColor); UpdateFields();
        }
        catch (Exception error) { ErrorText.Text = error.Message; ErrorText.Visibility = Visibility.Visible; }
    }
    private void Accept(object sender, RoutedEventArgs args)
    {
        try { var color = ThemeColor.Parse(Hex.Text); ValidateColor(color); SelectedColor = color; DialogResult = true; }
        catch (ThemeException error) { UseColor.IsEnabled = false; ErrorText.Text = error.Message; ErrorText.Visibility = Visibility.Visible; }
    }
}
