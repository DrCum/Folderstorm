// SPDX-License-Identifier: LGPL-2.1-or-later
using System.Windows;
using System.Windows.Controls;
using System.Windows.Documents;
using System.Windows.Media;

namespace Folderstorm.Theme.Editor;

public sealed class PaletteToolsDialog : Window
{
    private readonly ThemeProject project;
    private readonly BaseCatalog catalog;
    private readonly string[] selected;
    private readonly ComboBox mode = new(), preset = new() { DisplayMemberPath = "Name" }, link = new() { IsEditable = true };
    private readonly TextBox value = new() { Text = "#64D8B2" };
    private readonly StackPanel choices = new(), valuePanel = new(), linkPanel = new();
    private readonly Dictionary<string, CheckBox> checks = new(StringComparer.Ordinal);
    private readonly TextBlock status = new() { TextWrapping = TextWrapping.Wrap }, changesText = new() { TextWrapping = TextWrapping.Wrap, FontFamily = new FontFamily("Consolas"), FontSize = 12 };
    private readonly Button preview = new() { Content = "Preview checked" }, apply = new() { Content = "Apply", IsEnabled = false };
    private PaletteChange[] prepared = [];
    private bool updating;
    public PaletteChange[] Changes { get; private set; } = [];
    public string EditLabel { get; private set; } = "Edit palette";

