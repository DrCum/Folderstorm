// SPDX-License-Identifier: LGPL-2.1-or-later
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace Folderstorm.Theme.Editor;

public partial class ColorPickerDialog : Window
{
    private bool updating;
    private HsvColor hsv;
    public ThemeColor SelectedColor { get; private set; }
    public ColorPickerDialog(ThemeColor initial)
    {
        InitializeComponent(); SelectedColor = initial; hsv = HsvColor.FromColor(initial);
        Spectrum.Changed += (saturation, value) => { hsv = hsv with { Saturation = saturation, Value = value }; SelectedColor = hsv.ToColor(); UpdateFields(); };
        foreach (var hex in new[] { "#FFFFFFFF", "#000000FF", "#BFA4FCFF", "#64D8B2FF", "#FFCC66FF", "#FF6699FF", "#00000000" })
        {
            var color = ThemeColor.Parse(hex);
            var swatch = new Button { Width = 36, Height = 30, Padding = new Thickness(0), Margin = new Thickness(0, 0, 7, 0), ToolTip = hex,
                Content = new Border { Width = 22, Height = 16, Background = Brush(color), BorderBrush = Brushes.Gray, BorderThickness = new Thickness(1) } };
            System.Windows.Automation.AutomationProperties.SetName(swatch, "Choose " + hex);
            swatch.Click += (_, _) => { SelectedColor = color; hsv = HsvColor.FromColor(color); UpdateFields(); };
            Swatches.Children.Add(swatch);
        }
        UpdateFields();
    }
    private static SolidColorBrush Brush(ThemeColor color) => new(Color.FromArgb((byte)Math.Round(color.A * 255), (byte)Math.Round(color.R * 255), (byte)Math.Round(color.G * 255), (byte)Math.Round(color.B * 255)));
    private void UpdateFields()
    {
        updating = true;
        try
        {
            Hex.Text = SelectedColor.Hex(); Hue.Value = hsv.Hue; Alpha.Value = Math.Round(SelectedColor.A * 255);
            AlphaLabel.Text = Math.Round(SelectedColor.A * 100) + "%"; ChosenSwatch.Background = Brush(SelectedColor); Spectrum.SetColor(hsv);
            UseColor.IsEnabled = true; ErrorText.Visibility = Visibility.Collapsed;
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
        hsv = hsv with { Alpha = args.NewValue / 255 }; SelectedColor = hsv.ToColor(); UpdateFields();
    }
    private void HexChanged(object sender, TextChangedEventArgs args)
    {
        if (updating || UseColor == null || Alpha == null) return;
        try
        {
            SelectedColor = ThemeColor.Parse(Hex.Text); hsv = HsvColor.FromColor(SelectedColor);
            updating = true;
            try { Hue.Value = hsv.Hue; Alpha.Value = Math.Round(hsv.Alpha * 255); AlphaLabel.Text = Math.Round(hsv.Alpha * 100) + "%"; ChosenSwatch.Background = Brush(SelectedColor); Spectrum.SetColor(hsv); }
            finally { updating = false; }
            UseColor.IsEnabled = true; ErrorText.Visibility = Visibility.Collapsed;
        }
        catch (ThemeException error) { UseColor.IsEnabled = false; ErrorText.Text = error.Message; ErrorText.Visibility = Visibility.Visible; }
    }
    private void Accept(object sender, RoutedEventArgs args)
    {
        try { SelectedColor = ThemeColor.Parse(Hex.Text); DialogResult = true; }
        catch (ThemeException error) { UseColor.IsEnabled = false; ErrorText.Text = error.Message; ErrorText.Visibility = Visibility.Visible; }
    }
}
