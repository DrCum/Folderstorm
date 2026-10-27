// SPDX-License-Identifier: LGPL-2.1-or-later
namespace Folderstorm.Theme.Editor;

public partial class MainWindow
{
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