    public PaletteToolsDialog(Window? owner, ThemeProject document, BaseCatalog source, IEnumerable<string> selection)
    {
        if (owner != null) Owner = owner;
        project = document; catalog = source; selected = selection.Where(catalog.Colors.ContainsKey).Distinct(StringComparer.Ordinal).Order(StringComparer.Ordinal).ToArray();
        Title = "Palette tools"; Width = 900; Height = 660; MinWidth = 760; MinHeight = 500;
        WindowStartupLocation = owner == null ? WindowStartupLocation.CenterScreen : WindowStartupLocation.CenterOwner;
        Background = new SolidColorBrush(Color.FromRgb(17, 16, 22)); Foreground = Brushes.White;
        var root = new DockPanel { Margin = new Thickness(20) }; TextElement.SetForeground(root, Foreground); Content = root;
        var actions = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right, Margin = new Thickness(0, 14, 0, 0) };
        DockPanel.SetDock(actions, Dock.Bottom); root.Children.Add(actions);
        actions.Children.Add(new Button { Content = "Cancel", IsCancel = true }); actions.Children.Add(preview); actions.Children.Add(apply);
        var tools = new StackPanel { Width = 260, Margin = new Thickness(0, 0, 18, 0) };
        var toolsScroll = new ScrollViewer { Content = tools, Width = 290, VerticalScrollBarVisibility = ScrollBarVisibility.Auto, HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled };
        DockPanel.SetDock(toolsScroll, Dock.Left); root.Children.Add(toolsScroll);
        tools.Children.Add(new TextBlock { Text = "Palette tools", FontSize = 22 });
        tools.Children.Add(Label("Operation")); tools.Children.Add(mode);
        foreach (var text in new[] { "Preset", "Set color", "Link to color", "Reset checked" }) mode.Items.Add(text);
        preset.ItemsSource = PalettePreset.Choices; preset.SelectedIndex = 0; tools.Children.Add(preset);
        valuePanel.Children.Add(Label("Color / opacity")); var colorRow = new DockPanel(); valuePanel.Children.Add(colorRow);
        var pick = new Button { Content = "Pick", Margin = new Thickness(8, 0, 0, 0) }; DockPanel.SetDock(pick, Dock.Right); colorRow.Children.Add(pick); colorRow.Children.Add(value); tools.Children.Add(valuePanel);
        linkPanel.Children.Add(Label("Target")); link.ItemsSource = catalog.Colors.Keys.Order(StringComparer.Ordinal).ToArray(); link.Text = "ThemeAccent"; linkPanel.Children.Add(link); tools.Children.Add(linkPanel);
        tools.Children.Add(Label("Checked colors")); tools.Children.Add(choices);
        var report = new DockPanel(); root.Children.Add(report);
        var reportHeading = new TextBlock { Text = "Effective color changes", FontSize = 20, Margin = new Thickness(0, 0, 0, 12) }; DockPanel.SetDock(reportHeading, Dock.Top); report.Children.Add(reportHeading);
        status.Margin = new Thickness(0, 0, 0, 12); DockPanel.SetDock(status, Dock.Top); report.Children.Add(status);
        report.Children.Add(new ScrollViewer { Content = changesText, VerticalScrollBarVisibility = ScrollBarVisibility.Auto, HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled });
        void Invalidate() { if (updating) return; prepared = []; apply.IsEnabled = false; changesText.Text = ""; status.Text = checks.Values.Count(check => check.IsChecked == true) + " checked. Preview before applying."; }
        void Rebuild()
        {
            updating = true; checks.Clear(); choices.Children.Clear();
            var names = mode.SelectedIndex == 0 ? ((PalettePreset)preset.SelectedItem).Changes(catalog).Select(item => item.Name) : selected;
            foreach (var name in names)
            {
                var check = new CheckBox { Content = new TextBlock { Text = name, TextWrapping = TextWrapping.Wrap }, ToolTip = name, IsChecked = true, Foreground = Brushes.White, Margin = new Thickness(0, 4, 0, 4) };
                checks.Add(name, check); choices.Children.Add(check); check.Checked += (_, _) => Invalidate(); check.Unchecked += (_, _) => Invalidate();
            }
            preset.Visibility = mode.SelectedIndex == 0 ? Visibility.Visible : Visibility.Collapsed;
            valuePanel.Visibility = mode.SelectedIndex == 1 ? Visibility.Visible : Visibility.Collapsed;
            linkPanel.Visibility = mode.SelectedIndex == 2 ? Visibility.Visible : Visibility.Collapsed;
            updating = false; Invalidate();
        }
        mode.SelectionChanged += (_, _) => Rebuild(); preset.SelectionChanged += (_, _) => Rebuild();
        value.TextChanged += (_, _) => Invalidate(); link.SelectionChanged += (_, _) => Invalidate();
        link.AddHandler(TextBox.TextChangedEvent, new TextChangedEventHandler((_, _) => Invalidate()));
        pick.Click += (_, _) =>
        {
            try { var dialog = new ColorPickerDialog(ThemeColor.Parse(value.Text)) { Owner = this }; if (dialog.ShowDialog() == true) value.Text = dialog.SelectedColor.Hex(); }
            catch (ThemeException error) { status.Text = error.Message; }
        };
        preview.Click += (_, _) =>
        {
            try
            {
                var names = checks.Where(item => item.Value.IsChecked == true).Select(item => item.Key).ToArray();
                var predefined = ((PalettePreset)preset.SelectedItem).Changes(catalog).ToDictionary(item => item.Name, StringComparer.Ordinal);
                prepared = names.Select(name => mode.SelectedIndex switch
                { 0 => predefined[name], 1 => new PaletteChange(name, value.Text), 2 => new PaletteChange(name, Link: link.Text.Trim()), _ => new PaletteChange(name) }).ToArray();
                var result = PaletteProject.Preview(project, catalog, prepared);
                string Before(string name) { try { return catalog.InspectColor(name, project.Edits.Colors, project.Edits.ColorLinks).Value.Hex(); } catch (ThemeException) { return "Native value"; } }
                changesText.Text = string.Join("\n", result.EffectiveChanges.OrderBy(item => item.Key, StringComparer.Ordinal).Select(item => item.Key + "\n" + Before(item.Key) + " → " + ThemeColor.Parse(item.Value).Hex() + "\n")
                    .Concat(result.NativeOnly.Select(name => name + "\nInherited native value\n")));
                status.Text = result.DirectChanges + " direct edits · " + result.EffectiveChanges.Count + " color changes. Apply makes one Undo step.";
                apply.IsEnabled = result.DirectChanges > 0;
            }
            catch (ThemeException error) { prepared = []; apply.IsEnabled = false; status.Text = error.Message; }
        };
        apply.Click += (_, _) => { if (!apply.IsEnabled || prepared.Length == 0) return; Changes = prepared; EditLabel = mode.SelectedIndex == 0 ? "Preset " + ((PalettePreset)preset.SelectedItem).Name : mode.Text + " palette"; DialogResult = true; };
        System.Windows.Automation.AutomationProperties.SetName(mode, "Palette operation"); System.Windows.Automation.AutomationProperties.SetName(preset, "Palette preset");
        System.Windows.Automation.AutomationProperties.SetName(value, "Bulk color value"); System.Windows.Automation.AutomationProperties.SetName(link, "Bulk color link");
        mode.SelectedIndex = 0;
    }
    private static TextBlock Label(string text) => new() { Text = text, Foreground = new SolidColorBrush(Color.FromRgb(173, 169, 186)), Margin = new Thickness(0, 14, 0, 6) };
}
