// SPDX-License-Identifier: LGPL-2.1-or-later
using Microsoft.Win32;
using System.IO;
using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Documents;
using System.Windows.Data;
using System.Windows.Media;

namespace Folderstorm.Theme.Editor;

public sealed class LibraryDialog : Window
{
    private sealed record Row(string Name, string Detail, LibraryEntry? Entry, string? Archive)
    {
        public string ShortDetail => Entry == null ? "Package archive" : Detail[..12];
        public override string ToString() => Name + " · " + ShortDetail;
    }
    private readonly string baseRoot;
    private readonly Func<ValidatedTheme, BaseCatalog, Task> preview;
    private readonly ListBox entries = new();
    private readonly TextBox search = new();
    private readonly TextBlock heading = new() { FontSize = 22, TextWrapping = TextWrapping.Wrap }, details = new() { TextWrapping = TextWrapping.Wrap }, status = new() { TextWrapping = TextWrapping.Wrap };
    private readonly Image thumbnail = new() { Stretch = Stretch.Uniform, MaxHeight = 250 };
    private readonly Border imageFrame = new() { Padding = new Thickness(8), Margin = new Thickness(0, 0, 0, 14), Visibility = Visibility.Collapsed };
    private readonly TextBlock previewState = new() { Text = "No preview image", Foreground = new SolidColorBrush(Color.FromRgb(173, 169, 186)), Margin = new Thickness(0, 0, 0, 14) };
    private readonly ListBox payloads = new() { FontSize = 12, MaxHeight = 220 };
    private readonly Button previewButton = new() { Content = "Preview in viewer", IsEnabled = false }, openButton = new() { Content = "Open as project…", IsEnabled = false };
    private readonly DockPanel root = new() { Margin = new Thickness(20) };
    private Row[] rows = [];
    private int selection;
    private bool busy;
    private readonly SemaphoreSlim validation = new(1, 1);
    private string libraryRoot;
    private readonly string projectParent;
    public ValidatedTheme? SelectedTheme { get; private set; }
    public string Destination { get; private set; } = "";

