// SPDX-License-Identifier: LGPL-2.1-or-later
using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Media;

namespace Folderstorm.Theme.Editor;

public sealed class ProjectChangesDialog : Window
{
    private readonly ChangeReview review;
    private readonly TextBox search = new(), before = new(), after = new();
    private readonly ComboBox group = new();
    private readonly ListBox entries = new();
    private readonly TextBlock source = new() { TextWrapping = TextWrapping.Wrap }, status = new() { TextWrapping = TextWrapping.Wrap };
    private readonly Button reset = new() { Content = "Reset selected" }, resetGroup = new() { Content = "Reset group" }, resetAll = new() { Content = "Reset all overrides" };
    public ChangeReset? Request { get; private set; }
    public ProjectChangesDialog(Window? owner, ChangeReview review)
    {
        this.review = review; if (owner != null) Owner = owner;
        Title = "Project changes"; Width = 1120; Height = 720; MinWidth = 860; MinHeight = 520;
        WindowStartupLocation = owner == null ? WindowStartupLocation.CenterScreen : WindowStartupLocation.CenterOwner;
        Background = new SolidColorBrush(Color.FromRgb(17, 16, 22)); Foreground = Brushes.White;
        var root = new Grid { Margin = new Thickness(20) }; Content = root;
        root.RowDefinitions.Add(new() { Height = GridLength.Auto }); root.RowDefinitions.Add(new()); root.RowDefinitions.Add(new() { Height = GridLength.Auto });
        var top = new DockPanel { Margin = new Thickness(0, 0, 0, 14) }; root.Children.Add(top);
        var title = new TextBlock { Text = "Changes", FontSize = 25, Margin = new Thickness(0, 0, 20, 0) }; DockPanel.SetDock(title, Dock.Left); top.Children.Add(title);
        group.ItemsSource = new[] { "All groups" }.Concat(Enum.GetNames<ChangeGroup>()).ToArray(); group.SelectedIndex = 0; group.Width = 130;
        DockPanel.SetDock(group, Dock.Right); top.Children.Add(group); AutomationProperties.SetName(group, "Change group");
        search.Margin = new Thickness(0, 0, 10, 0); top.Children.Add(search); AutomationProperties.SetName(search, "Search project changes");
        var body = new Grid(); Grid.SetRow(body, 1); root.Children.Add(body);
        body.ColumnDefinitions.Add(new() { Width = new GridLength(280) }); body.ColumnDefinitions.Add(new() { Width = new GridLength(16) }); body.ColumnDefinitions.Add(new());
        entries.DisplayMemberPath = "Label"; ScrollViewer.SetHorizontalScrollBarVisibility(entries, ScrollBarVisibility.Auto); body.Children.Add(entries); AutomationProperties.SetName(entries, "Project overrides");
        var detail = new Grid(); Grid.SetColumn(detail, 2); body.Children.Add(detail);
        detail.RowDefinitions.Add(new() { Height = GridLength.Auto }); detail.RowDefinitions.Add(new()); detail.RowDefinitions.Add(new() { Height = GridLength.Auto });
        source.Margin = new Thickness(0, 0, 0, 10); detail.Children.Add(source);
        var values = new Grid(); Grid.SetRow(values, 1); detail.Children.Add(values);
        values.ColumnDefinitions.Add(new()); values.ColumnDefinitions.Add(new() { Width = new GridLength(12) }); values.ColumnDefinitions.Add(new());
        AddValue("Base", before, 0); AddValue("Current", after, 2);
        void AddValue(string label, TextBox field, int column)
        {
            var panel = new DockPanel(); Grid.SetColumn(panel, column); values.Children.Add(panel);
            var caption = new TextBlock { Text = label, Margin = new Thickness(0, 0, 0, 8) }; DockPanel.SetDock(caption, Dock.Top); panel.Children.Add(caption);
            field.IsReadOnly = true; field.AcceptsReturn = true; field.FontFamily = new FontFamily("Consolas"); field.FontSize = 12;
            field.TextWrapping = TextWrapping.Wrap;
            field.VerticalScrollBarVisibility = ScrollBarVisibility.Auto; field.HorizontalScrollBarVisibility = ScrollBarVisibility.Auto;
            AutomationProperties.SetName(field, label + " override values"); panel.Children.Add(field);
        }
        status.Margin = new Thickness(0, 10, 0, 0); Grid.SetRow(status, 2); detail.Children.Add(status);
        var actions = new DockPanel { Margin = new Thickness(0, 16, 0, 0) }; Grid.SetRow(actions, 2); root.Children.Add(actions);
        var close = new Button { Content = "Close", IsCancel = true, Margin = new Thickness(0) }; DockPanel.SetDock(close, Dock.Right); actions.Children.Add(close);
        var resets = new WrapPanel(); actions.Children.Add(resets); resets.Children.Add(reset); resets.Children.Add(resetGroup); resets.Children.Add(resetAll);
        foreach (var button in new[] { reset, resetGroup, resetAll }) button.ToolTip = "Restores inherited values in one Undo step. Original files and theme details stay in the project. Dependencies are validated before applying.";
        search.TextChanged += (_, _) => Filter(); group.SelectionChanged += (_, _) => Filter(); entries.SelectionChanged += (_, _) => ShowEntry();
        reset.Click += (_, _) => ChooseReset(0); resetGroup.Click += (_, _) => ChooseReset(1); resetAll.Click += (_, _) => ChooseReset(2);
        Filter();
    }
    private void Filter()
    {
        var selected = (entries.SelectedItem as ProjectChange)?.Id;
        var items = review.Entries.Where(entry => (group.SelectedIndex == 0 || entry.Group.ToString() == group.SelectedItem as string) &&
            (entry.Label.Contains(search.Text, StringComparison.OrdinalIgnoreCase) || entry.Source.Contains(search.Text, StringComparison.OrdinalIgnoreCase))).ToArray();
        entries.ItemsSource = items; entries.SelectedItem = items.FirstOrDefault(entry => entry.Id == selected) ?? items.FirstOrDefault(); ShowEntry();
    }
    private void ShowEntry()
    {
        var selected = entries.SelectedItem as ProjectChange;
        before.Text = selected?.Before ?? ""; after.Text = selected?.After ?? "";
        source.Text = selected == null ? "No matching overrides" : selected.Name + "\n" + selected.Source;
        reset.IsEnabled = resetGroup.IsEnabled = selected != null; resetAll.IsEnabled = review.Entries.Length > 0;
        resetGroup.Content = selected == null ? "Reset group" : "Reset " + selected.Group;
        status.Text = review.Entries.Length == 0 ? "Appearance matches the base." : $"{review.Entries.Length} overrides. XML is formatted for comparison; packaged bytes are unchanged.";
    }
    private void ChooseReset(int scope)
    {
        Request = PrepareReset(scope); if (Request == null) return;
        DialogResult = true;
    }
    private ChangeReset? PrepareReset(int scope)
    {
        Contract.Require(scope is >= 0 and <= 2, "changes_selection", "Choose a reset scope");
        var selected = entries.SelectedItem as ProjectChange;
        return scope != 2 && selected == null ? null : new(review.EditsHash, scope == 0 ? selected!.Id : null, scope == 1 ? selected!.Group : null);
    }
}
