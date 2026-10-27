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
    private string? appliedColorValue;
    private string[] readOnlyReasons = [];
    public event PropertyChangedEventHandler? PropertyChanged;
    public bool CanEditDocument => project != null && readOnlyReasons.Length == 0 && !closePending && !copyingProject && !imageOperation && !previewOperation && EditCanvas?.IsInteracting != true;
    public bool CanOpenDocument => !closePending && !copyingProject && !imageOperation && !previewOperation && EditCanvas?.IsInteracting != true;
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
        foreach (var property in new[] { nameof(CanEditDocument), nameof(CanOpenDocument), nameof(CanUndo), nameof(CanRedo), nameof(CanReplaceImage), nameof(CanEditImage), nameof(CanRepairImageGuides), nameof(CanReloadExternal), nameof(CanBrowseWorkingFiles) })
            PropertyChanged?.Invoke(this, new(property));
        if (ProjectLabel == null) return;
        ProjectLabel.Text = project == null ? "Classic AnsaStorm" : project.DisplayName + (changed ? "  •  Unsaved" : "") + "  ·  " + catalog?.Schemes.FirstOrDefault(s => s.Id == project.BaseScheme)?.Name;
        ProjectLabel.ToolTip = projectDirectory;
        Title = "Folderstorm theme lab · " + (typeof(MainWindow).Assembly.GetName().Version?.ToString(3) ?? "Development") + (changed ? " *" : "");
        XmlEditor.IsReadOnly = !CanEditDocument;
        if (EditCanvas != null) DrawingSettingsChanged(this, new RoutedEventArgs());
        RefreshExternalDisplay();
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
            var paletteCount = project == null ? 0 : project.Edits.Colors.Count + project.Edits.ColorLinks.Count;
            if (project != null && catalog != null && readOnlyReasons.Length == 0)
            {
                try { var palette = PaletteProject.Read(projectDirectory, project, catalog); paletteCount = palette.Literals.Count + palette.Links.Count; }
                catch (Exception error) when (error is ThemeException or System.IO.IOException) { }
            }
            ProjectSummary.Text = project == null ? "" : $"{paletteCount} palette overrides · {project.Edits.Assets.Count} assets\n{projectDirectory}";
        }
        finally { updatingEditors = false; }
        RefreshThumbnail();
        UpdateCommandState();
    }
    private void RefreshDocumentEditors()
    {
        SyncMetadata(); RefreshPalette(); RefreshImageEditors(); FontWorkspace.SetProject(catalog, project, projectDirectory); WidgetWorkspace.SetProject(catalog, project, projectDirectory);
    }
    private async void SaveAsProject(object sender, RoutedEventArgs args)
    {
        if (!Ready() || !ConfirmExternalChanges()) return;
        var dialog = new ProjectCopyDialog(this, projectDirectory); if (dialog.ShowDialog() != true) return;
        CommitMetadata(); var snapshot = Contract.Read<ThemeProject>(Contract.Bytes(project!)); var source = projectDirectory;
        copyingProject = true; UpdateCommandState(); StatusText.Text = "Saving project copy…";
        try
        {
            var destination = await Task.Run(() => ProjectCopy.SaveAs(source, dialog.Destination, snapshot));
            StopExternalSessions(); projectDirectory = destination;
            project = snapshot; changed = false; RefreshDocumentEditors(); StatusText.Text = "Editing copy: " + System.IO.Path.GetFileName(projectDirectory);
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
        PaletteState palette;
        try { palette = PaletteProject.Read(projectDirectory, project, catalog); }
        catch (Exception error) { ColorList.ItemsSource = null; StatusText.Text = error.Message; return; }
        var literals = palette.Literals; var links = palette.Links;
        Dictionary<string, string> compiled;
        try { compiled = palette.CompileChanges(); }
        catch (ThemeException error) { compiled = new(StringComparer.Ordinal); StatusText.Text = error.Message; }
        var roleNames = catalog.Colors.Keys.Where(name => name.StartsWith("Theme", StringComparison.Ordinal)).ToHashSet(StringComparer.Ordinal);
        if (roleNames.Count == 0) roleNames.UnionWith(new[] { "SL-Background", "SL-Background_Light", "TextBgWriteableColor", "TextFgColor", "LabelDisabledColor", "AccentColor", "SchemeLightest", "EmphasisColor", "ScrollSelectedBGColor", "ScrollHoveredColor", "FloaterFocusBorderColor", "HTMLLinkColor" });
        var rows = catalog.Colors.Keys.Order(StringComparer.Ordinal)
            .Where(name => name.Contains(ColorSearch.Text, StringComparison.OrdinalIgnoreCase))
            .Where(name => ColorScope.SelectedIndex switch { 0 => roleNames.Contains(name), 2 => literals.ContainsKey(name) || links.ContainsKey(name), 3 => palette.Imported.ContainsKey(name) || compiled.ContainsKey(name), _ => true })
            .Select(name =>
            {
                Brush swatch = Brushes.Transparent;
                try { swatch = ToBrush(palette.Inspect(name).Value); }
                catch (ThemeException) { }
                var authored = project?.Edits.Colors.ContainsKey(name) == true || project?.Edits.ColorLinks.ContainsKey(name) == true;
                var state = !authored && palette.Imported.ContainsKey(name) ? links.TryGetValue(name, out var rawTarget) ? "Imported link → " + rawTarget : "Imported override"
                    : links.TryGetValue(name, out var target) ? "Linked → " + target : literals.ContainsKey(name) ? "Override" : compiled.ContainsKey(name) ? "Follows an override" : "Inherited";
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
        PaletteState palette;
        try { palette = PaletteProject.Read(projectDirectory, edited ? project : null, catalog); }
        catch (Exception) { return values; }
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
            try { values[role] = palette.Inspect(name).Value; }
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
        ColorState.ToolTip = project?.Origin == "compiled-package" ? "Imported colors are explicit overrides. Reset palette restores the base; original editing links are not inferred." : null;
        try
        {
            var palette = PaletteProject.Read(projectDirectory, project, catalog); var inspected = palette.Inspect(row.Name);
            appliedColorValue = inspected.Value.NativeText;
            updatingEditors = true;
            try { ColorValue.Text = appliedColorValue; }
            finally { updatingEditors = false; }
            ColorSource.Text = inspected.Source + "\n" + string.Join(" → ", inspected.ReferenceChain);
            ColorLinkTarget.Text = palette.Links.GetValueOrDefault(row.Name) ?? catalog.Colors[row.Name].Reference ?? "";
            var followers = catalog.Colors.Keys.Where(name => name != row.Name).Where(name =>
            {
                try { return palette.Inspect(name).ReferenceChain.Contains(row.Name); }
                catch (ThemeException) { return false; }
            }).ToArray();
            ColorAffected.Text = followers.Length == 0 ? "" : $"{followers.Length} linked colors\n" + string.Join(", ", followers.Take(7)) + (followers.Length > 7 ? "…" : "");
            ShowDraftColor();
        }
        catch (Exception error)
        {
            appliedColorValue = null; ColorDraftState.Visibility = Visibility.Collapsed;
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
            ColorDraftState.Visibility = appliedColorValue != null && value.NativeText != appliedColorValue ? Visibility.Visible : Visibility.Collapsed;
            var sample = SampleColors(true); var panel = sample.GetValueOrDefault("ThemePanel", ThemeColor.Parse("#1A1921"));
            var canvas = sample.GetValueOrDefault("ThemeBg", ThemeColor.Parse("#111016")).Over(ThemeColor.Parse("#000"));
            ColorContrast.Text = "Against panel: " + ThemeColor.Contrast(value, panel, canvas).ToString("0.00", CultureInfo.InvariantCulture) + ":1";
        }
        catch (ThemeException error) { ColorFormats.Text = error.Message; ColorContrast.Text = ""; ColorDraftState.Visibility = appliedColorValue == null ? Visibility.Collapsed : Visibility.Visible; }
    }
    private void ChannelChanged(object sender, RoutedPropertyChangedEventArgs<double> args)
    {
        if (updatingEditors || ColorValue == null || RedChannel == null || GreenChannel == null || BlueChannel == null || AlphaChannel == null || ColorList?.SelectedItem == null) return;
        ColorValue.Text = ThemeColor.Create(RedChannel.Value / 255, GreenChannel.Value / 255, BlueChannel.Value / 255, AlphaChannel.Value / 255).Hex();
    }
    private async void SetColor(object sender, RoutedEventArgs args) => await Run(() =>
    {
        if (!Ready() || ColorList.SelectedItem is not ColorRow row) return;
        CommitMetadata(); if (ColorAuthoring.SetLiteral(project!, catalog!, row.Name, ColorValue.Text, projectDirectory)) changed = true;
        RefreshPalette(); StatusText.Text = "Color set.";
    });
    private async void SetColorLink(object sender, RoutedEventArgs args) => await Run(() =>
    {
        if (!Ready() || ColorList.SelectedItem is not ColorRow row) return;
        CommitMetadata(); if (ColorAuthoring.SetLink(project!, catalog!, row.Name, ColorLinkTarget.Text.Trim(), projectDirectory)) changed = true;
        RefreshPalette(); StatusText.Text = "Color linked.";
    });
    private async void ResetColor(object sender, RoutedEventArgs args) => await Run(() =>
    {
        if (!Ready() || ColorList.SelectedItem is not ColorRow row) return;
        CommitMetadata(); if (ColorAuthoring.Reset(project!, catalog!, row.Name, projectDirectory)) changed = true;
        RefreshPalette(); StatusText.Text = "Inherited color restored.";
    });
    private async void ResetPalette(object sender, RoutedEventArgs args) => await Run(() =>
    {
        if (!Ready()) return;
        CommitMetadata(); if (PaletteProject.ResetAll(projectDirectory, project!, catalog!)) changed = true;
        RefreshPalette(); StatusText.Text = "Inherited palette restored. Undo keeps your edits.";
    });
    private async void OpenPaletteTools(object sender, RoutedEventArgs args) => await Run(() =>
    {
        if (!Ready()) return;
        var selection = ColorList.SelectedItems.Cast<ColorRow>().Select(item => item.Name).ToArray();
        var dialog = new PaletteToolsDialog(this, project!, catalog!, selection, projectDirectory);
        if (dialog.ShowDialog() == true) ApplyPaletteChanges(dialog.Changes, dialog.EditLabel);
    });
    private void ApplyPaletteChanges(PaletteChange[] changes, string label)
    {
        if (!Ready()) return;
        CommitMetadata(); var count = PaletteProject.Apply(project!, catalog!, changes, label, projectDirectory);
        if (count > 0) { changed = true; RefreshPalette(); SyncMetadata(); }
        StatusText.Text = count == 0 ? "Palette already matches." : count + " palette edits applied. Undo restores the palette.";
    }
}