    public LibraryDialog(Window? owner, string viewerRoot, Func<ValidatedTheme, BaseCatalog, Task> nativePreview, string? library = null, string? parent = null)
    {
        if (owner != null) Owner = owner;
        baseRoot = viewerRoot; preview = nativePreview; libraryRoot = library ?? ThemeLibrary.DefaultRoot; projectParent = parent ?? Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments);
        Title = "Theme library"; Width = 1020; Height = 760; MinWidth = 820; MinHeight = 600;
        WindowStartupLocation = owner == null ? WindowStartupLocation.CenterScreen : WindowStartupLocation.CenterOwner;
        Background = new SolidColorBrush(Color.FromRgb(17, 16, 22)); Foreground = Brushes.White; Content = root; TextElement.SetForeground(root, Foreground);
        var actions = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right, Margin = new Thickness(0, 14, 0, 0) };
        DockPanel.SetDock(actions, Dock.Bottom); root.Children.Add(actions);
        actions.Children.Add(new Button { Content = "Close", IsCancel = true }); actions.Children.Add(previewButton); actions.Children.Add(openButton);
        status.Margin = new Thickness(0, 12, 0, 0); DockPanel.SetDock(status, Dock.Bottom); root.Children.Add(status);
        var toolbar = new DockPanel { Margin = new Thickness(0, 0, 0, 16) }; DockPanel.SetDock(toolbar, Dock.Top); root.Children.Add(toolbar);
        var refresh = new Button { Content = "Refresh" }; var folder = new Button { Content = "Library folder…" }; var package = new Button { Content = "Open .fstheme…", Margin = new Thickness(0) };
        var tools = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right }; DockPanel.SetDock(tools, Dock.Right); toolbar.Children.Add(tools);
        tools.Children.Add(refresh); tools.Children.Add(folder); tools.Children.Add(package); toolbar.Children.Add(new TextBlock { Text = "Theme library", FontSize = 24, VerticalAlignment = VerticalAlignment.Center });
        var columns = new Grid(); columns.ColumnDefinitions.Add(new() { Width = new GridLength(310) }); columns.ColumnDefinitions.Add(new()); root.Children.Add(columns);
        var left = new DockPanel { Margin = new Thickness(0, 0, 18, 0) }; columns.Children.Add(left);
        search.Margin = new Thickness(0, 0, 0, 10); AutomationProperties.SetName(search, "Search theme library"); DockPanel.SetDock(search, Dock.Top); left.Children.Add(search); left.Children.Add(entries);
        var content = new StackPanel { Margin = new Thickness(0, 0, 16, 0) }; var scroll = new ScrollViewer { Content = content, VerticalScrollBarVisibility = ScrollBarVisibility.Auto, HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled }; Grid.SetColumn(scroll, 1); columns.Children.Add(scroll);
        content.Children.Add(heading); details.Margin = new Thickness(0, 8, 0, 12); content.Children.Add(details);
        imageFrame.Background = (Brush)FindResource("Checkerboard"); imageFrame.Child = thumbnail; content.Children.Add(imageFrame); content.Children.Add(previewState);
        content.Children.Add(new TextBlock { Text = "Package files", Margin = new Thickness(0, 0, 0, 7) }); content.Children.Add(payloads);
        var rowTemplate = new FrameworkElementFactory(typeof(StackPanel));
        var nameText = new FrameworkElementFactory(typeof(TextBlock)); nameText.SetBinding(TextBlock.TextProperty, new Binding("Name")); nameText.SetValue(TextBlock.TextTrimmingProperty, TextTrimming.CharacterEllipsis); rowTemplate.AppendChild(nameText);
        var revisionText = new FrameworkElementFactory(typeof(TextBlock)); revisionText.SetBinding(TextBlock.TextProperty, new Binding("ShortDetail")); revisionText.SetValue(TextBlock.ForegroundProperty, previewState.Foreground); revisionText.SetValue(TextBlock.FontSizeProperty, 12d); revisionText.SetValue(FrameworkElement.MarginProperty, new Thickness(0, 3, 0, 0)); rowTemplate.AppendChild(revisionText);
        entries.ItemTemplate = new DataTemplate { VisualTree = rowTemplate }; entries.SelectionChanged += async (_, _) => await SelectAsync();
        search.TextChanged += (_, _) => Filter(); refresh.Click += (_, _) => RefreshLibrary();
        folder.Click += (_, _) => { var picker = new OpenFolderDialog { Title = "Choose a theme library", InitialDirectory = Directory.Exists(libraryRoot) ? libraryRoot : projectParent }; if (picker.ShowDialog(this) == true) { libraryRoot = picker.FolderName; RefreshLibrary(); } };
        package.Click += (_, _) =>
        {
            var picker = new OpenFileDialog { Filter = "Folderstorm theme|*.fstheme" }; if (picker.ShowDialog(this) != true) return;
            var row = new Row(Path.GetFileNameWithoutExtension(picker.FileName), "Package archive", null, picker.FileName);
            rows = rows.Append(row).ToArray(); search.Text = ""; Filter(); entries.SelectedItem = row;
        };
        previewButton.Click += async (_, _) => await UseSelectionAsync(false);
        openButton.Click += async (_, _) => await UseSelectionAsync(true);
        Closing += (_, args) => { if (busy) { args.Cancel = true; status.Text = "Wait for the preview to finish starting."; } else ++selection; };
        RefreshLibrary();
    }
    private void RefreshLibrary()
    {
        ++selection; var scan = LibraryCatalog.Scan(libraryRoot);
        rows = scan.Entries.Select(entry => new Row(entry.DisplayName + " · " + (entry.Header?.PackageVersion ?? "Unavailable"), entry.Revision, entry, null)).ToArray();
        Filter(); status.Text = rows.Length == 0 ? "No themes here. Open a .fstheme package to inspect it." : rows.Length + " revisions. Select one to validate it.";
        if (scan.Truncated) status.Text += " Listing limited to 512 folders; choose a smaller library folder.";
        if (scan.Warnings.Length > 0) status.Text += " " + string.Join(" ", scan.Warnings.Take(2));
        status.ToolTip = libraryRoot;
    }
    private void Filter()
    {
        var previous = entries.SelectedItem as Row;
        entries.ItemsSource = rows.Where(row => row.Name.Contains(search.Text, StringComparison.OrdinalIgnoreCase) || row.Detail.Contains(search.Text, StringComparison.OrdinalIgnoreCase)).ToArray();
        entries.SelectedItem = entries.Items.Cast<Row>().FirstOrDefault(row => row == previous) ?? entries.Items.Cast<Row>().FirstOrDefault();
    }
    private (ValidatedTheme Theme, BaseCatalog Catalog) Validate(Row row)
    {
        if (row.Entry is { } entry)
        {
            Contract.Require(entry.Error == null && entry.Header != null, "library_entry", entry.Error ?? "Unsupported library entry");
            var catalog = new BaseCatalog(baseRoot, entry.Header.BaseSkin, entry.Header.BaseScheme);
            return (LibraryCatalog.Validate(entry, catalog), catalog);
        }
        var package = PackageStore.ReadArchive(row.Archive!); var source = new BaseCatalog(baseRoot, package.Manifest.BaseSkin, package.Manifest.BaseScheme);
        return (PackageStore.Validate(package.Files, source), source);
    }
    private async Task SelectAsync()
    {
        var ticket = ++selection; previewButton.IsEnabled = openButton.IsEnabled = false; thumbnail.Source = null; imageFrame.Visibility = Visibility.Collapsed; previewState.Visibility = Visibility.Visible; previewState.Text = "No preview image"; payloads.ItemsSource = null;
        if (entries.SelectedItem is not Row row) { heading.Text = "Choose a theme"; details.Text = ""; return; }
        heading.Text = row.Name; details.Text = row.Entry?.Error ?? "Validating package…";
        try
        {
            var (theme, _) = await ValidateAsync(row, ticket); if (ticket != selection) return;
            ShowTheme(theme); previewButton.IsEnabled = openButton.IsEnabled = true; status.Text = "Validated. Open as project starts with independent editing history.";
        }
        catch (Exception error) { if (ticket == selection) { details.Text = error.Message; status.Text = "This package cannot be opened against the chosen base."; } }
    }
    private async Task<(ValidatedTheme Theme, BaseCatalog Catalog)> ValidateAsync(Row row, int ticket)
    {
        await validation.WaitAsync();
        try
        {
            if (ticket != selection) throw new OperationCanceledException("Selection changed");
            return await Task.Run(() => Validate(row));
        }
        finally { validation.Release(); }
    }
    private void ShowTheme(ValidatedTheme theme)
    {
        var header = theme.Manifest;
        heading.Text = header.DisplayName; details.Text = string.Join("\n", new[] { header.Author, $"{header.PackageVersion} · {header.BaseSkin} / {header.BaseScheme}", header.Description, "Revision " + theme.Revision[..12] }.Where(line => !string.IsNullOrEmpty(line)));
        thumbnail.Source = null; imageFrame.Visibility = Visibility.Collapsed; previewState.Visibility = Visibility.Visible;
        if (theme.Files.TryGetValue("preview.png", out var png)) { thumbnail.Source = ImageCodec.ToBitmap(ImageCodec.Decode(png, "preview.png")); imageFrame.Visibility = Visibility.Visible; previewState.Visibility = Visibility.Collapsed; }
        payloads.ItemsSource = header.Files.Select(file => $"{file.Path}  ({file.Size:N0} bytes)").ToArray();
    }
    private async Task UseSelectionAsync(bool open)
    {
        if (busy || entries.SelectedItem is not Row row) return;
        busy = true; root.IsEnabled = false; var ticket = ++selection;
        try
        {
            var (theme, catalog) = await ValidateAsync(row, ticket); ShowTheme(theme);
            if (open)
            {
                var picker = new ProjectCopyDialog(this, Path.Combine(projectParent, "Imported theme"), "Open package as project", "Create project", "Imported theme");
                if (picker.ShowDialog() == true) { SelectedTheme = theme; Destination = picker.Destination; busy = false; DialogResult = true; }
            }
            else { status.Text = "Starting isolated preview…"; await preview(theme, catalog); status.Text = "Native preview opened."; }
        }
        catch (Exception error) { status.Text = error.Message; previewButton.IsEnabled = openButton.IsEnabled = false; }
        finally { busy = false; root.IsEnabled = true; }
    }
}
