// SPDX-License-Identifier: LGPL-2.1-or-later
using Microsoft.Win32;
using System.ComponentModel;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Xml.Linq;

namespace Folderstorm.Theme.Editor;

public partial class MainWindow : Window
{
    private BaseCatalog? catalog;
    private ThemeProject? project;
    private string projectDirectory = "";
    private bool changed, closing;
    private readonly PreviewSession preview = new();
    public MainWindow()
    {
        InitializeComponent(); InitializeAuthoring();
        var args = Environment.GetCommandLineArgs();
        string Argument(string key) { var index = Array.IndexOf(args, key); return index >= 0 && index + 1 < args.Length ? args[index + 1] : ""; }
        BasePath.Text = Argument("--base"); ViewerPath.Text = Argument("--viewer");

        BasePath.LostKeyboardFocus += async (_, _) =>
        {
            if (project == null && BasePath.Text.Length > 0)
                await Run(() => { catalog = new BaseCatalog(BasePath.Text); RefreshCatalog(); });
        };
        XmlFile.ItemsSource = XuiPolicy.Files.Order(StringComparer.Ordinal).Concat(["textures/textures.xml"]).ToArray(); XmlFile.SelectedIndex = 0;
        Loaded += async (_, _) =>
        {
            if (BasePath.Text.Length > 0) await Run(() => { catalog = new BaseCatalog(BasePath.Text); RefreshCatalog(); });
            var path = Argument("--project");
            if (path.Length > 0) await Run(() => Open(path));
        };
    }
    private async Task Run(Action action)
    {
        try { action(); }
        catch (Exception error) { StatusText.Text = error.Message; }
        finally { UpdateCommandState(); }
        await Task.CompletedTask;
    }
    private bool Ready()
    {
        if (CanEditDocument && catalog != null) return true;
        if (readOnlyReasons.Length > 0) { StatusText.Text = string.Join(" ", readOnlyReasons); return false; }
        StatusText.Text = "Create or open a project first."; return false;
    }
    private bool Discard()
    {
        if (!changed) return true;
        var answer = MessageBox.Show(this, "Save this project before continuing?", "Unsaved changes", MessageBoxButton.YesNoCancel);
        if (answer == MessageBoxResult.Cancel) return false;
        if (answer == MessageBoxResult.Yes) { Save(); return !changed; }
        return true;
    }
    private async void OpenProject(object sender, RoutedEventArgs args)
    {
        if (!Discard()) return;
        var picker = new OpenFolderDialog { Title = "Create or open a theme project" };
        if (picker.ShowDialog(this) != true) return;
        await Run(() =>
        {
            if (File.Exists(Path.Combine(picker.FolderName, ProjectStore.FileName))) Open(picker.FolderName);
            else
            {
                catalog = new BaseCatalog(BasePath.Text, scheme: BaseScheme.SelectedValue as string ?? "folderstorm");
                project = ProjectStore.Create(catalog, "My theme"); projectDirectory = picker.FolderName;
                readOnlyReasons = []; ReadOnlyBanner.Visibility = Visibility.Collapsed; RawProject.Visibility = Visibility.Collapsed; SyncMetadata(); Save(); RefreshCatalog(); SetupExpander.IsExpanded = false;
            }
        });
    }
    private void Open(string directory)
    {
        var document = ProjectStore.OpenDocument(directory);
        var next = document.Project;
        var nextCatalog = next == null ? catalog : new BaseCatalog(BasePath.Text, next.BaseSkin, next.BaseScheme);
        project = next; catalog = nextCatalog; projectDirectory = Path.GetFullPath(directory); readOnlyReasons = document.ReadOnlyReasons;
        changed = false; SyncMetadata(); RefreshCatalog();
        ReadOnlyBanner.Visibility = document.CanEdit ? Visibility.Collapsed : Visibility.Visible;
        ReadOnlyMessage.Text = string.Join(" ", readOnlyReasons);
        RawProject.Text = document.CanEdit ? "" : System.Text.Encoding.UTF8.GetString(document.OriginalBytes);
        RawProject.Visibility = document.CanEdit ? Visibility.Collapsed : Visibility.Visible;
        if (!document.CanEdit) { Workspace.SelectedItem = ProjectTab; StatusText.Text = "Read-only project. Original bytes are preserved."; }
        else StatusText.Text = document.WasMigrated ? "Version 1 opened. Save retains the original and writes version 2." : project!.Origin == "compiled-package" ? "Compiled overrides opened with independent history." : "Project opened.";
        SetupExpander.IsExpanded = false;
    }
    private void RefreshCatalog()
    {
        if (catalog == null) return;
        BaseScheme.ItemsSource = catalog.Schemes; BaseScheme.SelectedValue = catalog.Scheme;
        RefreshPalette();
        TextureList.ItemsSource = catalog.Textures.Values.OrderBy(t => t.Name, StringComparer.Ordinal).ToArray();
        FontList.ItemsSource = catalog.FontConfigurations; FontList.SelectedIndex = 0;
        SyncMetadata();
    }
    private void Save()
    {
        if (!Ready()) return;
        CommitMetadata(); ProjectStore.Save(projectDirectory, project!); changed = false; SyncMetadata(); StatusText.Text = "Project saved.";
    }
    private async void SaveProject(object sender, RoutedEventArgs args) => await Run(Save);
    private async void Undo(object sender, RoutedEventArgs args) => await Run(() => { if (!Ready()) return; CommitMetadata(); if (project!.UndoEdit()) { changed = true; RefreshDocumentEditors(); StatusText.Text = "Undone."; } });
    private async void Redo(object sender, RoutedEventArgs args) => await Run(() => { if (!Ready()) return; if (project!.RedoEdit()) { changed = true; RefreshDocumentEditors(); StatusText.Text = "Redone."; } });
    private void SelectTexture(object sender, SelectionChangedEventArgs args)
    {
        if (catalog == null || TextureList.SelectedItem is not TextureDefinition texture) return;
        TextureName.Text = texture.Name; TextureSource.Text = texture.Source ?? "Requires native resolution"; AssetImage.Source = null;
        var edited = project?.Edits.Assets.LastOrDefault(a => a.PayloadPath == "viewer/textures/" + texture.Attributes.GetValueOrDefault("file_name"));
        var path = edited == null ? texture.Source == null ? null : Path.Combine(catalog.Root, texture.Source) : Path.Combine(projectDirectory, edited.AssetPath);
        if (path == null) return;
        try { var bitmap = new BitmapImage(); bitmap.BeginInit(); bitmap.CacheOption = BitmapCacheOption.OnLoad; bitmap.UriSource = new Uri(Path.GetFullPath(path)); bitmap.EndInit(); bitmap.Freeze(); AssetImage.Source = bitmap; }
        catch (Exception error) when (error is IOException or NotSupportedException or FormatException) { TextureSource.Text += "\nUse native inspection for this image format."; }
    }
    private async void ReplaceImage(object sender, RoutedEventArgs args)
    {
        if (!Ready() || TextureList.SelectedItem is not TextureDefinition texture) return;
        var picker = new OpenFileDialog { Filter = "PNG image|*.png" }; if (picker.ShowDialog(this) != true) return;
        await Run(() => { var payload = "viewer/textures/" + texture.Attributes["file_name"]; Contract.Require(payload.EndsWith(".png", StringComparison.OrdinalIgnoreCase), "image", "Choose a PNG-backed registration for this initial replacement tool");
            project!.Checkpoint("Replace " + texture.Name); var asset = ProjectStore.ImportAsset(projectDirectory, picker.FileName, payload, "image");
            project.Edits.Assets.RemoveAll(a => a.PayloadPath == payload); project.Edits.Assets.Add(asset); changed = true; SelectTexture(sender, null!); });
    }
    private void PutAsset(string payload, string kind, byte[] bytes, byte[]? original = null)
    {
        var extension = Path.GetExtension(payload); var relative = "assets/" + Contract.Hash(bytes) + extension;
        var originalRelative = "assets/" + Contract.Hash(original ?? bytes) + extension;
        Contract.AtomicWrite(Contract.Contained(projectDirectory, relative), bytes); Contract.AtomicWrite(Contract.Contained(projectDirectory, originalRelative), original ?? bytes);
        project!.Edits.Assets.RemoveAll(a => a.PayloadPath == payload); project.Edits.Assets.Add(new(payload, relative, originalRelative, kind)); changed = true;
    }
    private async void UseFont(object sender, RoutedEventArgs args) => await Run(() =>
    {
        if (!Ready() || FontList.SelectedItem is not string font) return;
        project!.Checkpoint("Use font configuration"); PutAsset("viewer/fonts/fonts.xml", "font_config", File.ReadAllBytes(Path.Combine(catalog!.Root, "fonts", font)));
        FontStatus.Text = font + " selected. Native preview restarts to load it.";
    });
    private async void EmbedFont(object sender, RoutedEventArgs args)
    {
        if (!Ready()) return;
        var picker = new OpenFileDialog { Filter = "Font file|*.ttf;*.otf;*.ttc" }; if (picker.ShowDialog(this) != true) return;
        await Run(() => { project!.Checkpoint("Embed font"); var name = Path.GetFileName(picker.FileName); PutAsset("viewer/fonts/" + name, "font", File.ReadAllBytes(picker.FileName));
            var config = Contract.Xml(File.ReadAllBytes(Path.Combine(catalog!.Root, "fonts", FontList.SelectedItem as string ?? "fonts.xml")));
            foreach (var font in config.Root!.Elements("font").Where(f => (string?)f.Attribute("name") == "SansSerif" || (string?)f.Attribute("name") == "default"))
            { var first = font.Element("file"); if (first != null) { first.Value = name; first.Attributes().Remove(); } }
            PutAsset("viewer/fonts/fonts.xml", "font_config", Contract.XmlBytes(config)); FontStatus.Text = name + " embedded. Include its redistribution license before sharing."; });
    }
    private async void ResetFont(object sender, RoutedEventArgs args) => await Run(() => { if (!Ready()) return; project!.Checkpoint("Reset font"); project.Edits.Assets.RemoveAll(a => a.PayloadPath.StartsWith("viewer/fonts/", StringComparison.Ordinal)); changed = true; FontStatus.Text = "Inherited font configuration restored."; });
    private async void LoadXml(object sender, RoutedEventArgs args) => await Run(() =>
    {
        if (catalog == null || XmlFile.SelectedItem is not string file) return;
        var asset = project?.Edits.Assets.LastOrDefault(a => a.PayloadPath == "viewer/" + file);
        XmlEditor.Text = System.Text.Encoding.UTF8.GetString(asset == null ? catalog.ResolveBytes(file) : File.ReadAllBytes(Contract.Contained(projectDirectory, asset.AssetPath)));
    });
    private async void SetXml(object sender, RoutedEventArgs args) => await Run(() =>
    {
        if (!Ready() || XmlFile.SelectedItem is not string file) return;
        var bytes = System.Text.Encoding.UTF8.GetBytes(XmlEditor.Text); Contract.Xml(bytes);
        if (file.StartsWith("xui/", StringComparison.Ordinal)) XuiPolicy.Validate(file, bytes, catalog!);
        var before = Contract.Bytes(project!); var wasChanged = changed;
        try
        {
            project!.Checkpoint("Edit " + file); PutAsset("viewer/" + file, PackageStore.Kind("viewer/" + file), bytes, catalog!.ResolveBytes(file));
            PackageStore.Compile(projectDirectory, project, catalog); StatusText.Text = "Override validated.";
        }
        catch { project = Contract.Read<ThemeProject>(before); changed = wasChanged; throw; }
    });
    private async void Export(object sender, RoutedEventArgs args)
    {
        if (!Ready()) return;
        var picker = new SaveFileDialog { Filter = "Folderstorm theme|*.fstheme", FileName = "My theme.fstheme" }; if (picker.ShowDialog(this) != true) return;
        await Run(() => { CommitMetadata(); PackageStore.Export(picker.FileName, PackageStore.Compile(projectDirectory, project!, catalog!)); StatusText.Text = "Theme exported. Save the project to retain its editing history."; });
    }
    private async void Preview(object sender, RoutedEventArgs args)
    {
        if (!Ready()) return;
        try { StatusText.Text = "Validating and starting native preview…"; Workspace.IsEnabled = false;
            CommitMetadata(); var theme = PackageStore.Compile(projectDirectory, project!, catalog!);
            await preview.StartAsync(ViewerPath.Text, theme, catalog!); StatusText.Text = "Native preview " + preview.Status + " · PID " + preview.ProcessId; }
        catch (Exception error) { StatusText.Text = error.Message; }
        finally { Workspace.IsEnabled = true; }
    }
    private async void ClosePreview(object sender, RoutedEventArgs args) { await preview.CloseAsync(); StatusText.Text = "Preview closed."; }
    private async void OnClosing(object? sender, CancelEventArgs args)
    {
        if (closing) return; args.Cancel = true;
        try { if (!Discard()) return; await preview.DisposeAsync(); closing = true; Close(); }
        catch (Exception error) { StatusText.Text = error.Message; }
    }
}
