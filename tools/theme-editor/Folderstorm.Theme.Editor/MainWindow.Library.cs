// SPDX-License-Identifier: LGPL-2.1-or-later
using Microsoft.Win32;
using System.IO;
using System.Windows;

namespace Folderstorm.Theme.Editor;

public partial class MainWindow
{
    private bool previewOperation;
    private string? thumbnailHash;
    private void RefreshThumbnail()
    {
        if (ThumbnailImage == null) return;
        try
        {
            var bytes = project == null ? null : ThumbnailProject.Read(projectDirectory, project);
            if (bytes == null) { ThumbnailImage.Source = null; thumbnailHash = null; ThumbnailState.Text = "No preview image"; return; }
            var hash = Contract.Hash(bytes); if (hash == thumbnailHash) return;
            var decoded = ImageCodec.Decode(bytes, "preview.png"); ThumbnailImage.Source = ImageCodec.ToBitmap(decoded); thumbnailHash = hash;
            ThumbnailState.Text = $"{decoded.Width} × {decoded.Height} · Included in exported packages";
        }
        catch (Exception error) { ThumbnailImage.Source = null; thumbnailHash = null; ThumbnailState.Text = error.Message; }
    }
    private bool SetThumbnail(byte[] png)
    {
        if (!Ready()) return false;
        _ = ImageCodec.Decode(png, "preview.png");
        CommitMetadata(); var applied = ThumbnailProject.SetPng(projectDirectory, project!, catalog!, png);
        if (applied) { changed = true; RefreshDocumentEditors(); }
        return applied;
    }
    private async void ImportThumbnail(object sender, RoutedEventArgs args)
    {
        if (!Ready()) return;
        var picker = new OpenFileDialog { Filter = "PNG image|*.png" }; if (picker.ShowDialog(this) != true) return;
        await Run(() => { SetThumbnail(LibraryCatalog.ReadBounded(picker.FileName, 32L * 1024 * 1024)); StatusText.Text = "Preview image set."; });
    }
    private async void ResetThumbnail(object sender, RoutedEventArgs args) => await Run(() =>
    {
        if (!Ready()) return;
        CommitMetadata(); if (ThumbnailProject.Reset(projectDirectory, project!, catalog!)) { changed = true; RefreshDocumentEditors(); }
        StatusText.Text = "Preview image removed. Undo restores it.";
    });
    private async void CaptureThumbnail(object sender, RoutedEventArgs args)
    {
        if (!Ready()) return;
        try
        {
            CommitMetadata(); var theme = PackageStore.Compile(projectDirectory, project!, catalog!);
            previewOperation = true; UpdateCommandState(); Workspace.IsEnabled = false;
            StatusText.Text = "Starting native preview for this project…";
            await preview.StartAsync(ViewerPath.Text, theme, catalog!, requiresCapture: true);
            StatusText.Text = "Capturing native preview…"; var capture = await preview.CaptureAsync();
            _ = ImageCodec.Decode(capture.Png, "preview.png");
            if (ThumbnailProject.SetPng(projectDirectory, project!, catalog!, capture.Png)) changed = true;
            RefreshDocumentEditors(); StatusText.Text = "Native preview image set. Save to keep it in the project.";
        }
        catch (Exception error) { StatusText.Text = error.Message; }
        finally { previewOperation = false; Workspace.IsEnabled = true; UpdateCommandState(); }
    }
    private void OpenLibrary(object sender, RoutedEventArgs args)
    {
        if (!CanOpenDocument) return;
        try
        {
            var dialog = new LibraryDialog(this, BasePath.Text, async (theme, source) =>
            {
                await preview.StartAsync(ViewerPath.Text, theme, source);
                Contract.Require(preview.Status == "ready", "preview", "Viewer used its built-in fallback; this package could not be previewed.");
            }, parent: projectDirectory.Length > 0 ? Path.GetDirectoryName(projectDirectory) : null);
            if (dialog.ShowDialog() != true || dialog.SelectedTheme == null || !Discard()) return;
            PackageStore.ImportProject(dialog.Destination, dialog.SelectedTheme); Open(dialog.Destination);
        }
        catch (Exception error) { StatusText.Text = error.Message; }
        finally { UpdateCommandState(); }
    }
}
