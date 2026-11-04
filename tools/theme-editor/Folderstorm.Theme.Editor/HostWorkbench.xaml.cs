// SPDX-License-Identifier: LGPL-2.1-or-later
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace Folderstorm.Theme.Editor;

public sealed record HostColorRow(string Role, string Name, Brush Swatch);
public partial class HostWorkbench : UserControl
{
    private BaseCatalog? catalog;
    private ThemeProject? project;
    private string directory = "";
    private bool refreshing = true;
    private HostPaletteDefinition palette = HostPalette.Midnight;
    public Func<bool>? CanApply { get; set; }
    public int BaselineChoice => Preset.SelectedIndex == 1 ? 1 : 0;
    public event Action<HostPaletteDefinition?>? ApplyRequested;
    public HostWorkbench() { InitializeComponent(); Preset.SelectedIndex = 0; refreshing = false; }
    public void SetProject(BaseCatalog? nextCatalog, ThemeProject? nextProject, string nextDirectory)
    {
        catalog = nextCatalog; project = nextProject; directory = nextDirectory; Status.Text = "";
        RefreshPalette();
    }
    private static Brush Brush(string value)
    {
        var color = ThemeColor.Parse(value); return new SolidColorBrush(Color.FromRgb((byte)Math.Round(color.R * 255), (byte)Math.Round(color.G * 255), (byte)Math.Round(color.B * 255)));
    }
    private HostPaletteDefinition PresetPalette => Preset.SelectedIndex == 1 ? HostPalette.Daylight : HostPalette.Midnight;
    private void RefreshPalette()
    {
        var role = (Roles.SelectedItem as HostColorRow)?.Role ?? "accent"; refreshing = true;
        try
        {
            var authored = HostProject.Read(directory, project); palette = authored ?? PresetPalette;
            SampleFrame.Visibility = Visibility.Visible;
            PaletteState.Text = authored != null ? "Project override" : "Built-in theme example. No host override.";
            Roles.ItemsSource = HostPalette.Roles.Select(name => new HostColorRow(name, char.ToUpperInvariant(name[0]) + name[1..], Brush(palette.Colors[name]))).ToArray();
            Roles.SelectedItem = Roles.Items.Cast<HostColorRow>().First(row => row.Role == role);
            SampleFrame.Background = Brush(palette.Colors["background"]); SampleFrame.BorderBrush = Brush(palette.Colors["border"]);
            SamplePanel.Background = Brush(palette.Colors["panel"]); SamplePanel.BorderBrush = SampleFrame.BorderBrush;
            SampleInput.Background = Brush(palette.Colors["input"]); SampleInput.BorderBrush = SampleFrame.BorderBrush;
            SampleTitle.Foreground = SampleText.Foreground = Brush(palette.Colors["text"]);
            SampleMuted.Foreground = SamplePlaceholder.Foreground = Brush(palette.Colors["muted"]);
            SampleButton.Background = SamplePanel.Background; SampleButton.BorderBrush = SampleButtonText.Foreground = Brush(palette.Colors["accent"]);
            ShowRole();
        }
        catch (Exception error)
        {
            Roles.ItemsSource = null; RoleName.Text = "Host palette unavailable"; PaletteState.Text = "Cannot inspect this palette";
            ColorValue.Text = ""; ChosenColor.Background = Brushes.Transparent; SampleFrame.Visibility = Visibility.Collapsed; Status.Text = error.Message;
        }
        finally { refreshing = false; }
    }
    private void ShowRole()
    {
        if (Roles.SelectedItem is not HostColorRow row) return;
        RoleName.Text = row.Name; ColorValue.Text = palette.Colors[row.Role]; ChosenColor.Background = row.Swatch;
    }
    private void SelectRole(object sender, SelectionChangedEventArgs args) { if (!refreshing) { Status.Text = ""; ShowRole(); } }
    private void SelectPreset(object sender, SelectionChangedEventArgs args) { if (!refreshing) RefreshPalette(); }
    private void Run(Action action)
    {
        if (catalog == null || project == null || CanApply?.Invoke() != true) return;
        try { action(); }
        catch (Exception error) { Status.Text = error.Message; }
    }
    private void SetColor(object sender, RoutedEventArgs args) => Run(() =>
    {
        if (Roles.SelectedItem is HostColorRow row) ApplyRequested?.Invoke(HostPalette.WithColor(palette, row.Role, ColorValue.Text));
    });
    private void PickColor(object sender, RoutedEventArgs args) => Run(() =>
    {
        if (Roles.SelectedItem is not HostColorRow row) return;
        var initial = ThemeColor.Parse(palette.Colors[row.Role]);
        try { var draft = ThemeColor.Parse(ColorValue.Text); if (draft.A == 1) initial = draft; } catch (ThemeException) { }
        var picker = new ColorPickerDialog(initial, allowOpacity: false) { Owner = Window.GetWindow(this) };
        if (picker.ShowDialog() == true) ColorValue.Text = picker.SelectedColor.Hex(false);
    });
    private void UsePreset(object sender, RoutedEventArgs args) => Run(() => ApplyRequested?.Invoke(PresetPalette));
    private void FromViewer(object sender, RoutedEventArgs args) => Run(() => ApplyRequested?.Invoke(HostProject.FromViewer(directory, project, catalog!)));
    private void ResetPalette(object sender, RoutedEventArgs args) => Run(() => ApplyRequested?.Invoke(null));
}
