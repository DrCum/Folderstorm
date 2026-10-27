// SPDX-License-Identifier: LGPL-2.1-or-later
using Microsoft.Win32;
using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Threading;

namespace Folderstorm.Theme.Editor;

public partial class MainWindow
{
    private readonly Dictionary<string, (ExternalImageSession Session, FileSystemWatcher Watcher)> externalSessions = new(StringComparer.Ordinal);
    private readonly HashSet<string> changedExternalImages = new(StringComparer.Ordinal);
    private readonly DispatcherTimer externalRefresh = new() { Interval = TimeSpan.FromMilliseconds(200) };
    private string externalWorkRoot = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Folderstorm", "theme-editor", "working");
    private string externalEditorPath = "";
    public bool CanReloadExternal => CanEditImage && SelectedTexture is { } texture && project != null &&
        externalSessions.TryGetValue(texture.Name, out var item) && item.Session.Matches(projectDirectory, project, texture.Name);
    public bool CanBrowseWorkingFiles => CanOpenDocument && Directory.Exists(externalWorkRoot);
    private string ExternalEditorSetting => externalWorkRoot + ".editor.json";

    private void InitializeExternalEditing()
    {
        try
        {
            if (File.Exists(ExternalEditorSetting) && new FileInfo(ExternalEditorSetting).Length <= 8192)
                externalEditorPath = Contract.Read<string>(File.ReadAllBytes(ExternalEditorSetting));
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or ThemeException) { }
        externalRefresh.Tick += (_, _) =>
        {
            externalRefresh.Stop(); if (closing || closePending) return;
            RefreshExternalDisplay();
            if (changedExternalImages.Count > 0) StatusText.Text = "External copy changed: " + string.Join(", ", changedExternalImages.Take(3));
            changedExternalImages.Clear();
        };
        RefreshExternalDisplay();
    }
    private void RefreshExternalDisplay()
    {
        if (ExternalAppName == null || ExternalChangeStatus == null || ExternalPath == null) return;
        ExternalAppName.Text = File.Exists(externalEditorPath) ? Path.GetFileNameWithoutExtension(externalEditorPath) : "Choose an image editor";
        ExternalAppName.ToolTip = externalEditorPath;
        ExternalPath.Visibility = Visibility.Collapsed;
        ExternalChangeStatus.Text = "Save in your editor, then Reload.";
        if (project == null || SelectedTexture is not { } texture || !externalSessions.TryGetValue(texture.Name, out var item) ||
            !item.Session.Matches(projectDirectory, project, texture.Name)) return;
        try
        {
            ExternalPath.Text = item.Session.WorkingPath; ExternalPath.ToolTip = item.Session.WorkingPath; ExternalPath.Visibility = Visibility.Visible;
            ExternalChangeStatus.Text = item.Session.HasChanges ? "Changed externally. Reload to apply." : "Working copy is up to date.";
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or ThemeException) { ExternalChangeStatus.Text = error.Message; }
    }
    private void StopExternalSessions()
    {
        externalRefresh.Stop(); changedExternalImages.Clear();
        foreach (var item in externalSessions.Values) item.Watcher.Dispose();
        externalSessions.Clear(); RefreshExternalDisplay();
    }
    private bool ConfirmExternalChanges()
    {
        var pending = false;
        foreach (var item in externalSessions.Values)
            try { pending |= item.Session.HasChanges; }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException or ThemeException) { pending = true; }
        return !pending || MessageBox.Show(this, "External copies have changes that have not been reloaded. Continue without reloading?\n\nFiles are kept in:\n" + externalWorkRoot,
            "External edits", MessageBoxButton.YesNo, MessageBoxImage.Question, MessageBoxResult.No) == MessageBoxResult.Yes;
    }
    private void ChooseExternalEditor(object sender, RoutedEventArgs args)
    {
        if (!CanOpenDocument) return;
        var picker = new OpenFileDialog { Title = "Choose an image editor", Filter = "Applications|*.exe" };
        if (picker.ShowDialog(this) != true) return;
        externalEditorPath = picker.FileName;
        try { Contract.AtomicWrite(ExternalEditorSetting, Contract.Bytes(externalEditorPath)); }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException) { StatusText.Text = "Editor selected for this run; its path could not be saved."; }
        RefreshExternalDisplay();
    }
    private ExternalImageSession PrepareExternalCopy()
    {
        Contract.Require(Ready() && CanEditImage && SelectedTexture != null, "external_scope", "Choose an editable image first");
        var name = SelectedTexture.Name;
        if (externalSessions.TryGetValue(name, out var existing))
        {
            if (existing.Session.IsCurrent(projectDirectory, project!, catalog!) && File.Exists(existing.Session.WorkingPath)) return existing.Session;
            existing.Watcher.Dispose(); externalSessions.Remove(name);
        }
        Contract.Require(externalSessions.Count < 32, "external_limit", "At most 32 external copies can be watched at once. Reopen the project to start a new session.");
        var session = ExternalImageSession.FindPrevious(projectDirectory, project!, name, externalWorkRoot);
        if (session == null || !session.IsCurrent(projectDirectory, project!, catalog!))
            session = ExternalImageSession.Create(projectDirectory, project!, catalog!, name, externalWorkRoot, (bytes, file) => ImageCodec.EncodePng(ImageCodec.Decode(bytes, file)));
        var watchedSession = session;
        var watcher = session.Watch(() => Dispatcher.BeginInvoke(new Action(() =>
        {
            if (closing || closePending || !externalSessions.TryGetValue(name, out var active) || !ReferenceEquals(active.Session, watchedSession)) return;
            changedExternalImages.Add(name); externalRefresh.Stop(); externalRefresh.Start();
        }), DispatcherPriority.Background));
        externalSessions[name] = (session, watcher); RefreshExternalDisplay(); UpdateCommandState(); return session;
    }
    private async void OpenExternalCopy(object sender, RoutedEventArgs args) => await Run(() =>
    {
        if (!CanEditImage) return;
        if (!File.Exists(externalEditorPath)) ChooseExternalEditor(sender, args);
        if (!File.Exists(externalEditorPath)) return;
        Contract.Require(Path.GetExtension(externalEditorPath).Equals(".exe", StringComparison.OrdinalIgnoreCase), "external_editor", "Choose an editor application");
        var session = PrepareExternalCopy(); var start = new ProcessStartInfo(externalEditorPath) { UseShellExecute = false };
        start.ArgumentList.Add(session.WorkingPath);
        using var process = Process.Start(start) ?? throw new ThemeException("external_editor", "Image editor could not start");
        StatusText.Text = "Working copy opened. Save it, then Reload.";
    });
    private async void ReloadExternalCopy(object sender, RoutedEventArgs args) => await Run(() =>
    {
        if (!Ready() || !CanReloadExternal || SelectedTexture is not { } texture) return;
        var session = externalSessions[texture.Name].Session; CommitMetadata();
        if (session.Reload(projectDirectory, project!, catalog!, bytes => { _ = ImageCodec.Decode(bytes, "image.png"); }))
        {
            changed = true; RefreshImageEditors(); SyncMetadata();
            StatusText.Text = session.Warning ?? "External image reloaded. Undo is available.";
        }
        else StatusText.Text = "Working copy has no changes.";
        RefreshExternalDisplay();
    });
    private async void BrowseWorkingFiles(object sender, RoutedEventArgs args) => await Run(() =>
    {
        if (!CanBrowseWorkingFiles) return;
        var folder = SelectedTexture is { } texture && externalSessions.TryGetValue(texture.Name, out var item) ? item.Session.WorkingDirectory : externalWorkRoot;
        using var process = Process.Start(new ProcessStartInfo(folder) { UseShellExecute = true });
    });
}
