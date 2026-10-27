// SPDX-License-Identifier: LGPL-2.1-or-later
using System.Windows;
using System.Windows.Controls;

namespace Folderstorm.Theme.Editor;

public sealed record WidgetPropertyRow(XuiPropertyInfo Property)
{ public string Label => (Property.Changed ? "● " : "") + Property.Name + (Property.Editable ? "" : " · protected") + "\n" + (Property.Value ?? "(not set)"); }

public partial class WidgetWorkbench : UserControl
{
    private BaseCatalog? catalog;
    private ThemeProject? project;
    private string directory = "";
    private XuiInspection? inspection;
    private XuiPropertyInfo? property;
    private bool refreshing = true;
    private bool editingValue;
    public event Action? Edited;
    private string? CurrentPath => FileChoice.SelectedItem as string;
    private XuiElementInfo? CurrentControl => Controls.SelectedItem as XuiElementInfo;
    public WidgetWorkbench()
    {
        InitializeComponent(); FileChoice.ItemsSource = XuiPolicy.Files.Order(StringComparer.Ordinal).ToArray(); FileChoice.SelectedIndex = 0;
        PreviewState.ItemsSource = WidgetSampleSource.States; PreviewState.SelectedIndex = 0; refreshing = false;
        IsVisibleChanged += (_, _) => { if (IsVisible) RefreshSamples(); };
    }
    public void SetProject(BaseCatalog? nextCatalog, ThemeProject? nextProject, string nextDirectory)
    {
        catalog = nextCatalog; project = nextProject; directory = nextDirectory; Status.Text = ""; LoadControls();
    }
    private void SelectFile(object sender, SelectionChangedEventArgs args) { if (!refreshing) LoadControls(); }
    private void SearchControls(object sender, TextChangedEventArgs args) { if (!refreshing) FillControls(); }
    private void LoadControls()
    {
        if (catalog == null || CurrentPath == null) return;
        try
        {
            inspection = XuiProject.Inspect(directory, project, catalog, CurrentPath); FillControls();
            Provenance.Text = (inspection.IsOverride ? "Project override · " : "Inherited · ") + inspection.BaseSource + "\n" + inspection.Sha256;
        }
        catch (Exception error) { inspection = null; Controls.ItemsSource = null; ClearSelection(); Status.Text = error.Message; }
    }
    private void FillControls()
    {
        if (Controls == null || inspection == null) return;
        var previous = CurrentControl?.Index; var entries = inspection.Elements.Where(entry => (entry.Type + " " + entry.Name).Contains(ControlSearch.Text, StringComparison.OrdinalIgnoreCase)).ToArray();
        refreshing = true;
        try { Controls.ItemsSource = entries; Controls.SelectedItem = entries.FirstOrDefault(entry => entry.Index == previous) ?? entries.FirstOrDefault(); }
        finally { refreshing = false; }
        ShowControl();
    }
    private void SelectControl(object sender, SelectionChangedEventArgs args) { if (!refreshing) ShowControl(); }
    private void ClearSelection()
    {
        ControlTitle.Text = "No matching controls"; Properties.ItemsSource = null; ExtraProperty.ItemsSource = null; property = null;
        PropertyName.Text = PropertyState.Text = InheritedValue.Text = SampleDetail.Text = ""; PropertyValue.Text = "";
        ApplyPropertyButton.IsEnabled = ResetPropertyButton.IsEnabled = PickColorButton.IsEnabled = false; BeforeSample.SetSample(null, new(1, 1)); AfterSample.SetSample(null, new(1, 1));
    }
    private XuiPropertyInfo[] CurrentProperties() => catalog == null || CurrentPath == null || CurrentControl == null ? [] : XuiProject.Properties(directory, project, catalog, CurrentPath, CurrentControl.Index);
    private void ShowControl()
    {
        if (catalog == null || CurrentPath == null || CurrentControl == null) { ClearSelection(); return; }
        try
        {
            ControlTitle.Text = CurrentControl.Name.Length > 0 ? CurrentControl.Name : CurrentControl.Type + " defaults";
            var previous = property?.Name; var all = CurrentProperties();
            var rows = all.Where(item => PropertyFilter.SelectedIndex switch { 0 => item.Editable, 1 => item.Changed, _ => true }).Select(item => new WidgetPropertyRow(item)).ToArray();
            refreshing = true;
            try
            {
                Properties.ItemsSource = rows; Properties.SelectedItem = rows.FirstOrDefault(row => row.Property.Name == previous) ?? rows.FirstOrDefault(row => row.Property.Name == "label") ?? rows.FirstOrDefault();
                ExtraProperty.ItemsSource = AdditionalProperties(CurrentControl.Type, all); ExtraProperty.SelectedIndex = -1;
            }
            finally { refreshing = false; }
            ShowProperty((Properties.SelectedItem as WidgetPropertyRow)?.Property); RefreshSamples();
        }
        catch (Exception error) { ClearSelection(); Status.Text = error.Message; }
    }
    private static string[] AdditionalProperties(string type, XuiPropertyInfo[] current)
        => XuiPolicy.EditableAttributesFor(type).Where(name => !current.Any(item => item.Name == name)).ToArray();
    private void FilterProperties(object sender, SelectionChangedEventArgs args) { if (Properties != null && !refreshing) ShowControl(); }
    private void SelectProperty(object sender, SelectionChangedEventArgs args) { if (!refreshing) ShowProperty((Properties.SelectedItem as WidgetPropertyRow)?.Property); }
    private void SelectExtraProperty(object sender, SelectionChangedEventArgs args)
    {
        if (refreshing || ExtraProperty.SelectedItem is not string name) return;
        refreshing = true; Properties.SelectedItem = null; refreshing = false;
        ShowProperty(new(name, XuiPolicy.AttributeKind(name), null, null, true));
    }
    private void ShowProperty(XuiPropertyInfo? selected)
    {
        property = selected; editingValue = true;
        try
        {
            PropertyName.Text = selected?.Name ?? "Choose a property"; PropertyState.Text = selected == null ? "" : !selected.Editable ? "Protected binding or structure" : selected.Changed ? "Override" : "Inherited";
            PropertyValue.Text = selected?.Value ?? ""; PropertyValue.IsReadOnly = selected?.Editable != true;
            InheritedValue.Text = selected == null ? "" : "Base value: " + (selected.BaseValue ?? "(not set)");
            PickRow.Visibility = selected?.Kind == "color" ? Visibility.Visible : Visibility.Collapsed;
            IEnumerable<string> choices = selected?.Kind switch { "color" => catalog?.Colors.Keys.Order(StringComparer.Ordinal).ToArray() ?? [], "image" => catalog?.Textures.Keys.Order(StringComparer.Ordinal).ToArray() ?? [], _ => selected == null ? [] : XuiPolicy.ValueChoices(selected.Name) };
            ValueChoice.ItemsSource = choices.ToArray(); ValueChoice.SelectedIndex = -1; ValueChoice.Visibility = ValueChoice.Items.Count > 0 ? Visibility.Visible : Visibility.Collapsed;
            ValueChoiceLabel.Visibility = ValueChoice.Visibility; ValueChoiceLabel.Text = selected?.Kind == "color" ? "Named color" : selected?.Kind == "image" ? "Image ID" : "Value";
            ApplyPropertyButton.IsEnabled = ResetPropertyButton.IsEnabled = PickColorButton.IsEnabled = selected?.Editable == true;
        }
        finally { editingValue = false; }
    }
    private void DraftChanged(object sender, TextChangedEventArgs args) { if (!editingValue && PropertyState != null && property?.Editable == true) PropertyState.Text = "Draft · Set property to apply"; }
    private void ChooseValue(object sender, SelectionChangedEventArgs args) { if (!editingValue && ValueChoice.SelectedItem is string value) PropertyValue.Text = value; }
    private void PickColor(object sender, RoutedEventArgs args)
    {
        if (property?.Kind != "color" || catalog == null) return;
        try
        {
            var value = PropertyValue.Text; var initial = catalog.Colors.ContainsKey(value) ? catalog.InspectColor(value, project?.Edits.Colors ?? new Dictionary<string, string>(), project?.Edits.ColorLinks).Value : value.Length == 0 ? ThemeColor.Parse("#FFFFFFFF") : ThemeColor.Parse(value);
            var picker = new ColorPickerDialog(initial) { Owner = Window.GetWindow(this) }; if (picker.ShowDialog() == true) PropertyValue.Text = picker.SelectedColor.Hex();
        }
        catch (Exception error) { Status.Text = error.Message; }
    }
    private void StateChanged(object sender, SelectionChangedEventArgs args) { if (!refreshing) RefreshSamples(); }
    private void RefreshSamples()
    {
        if (catalog == null || CurrentPath == null || CurrentControl == null || PreviewState.SelectedItem is not string state) return;
        try
        {
            var before = WidgetSampleSource.Read(directory, null, catalog, CurrentPath, CurrentControl.Index, state);
            var after = WidgetSampleSource.Read(directory, project, catalog, CurrentPath, CurrentControl.Index, state);
            var extent = new Size(Math.Max(before.Width, after.Width), Math.Max(before.Height, after.Height)); BeforeSample.SetSample(before, extent); AfterSample.SetSample(after, extent);
            SampleDetail.Text = after.Detail + "\nWPF text uses Segoe UI/Consolas. Check native fonts, glow and behavior in the viewer.";
        }
        catch (Exception error) { BeforeSample.SetSample(null, new(1, 1)); AfterSample.SetSample(null, new(1, 1)); SampleDetail.Text = error.Message; }
    }
    private void Change(Func<bool> edit, string message)
    {
        try
        {
            if (project == null || catalog == null || DataContext is MainWindow { CanEditDocument: false }) return;
            if (!edit()) { Status.Text = "Already matches."; return; }
            Edited?.Invoke(); SetProject(catalog, project, directory); Status.Text = message;
        }
        catch (Exception error) { Status.Text = error.Message; }
    }
    private void ApplyProperty(object sender, RoutedEventArgs args) => Change(() => property?.Editable == true && CurrentControl != null && CurrentPath != null && XuiProject.Apply(directory, project!, catalog!, CurrentPath, [new(CurrentControl.Index, property.Name, PropertyValue.Text)], "Set " + property.Name), "Property applied.");
    private void ResetProperty(object sender, RoutedEventArgs args) => Change(() => property?.Editable == true && CurrentControl != null && CurrentPath != null && XuiProject.Apply(directory, project!, catalog!, CurrentPath, [new(CurrentControl.Index, property.Name, null)], "Reset " + property.Name), "Base property restored.");
    private void ResetFile(object sender, RoutedEventArgs args) => Change(() => CurrentPath != null && XuiProject.Reset(directory, project!, catalog!, CurrentPath), "Base file restored.");
}
