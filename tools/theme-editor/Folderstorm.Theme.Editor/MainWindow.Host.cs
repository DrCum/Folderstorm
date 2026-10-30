// SPDX-License-Identifier: LGPL-2.1-or-later
using System.IO;
using System.Windows;
namespace Folderstorm.Theme.Editor;

public partial class MainWindow
{
    private readonly HostPreviewSession hostPreview = new();
    private string hostExecutable = "";
    private async void PreviewHost(object sender, RoutedEventArgs args)
    {
        if (!Ready()) return;
        try
        {
            previewOperation = true; UpdateCommandState(); Workspace.IsEnabled = false;
            var candidate = project! with { DisplayName = ThemeName.Text, Author = ThemeAuthor.Text, PackageVersion = PackageVersion.Text, Description = ThemeDescription.Text };
            var theme = HostProject.Read(projectDirectory, candidate) == null ? null : PackageStore.Compile(projectDirectory, candidate, catalog!);
            var executable = hostExecutable.Length > 0 ? hostExecutable : Path.Combine(Path.GetDirectoryName(Path.GetFullPath(ViewerPath.Text))!, "folderstorm-session-host.exe");
            await hostPreview.StartAsync(executable, theme, catalog!, HostWorkspace.BaselineChoice);
            StatusText.Text = hostPreview.Receipt!.State == "ready" ? "Native host preview ready." : "Built-in host fallback: " + hostPreview.Receipt.Diagnostic;
        }
        catch (Exception error) { StatusText.Text = error.Message; }
        finally { previewOperation = false; Workspace.IsEnabled = true; UpdateCommandState(); }
    }
    private void ApplyHostPalette(HostPaletteDefinition? palette)
    {
        if (!Ready()) return;
        var candidate = project! with
        {
            Edits = Contract.Read<ProjectEdits>(Contract.Bytes(project.Edits)), Undo = [], Redo = [],
            DisplayName = ThemeName.Text, Author = ThemeAuthor.Text, PackageVersion = PackageVersion.Text, Description = ThemeDescription.Text
        };
        var applied = palette == null ? HostProject.Reset(projectDirectory, candidate, catalog!) : HostProject.Apply(projectDirectory, candidate, catalog!, palette);
        if (!applied) { StatusText.Text = "Host palette already matches."; return; }
        CommitMetadata(); project.Checkpoint(candidate.Undo[^1].Label); project.Edits = candidate.Edits;
        changed = true; RefreshDocumentEditors(); StatusText.Text = palette == null ? "Host override removed. Undo restores it." : "Host palette set. Save to keep it in the project.";
    }
}
