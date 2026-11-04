// SPDX-License-Identifier: LGPL-2.1-or-later
using Microsoft.Win32;
using System.IO;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Automation;
using System.Windows.Media;

namespace Folderstorm.Theme.Editor;

public sealed class ProjectCopyDialog : Window
{
    public string Destination { get; private set; } = "";
    public ProjectCopyDialog(Window owner, string source, string title = "Save project as", string action = "Save copy", string? folderName = null)
    {
        Owner = owner; Title = title; Width = 580; SizeToContent = SizeToContent.Height;
        ResizeMode = ResizeMode.NoResize; WindowStartupLocation = WindowStartupLocation.CenterOwner;
        Background = new SolidColorBrush(Color.FromRgb(17, 16, 22)); Foreground = Brushes.White;
        var panel = new StackPanel { Margin = new Thickness(20) }; Content = panel;
        panel.Children.Add(new TextBlock { Text = "Parent folder", Margin = new Thickness(0, 0, 0, 6) });
        var pathRow = new DockPanel(); panel.Children.Add(pathRow);
        var browse = new Button { Content = "Browse…", Margin = new Thickness(10, 0, 0, 0) }; DockPanel.SetDock(browse, Dock.Right); pathRow.Children.Add(browse);
        var parent = new TextBox { Text = Path.GetDirectoryName(source) ?? source, IsReadOnly = true, VerticalContentAlignment = VerticalAlignment.Center };
        AutomationProperties.SetName(parent, "Copy parent folder");
        pathRow.Children.Add(parent);
        panel.Children.Add(new TextBlock { Text = "New folder name", Margin = new Thickness(0, 18, 0, 6) });
        var initial = folderName ?? Path.GetFileName(Path.TrimEndingDirectorySeparator(source)) + " copy";
        for (var index = 2; Directory.Exists(Path.Combine(parent.Text, initial)) && index < 1000; ++index)
            initial = (folderName ?? Path.GetFileName(Path.TrimEndingDirectorySeparator(source)) + " copy") + " " + index;
        var name = new TextBox { Text = initial }; AutomationProperties.SetName(name, "New project folder name"); panel.Children.Add(name);
        var error = new TextBlock { Foreground = new SolidColorBrush(Color.FromRgb(242, 173, 173)), TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 10, 0, 0) }; panel.Children.Add(error);
        var actions = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right, Margin = new Thickness(0, 12, 0, 0) }; panel.Children.Add(actions);
        var cancel = new Button { Content = "Cancel", IsCancel = true }; var save = new Button { Content = action, IsDefault = true, Margin = new Thickness(0) };
        actions.Children.Add(cancel); actions.Children.Add(save);
        browse.Click += (_, _) => { var picker = new OpenFolderDialog { Title = "Choose the parent folder", InitialDirectory = parent.Text }; if (picker.ShowDialog(this) == true) parent.Text = picker.FolderName; };
        save.Click += (_, _) =>
        {
            try
            {
                Contract.PathName(name.Text); Contract.Require(!name.Text.Contains('/'), "path", "Use a single folder name");
                Contract.Require(Directory.Exists(parent.Text), "project_directory", "Choose an existing parent folder");
                Destination = Contract.Contained(parent.Text, name.Text);
                Contract.Require(!Directory.Exists(Destination) && !File.Exists(Destination), "project_exists", "Choose a new folder name");
                DialogResult = true;
            }
            catch (Exception exception) when (exception is ThemeException or IOException or ArgumentException) { error.Text = exception.Message; }
        };
        Loaded += (_, _) => { name.Focus(); name.SelectAll(); };
    }
}
