// SPDX-License-Identifier: LGPL-2.1-or-later
using System.Windows;

namespace Folderstorm.Theme.Editor;

public partial class MainWindow
{
    private async void ReviewChanges(object sender, RoutedEventArgs args) => await Run(() =>
    {
        if (!Ready()) return;
        var dialog = new ProjectChangesDialog(this, ProjectChanges.Inspect(projectDirectory, project!, catalog!));
        if (dialog.ShowDialog() == true && dialog.Request is { } request) ApplyChangeReset(request);
    });
    private void ApplyChangeReset(ChangeReset request)
    {
        if (!Ready()) return;
        var candidate = project! with
        {
            Edits = Contract.Read<ProjectEdits>(Contract.Bytes(project.Edits)), Undo = [], Redo = [],
            DisplayName = ThemeName.Text, Author = ThemeAuthor.Text, PackageVersion = PackageVersion.Text, Description = ThemeDescription.Text
        };
        if (ProjectChanges.Reset(projectDirectory, candidate, catalog!, request, (bytes, file) => ImageCodec.EncodePng(ImageCodec.Decode(bytes, file))))
        {
            var previous = project.Edits;
            CommitMetadata(); project.Checkpoint(candidate.Undo[^1].Label); project.Edits = candidate.Edits;
            changed = true; RefreshDocumentEditors();
            RefreshChangedXml(previous);
            StatusText.Text = "Overrides reset. Undo restores them.";
        }
        else StatusText.Text = "Appearance already matches.";
    }
    private void RefreshChangedXml(ProjectEdits previous)
    {
        if (XmlFile.SelectedItem is string file && previous.Assets.SingleOrDefault(asset => asset.PayloadPath == "viewer/" + file)?.AssetPath != project?.Edits.Assets.SingleOrDefault(asset => asset.PayloadPath == "viewer/" + file)?.AssetPath)
            LoadXml(this, new RoutedEventArgs());
    }
}
