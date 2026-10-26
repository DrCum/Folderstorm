// SPDX-License-Identifier: LGPL-2.1-or-later
using System.ComponentModel;
using System.Globalization;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;

namespace Folderstorm.Theme.Editor;

public sealed record ColorRow(string Name, Brush Swatch, string State);

public partial class MainWindow : INotifyPropertyChanged
{
    private bool updatingEditors;
    private bool copyingProject;
    private string[] readOnlyReasons = [];
    public event PropertyChangedEventHandler? PropertyChanged;
    public bool CanEditDocument => project != null && readOnlyReasons.Length == 0 && !copyingProject;
    public bool CanOpenDocument => !copyingProject;
    public bool CanUndo => CanEditDocument && (project!.Undo.Count > 0 || MetadataPending());
    public bool CanRedo => CanEditDocument && project!.Redo.Count > 0 && !MetadataPending();

    private void InitializeAuthoring()
    {
        DataContext = this;
        ColorValue.TextChanged += (_, _) => { if (!updatingEditors) ShowDraftColor(); };
        ColorValue.KeyDown += (sender, args) => { if (args.Key == Key.Enter) { SetColor(sender, new RoutedEventArgs()); args.Handled = true; } };
        void Command(RoutedCommand command, Action execute, Func<bool> allowed)
            => CommandBindings.Add(new(command, (_, args) => { execute(); args.Handled = true; }, (_, args) => { args.CanExecute = allowed(); args.Handled = true; }));
        Command(ApplicationCommands.Save, () => SaveProject(this, new RoutedEventArgs()), () => CanEditDocument);
        Command(ApplicationCommands.SaveAs, () => SaveAsProject(this, new RoutedEventArgs()), () => CanEditDocument);
        Command(ApplicationCommands.Undo, () => Undo(this, new RoutedEventArgs()), () => CanUndo);
        Command(ApplicationCommands.Redo, () => Redo(this, new RoutedEventArgs()), () => CanRedo);
        Command(ApplicationCommands.Open, () => OpenProject(this, new RoutedEventArgs()), () => CanOpenDocument);
        InputBindings.Add(new KeyBinding(ApplicationCommands.Save, Key.S, ModifierKeys.Control));
        InputBindings.Add(new KeyBinding(ApplicationCommands.SaveAs, Key.S, ModifierKeys.Control | ModifierKeys.Shift));
        InputBindings.Add(new KeyBinding(ApplicationCommands.Undo, Key.Z, ModifierKeys.Control));
        InputBindings.Add(new KeyBinding(ApplicationCommands.Redo, Key.Y, ModifierKeys.Control));
        InputBindings.Add(new KeyBinding(ApplicationCommands.Open, Key.O, ModifierKeys.Control));
    }
    private void UpdateCommandState()
    {
        foreach (var property in new[] { nameof(CanEditDocument), nameof(CanOpenDocument), nameof(CanUndo), nameof(CanRedo) })
            PropertyChanged?.Invoke(this, new(property));
        if (ProjectLabel == null) return;
        ProjectLabel.Text = project == null ? "Classic AnsaStorm" : project.DisplayName + (changed ? "  •  Unsaved" : "") + "  ·  " + catalog?.Schemes.FirstOrDefault(s => s.Id == project.BaseScheme)?.Name;
        ProjectLabel.ToolTip = projectDirectory;
        Title = "Folderstorm theme lab · Authoring" + (changed ? " *" : "");
        XmlEditor.IsReadOnly = !CanEditDocument;
        CommandManager.InvalidateRequerySuggested();
    }
    private bool MetadataPending() => project != null && ThemeName != null && ThemeAuthor != null && PackageVersion != null && ThemeDescription != null &&
        (ThemeName.Text != project.DisplayName || ThemeAuthor.Text != project.Author || PackageVersion.Text != project.PackageVersion || ThemeDescription.Text != project.Description);
    private void MetadataChanged(object sender, TextChangedEventArgs args)
    {
        if (updatingEditors || !CanEditDocument || !MetadataPending()) return;
        changed = true; UpdateCommandState();
    }
    private void CommitMetadata()
    {
        if (!CanEditDocument || !MetadataPending()) return;
        project!.Checkpoint("Edit theme details"); project.DisplayName = ThemeName.Text; project.Author = ThemeAuthor.Text;
        project.PackageVersion = PackageVersion.Text; project.Description = ThemeDescription.Text; changed = true;
    }
    private void SyncMetadata()
    {
        updatingEditors = true;
        try
        {
            ThemeName.Text = project?.DisplayName ?? ""; ThemeAuthor.Text = project?.Author ?? "";
            PackageVersion.Text = project?.PackageVersion ?? ""; ThemeDescription.Text = project?.Description ?? "";
            ProjectSummary.Text = project == null ? "" : $"{project.Edits.Colors.Count} color overrides · {project.Edits.ColorLinks.Count} links · {project.Edits.Assets.Count} assets\n{projectDirectory}";
        }
        finally { updatingEditors = false; }
        UpdateCommandState();
    }
    private void RefreshDocumentEditors()
    {
        SyncMetadata(); RefreshPalette(); SelectTexture(this, null!);
    }
    private async void SaveAsProject(object sender, RoutedEventArgs args)
    {
        if (!Ready()) return;
        var dialog = new ProjectCopyDialog(this, projectDirectory); if (dialog.ShowDialog() != true) return;
        CommitMetadata(); var snapshot = Contract.Read<ThemeProject>(Contract.Bytes(project!)); var source = projectDirectory;
        copyingProject = true; UpdateCommandState(); StatusText.Text = "Saving project copy…";
        try
        {
            projectDirectory = await Task.Run(() => ProjectCopy.SaveAs(source, dialog.Destination, snapshot));
            project = snapshot; changed = false; RefreshDocumentEditors(); StatusText.Text = "Project copy saved.";
        }
        catch (Exception error) { StatusText.Text = error.Message; }
        finally { copyingProject = false; UpdateCommandState(); }
    }
    private void FilterColors(object sender, SelectionChangedEventArgs args) => RefreshPalette();
    private void SearchColors(object sender, TextChangedEventArgs args) => RefreshPalette();
    private void RefreshPalette()
    {
        if (catalog == null || ColorList == null || ColorScope == null || ColorSearch == null) return;
        var selection = (ColorList.SelectedItem as ColorRow)?.Name;
        var literals = project?.Edits.Colors ?? new Dictionary<string, string>();
        var links = project?.Edits.ColorLinks ?? new Dictionary<string, string>();
        Dictionary<string, string> compiled;
        try { compiled = catalog.CompileColors(literals, links); }
        catch (ThemeException error) { compiled = new(StringComparer.Ordinal); StatusText.Text = error.Message; }
        var roleNames = catalog.Colors.Keys.Where(name => name.StartsWith("Theme", StringComparison.Ordinal)).ToHashSet(StringComparer.Ordinal);
        if (roleNames.Count == 0) roleNames.UnionWith(new[] { "SL-Background", "SL-Background_Light", "TextBgWriteableColor", "TextFgColor", "LabelDisabledColor", "AccentColor", "SchemeLightest", "EmphasisColor", "ScrollSelectedBGColor", "ScrollHoveredColor", "FloaterFocusBorderColor", "HTMLLinkColor" });
        var rows = catalog.Colors.Keys.Order(StringComparer.Ordinal)
            .Where(name => name.Contains(ColorSearch.Text, StringComparison.OrdinalIgnoreCase))
            .Where(name => ColorScope.SelectedIndex switch { 0 => roleNames.Contains(name), 2 => literals.ContainsKey(name) || links.ContainsKey(name), 3 => compiled.ContainsKey(name), _ => true })
            .Select(name =>
            {
                Brush swatch = Brushes.Transparent;
                try { swatch = ToBrush(catalog.InspectColor(name, literals, links).Value); }
                catch (ThemeException) { }
                var state = links.TryGetValue(name, out var target) ? "Linked → " + target : literals.ContainsKey(name) ? "Override" : compiled.ContainsKey(name) ? "Follows an override" : "Inherited";
                return new ColorRow(name, swatch, state);
            }).ToArray();
        ColorLinkTarget.ItemsSource = catalog.Colors.Keys.Order(StringComparer.Ordinal).ToArray();
        ColorList.ItemsSource = rows;
        ColorList.SelectedItem = rows.FirstOrDefault(row => row.Name == selection) ?? rows.FirstOrDefault(row => row.Name == "ThemeAccent") ?? rows.FirstOrDefault();
        BeforeSample.SetColors(SampleColors(false)); AfterSample.SetColors(SampleColors(true));
        if (rows.Length == 0) { ColorName.Text = "No matching colors"; ColorSource.Text = ""; ColorState.Text = ""; ColorAffected.Text = ""; }
    }
    private Dictionary<string, ThemeColor> SampleColors(bool edited)
    {
        var values = new Dictionary<string, ThemeColor>(StringComparer.Ordinal);
        if (catalog == null) return values;
        var aliases = new (string Role, string Fallback)[]
        {
            ("ThemeBg", "MenuBarBgColor"), ("ThemePanel", "SL-Background"), ("ThemeLine", "FloaterUnfocusBorderColor"),
            ("ThemeText", "TextFgColor"), ("ThemeMuted", "LabelDisabledColor"), ("ThemeAccent", "AccentColor"),
            ("ThemeSelected", "ScrollSelectedBGColor"), ("ThemeTitle", "FloaterFocusBorderColor"), ("ThemeHover", "ScrollHoveredColor"), ("AnsaStorm_HtmlLink", "HTMLLinkColor"),
            ("ButtonImageColor", "ButtonImageColor"), ("ButtonLabelColor", "ButtonLabelColor")
        };
        foreach (var (role, fallback) in aliases)
        {
            var name = catalog.Colors.ContainsKey(role) ? role : fallback;
            try { values[role] = catalog.InspectColor(name, edited && project != null ? project.Edits.Colors : new Dictionary<string, string>(), edited ? project?.Edits.ColorLinks : null).Value; }
            catch (ThemeException) { }
        }
        var button = values.GetValueOrDefault("ThemeLine", ThemeColor.Parse("#302D3B"));
        var tint = values.GetValueOrDefault("ButtonImageColor", ThemeColor.Parse("#fff"));
        values["SampleButtonFill"] = ThemeColor.Create(button.R * tint.R, button.G * tint.G, button.B * tint.B, button.A * tint.A);
        return values;
    }
    private static Brush ToBrush(ThemeColor color) => new SolidColorBrush(Color.FromArgb(
        (byte)Math.Round(color.A * 255), (byte)Math.Round(color.R * 255), (byte)Math.Round(color.G * 255), (byte)Math.Round(color.B * 255)));
    private void SelectColor(object sender, SelectionChangedEventArgs args)
    {
        if (catalog == null || ColorList.SelectedItem is not ColorRow row) return;
        ColorName.Text = row.Name; ColorState.Text = row.State;
        var literals = project?.Edits.Colors ?? new Dictionary<string, string>(); var links = project?.Edits.ColorLinks ?? new Dictionary<string, string>();
        try
        {
            var inspected = catalog.InspectColor(row.Name, literals, links);
            ColorValue.Text = inspected.Value.Hex(inspected.Value.A < 1);
            ColorSource.Text = inspected.Source + "\n" + string.Join(" → ", inspected.ReferenceChain);
            ColorLinkTarget.Text = links.GetValueOrDefault(row.Name) ?? catalog.Colors[row.Name].Reference ?? "";
            var followers = catalog.Colors.Keys.Where(name => name != row.Name).Where(name =>
            {
                try { return catalog.InspectColor(name, literals, links).ReferenceChain.Contains(row.Name); }
                catch (ThemeException) { return false; }
            }).ToArray();
            ColorAffected.Text = followers.Length == 0 ? "" : $"{followers.Length} linked colors\n" + string.Join(", ", followers.Take(7)) + (followers.Length > 7 ? "…" : "");
            ShowDraftColor();
        }
        catch (ThemeException error)
        {
            ColorValue.Text = catalog.Colors[row.Name].Value ?? ""; ColorSource.Text = error.Message;
            ColorSwatch.Background = Brushes.Transparent; ColorFormats.Text = "Native inherited value"; ColorContrast.Text = ""; ColorAffected.Text = "";
        }
    }
    private void ShowDraftColor()
    {
        if (ColorValue == null || RedChannel == null || AlphaChannel == null) return;
        try
        {
            var value = ThemeColor.Parse(ColorValue.Text); updatingEditors = true;
            try { RedChannel.Value = value.R * 255; GreenChannel.Value = value.G * 255; BlueChannel.Value = value.B * 255; AlphaChannel.Value = value.A * 255; }
            finally { updatingEditors = false; }
            ColorSwatch.Background = ToBrush(value); ColorFormats.Text = value.Hex() + "\n" + value.NativeText;
            var sample = SampleColors(true); var panel = sample.GetValueOrDefault("ThemePanel", ThemeColor.Parse("#1A1921"));
            var canvas = sample.GetValueOrDefault("ThemeBg", ThemeColor.Parse("#111016")).Over(ThemeColor.Parse("#000"));
            ColorContrast.Text = "Against panel: " + ThemeColor.Contrast(value, panel, canvas).ToString("0.00", CultureInfo.InvariantCulture) + ":1";
        }
        catch (ThemeException error) { ColorFormats.Text = error.Message; ColorContrast.Text = ""; }
    }
    private void ChannelChanged(object sender, RoutedPropertyChangedEventArgs<double> args)
    {
        if (updatingEditors || ColorValue == null || RedChannel == null || GreenChannel == null || BlueChannel == null || AlphaChannel == null || ColorList?.SelectedItem == null) return;
        ColorValue.Text = ThemeColor.Create(RedChannel.Value / 255, GreenChannel.Value / 255, BlueChannel.Value / 255, AlphaChannel.Value / 255).Hex();
    }
    private async void SetColor(object sender, RoutedEventArgs args) => await Run(() =>
    {
        if (!Ready() || ColorList.SelectedItem is not ColorRow row) return;
        CommitMetadata(); if (ColorAuthoring.SetLiteral(project!, catalog!, row.Name, ColorValue.Text)) changed = true;
        RefreshPalette(); StatusText.Text = "Color set.";
    });
    private async void SetColorLink(object sender, RoutedEventArgs args) => await Run(() =>
    {
        if (!Ready() || ColorList.SelectedItem is not ColorRow row) return;
        CommitMetadata(); if (ColorAuthoring.SetLink(project!, catalog!, row.Name, ColorLinkTarget.Text.Trim())) changed = true;
        RefreshPalette(); StatusText.Text = "Color linked.";
    });
    private async void ResetColor(object sender, RoutedEventArgs args) => await Run(() =>
    {
        if (!Ready() || ColorList.SelectedItem is not ColorRow row) return;
        CommitMetadata(); if (ColorAuthoring.Reset(project!, catalog!, row.Name)) changed = true;
        RefreshPalette(); StatusText.Text = "Inherited color restored.";
    });
    private async void ResetPalette(object sender, RoutedEventArgs args) => await Run(() =>
    {
        if (!Ready() || project!.Edits.Colors.Count + project.Edits.ColorLinks.Count == 0) return;
        CommitMetadata(); project.Checkpoint("Reset palette"); project.Edits = project.Edits with { Colors = new(StringComparer.Ordinal), ColorLinks = new(StringComparer.Ordinal) };
        changed = true; RefreshPalette(); StatusText.Text = "Inherited palette restored. Undo keeps your edits.";
    });
}
