// SPDX-License-Identifier: LGPL-2.1-or-later
using Microsoft.Win32;
using System.Collections.Concurrent;
using System.Globalization;
using System.IO;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace Folderstorm.Theme.Editor;

public sealed record FontChoice(string File, string Path, string Source) { public string Label => File + " · " + Source; }
public partial class FontWorkbench : UserControl
{
    private BaseCatalog? catalog;
    private ThemeProject? project;
    private string directory = "";
    private byte[] config = [];
    private bool refreshing = true;
    private int inspectionGeneration;
    private readonly SemaphoreSlim inspectionGate = new(1, 1);
    private readonly ConcurrentDictionary<string, FontFaceInfo[]> inspected = new(StringComparer.OrdinalIgnoreCase);
    private FontChoice? inspectedChoice;
    private FontReference? inspectedReference;
    private FontRole? inspectedRole;
    public event Action? Edited;
    public FontWorkbench() { InitializeComponent(); refreshing = false; }
    private FontRole? SelectedRole => Roles?.SelectedItem as FontRole;
    private FontReference? SelectedReference => References?.SelectedItem as FontReference;
    public void SetProject(BaseCatalog? nextCatalog, ThemeProject? nextProject, string nextDirectory)
    {
        ++inspectionGeneration; catalog = nextCatalog; project = nextProject; directory = nextDirectory; inspected.Clear();
        if (catalog == null) return;
        Status.Text = "";
        refreshing = true;
        try
        {
            var previous = Configuration.SelectedItem as string; Configuration.ItemsSource = catalog.FontConfigurations;
            Configuration.SelectedItem = catalog.FontConfigurations.Contains(previous ?? "") ? previous : "fonts.xml";
            config = FontProject.Read(directory, project, catalog); RebuildChoices(); RefreshRoles();
            var sizes = FontProject.Sizes(config); var selectedSize = SizeName.SelectedItem as string;
            SizeName.ItemsSource = sizes.Keys.ToArray(); SizeName.SelectedItem = selectedSize != null && sizes.ContainsKey(selectedSize) ? selectedSize : sizes.Keys.FirstOrDefault();
        }
        catch (Exception error) { Status.Text = error.Message; config = []; Roles.ItemsSource = null; References.ItemsSource = null; }
        finally { refreshing = false; }
        ShowRole(); SelectSize(this, new SelectionChangedEventArgs(SelectorEvent(), Array.Empty<object>(), Array.Empty<object>()));
    }
    private static RoutedEvent SelectorEvent() => System.Windows.Controls.Primitives.Selector.SelectionChangedEvent;
    private void RebuildChoices()
    {
        if (catalog == null) return; var choices = new List<FontChoice>();
        if (project != null) choices.AddRange(project.Edits.Assets.Where(asset => asset.Kind == "font").Select(asset => new FontChoice(Path.GetFileName(asset.PayloadPath), Contract.Contained(directory, asset.AssetPath), "Embedded")));
        void AddFolder(string root, string source)
        {
            if (!Directory.Exists(root)) return;
            foreach (var path in Directory.EnumerateFiles(root).Order(StringComparer.Ordinal).Take(5000))
                if (Path.GetExtension(path).ToLowerInvariant() is ".ttf" or ".otf" or ".ttc" or ".otc") choices.Add(new(Path.GetFileName(path), path, source));
        }
        AddFolder(Path.Combine(catalog.Root, "fonts"), "Bundled"); AddFolder(Environment.GetFolderPath(Environment.SpecialFolder.Fonts), "Installed dependency");
        Available.ItemsSource = choices.GroupBy(choice => choice.File, StringComparer.OrdinalIgnoreCase).Select(group => group.First()).OrderBy(choice => choice.File, StringComparer.OrdinalIgnoreCase).ToArray();
    }
    private void RefreshRoles()
    {
        if (config.Length == 0) return; var previous = SelectedRole;
        var roles = FontProject.Roles(config).Where(role => role.Label.Contains(Search.Text, StringComparison.OrdinalIgnoreCase)).ToArray();
        Roles.ItemsSource = roles; Roles.SelectedItem = roles.FirstOrDefault(role => role.Name == previous?.Name && role.Style == previous.Style) ?? roles.FirstOrDefault(role => role.Name == "SansSerif" && role.Style.Length == 0) ?? roles.FirstOrDefault();
    }
    private void SearchRoles(object sender, TextChangedEventArgs args) { if (Roles == null) return; RefreshRoles(); }
    private void SelectRole(object sender, SelectionChangedEventArgs args) { if (!refreshing) ShowRole(); }
    private void ShowRole()
    {
        try
        {
            if (SelectedRole == null) { RoleName.Text = "No matching font roles"; References.ItemsSource = null; ClearFace(); EmbeddedLicensePanel.Visibility = Visibility.Collapsed; Details.Text = Coverage.Text = Specimen.Text = SpecimenSmall.Text = ""; return; }
            var previous = SelectedReference?.Index ?? 0; RoleName.Text = SelectedRole.Label;
            var files = FontProject.References(config, SelectedRole.Index); var wasRefreshing = refreshing; refreshing = true;
            try { References.ItemsSource = files; References.SelectedItem = files.ElementAtOrDefault(Math.Min(previous, files.Length - 1)); }
            finally { refreshing = wasRefreshing; }
            // Equal reference records need an explicit refresh when only their license/history changed.
            _ = RefreshReference();
        }
        catch (Exception error) { References.ItemsSource = null; ClearFace(); EmbeddedLicensePanel.Visibility = Visibility.Collapsed; Status.Text = error.Message; Details.Text = Coverage.Text = Specimen.Text = SpecimenSmall.Text = ""; }
    }
    private async void SelectReference(object sender, SelectionChangedEventArgs args)
    { if (!refreshing) await RefreshReference(); }
    private async Task RefreshReference()
    {
        var reference = SelectedReference; ++inspectionGeneration;
        ClearFace(); RefreshLicense();
        if (reference == null) return;
        var choice = ((IEnumerable<FontChoice>?)Available.ItemsSource)?.FirstOrDefault(choice => choice.File.Equals(reference.File, StringComparison.OrdinalIgnoreCase));
        if (choice == null)
        {
            var choices = ((IEnumerable<FontChoice>?)Available.ItemsSource)?.ToList() ?? [];
            choice = new(reference.File, "", "Missing on this machine"); choices.Add(choice); Available.ItemsSource = choices;
        }
        Available.SelectedItem = choice; SizeDelta.Text = reference.SizeDelta.ToString(CultureInfo.InvariantCulture); Weight.Text = reference.Weight?.ToString(CultureInfo.InvariantCulture) ?? "";
        Collection.IsChecked = reference.Collection; SyntheticBold.IsChecked = reference.Bold;
        Hinting.SelectedIndex = reference.Hinting switch { "default" => 1, "no_hinting" => 2, _ => 0 };
        await InspectSelection();
    }
    private FontChoice? Resolve(string file) => ((IEnumerable<FontChoice>?)Available.ItemsSource)?.FirstOrDefault(choice => choice.File.Equals(file, StringComparison.OrdinalIgnoreCase) && choice.Path.Length > 0);
    private FontFaceInfo[] Inspect(FontChoice choice)
    {
        if (inspected.TryGetValue(choice.Path, out var cached)) return cached;
        var info = new FileInfo(choice.Path); Contract.Require(info.Length <= 64L * 1024 * 1024, "font_data", "Font exceeds64 MiB");
        return FontInspection.Read(File.ReadAllBytes(choice.Path));
    }
    private async Task InspectSelection()
    {
        var generation = ++inspectionGeneration; var reference = SelectedReference; var role = SelectedRole;
        if (reference == null || role == null || catalog == null) return;
        Details.Text = "Reading font…"; Coverage.Text = ""; var choice = Resolve(reference.File);
        if (choice == null)
        {
            Details.Text = reference.File + " is missing on this machine. The viewer will continue through its fallback chain. This sample uses Segoe UI.";
            Specimen.FontFamily = SpecimenSmall.FontFamily = new FontFamily("Segoe UI"); Specimen.FontWeight = SpecimenSmall.FontWeight = FontWeights.Normal;
            Specimen.FontStyle = SpecimenSmall.FontStyle = FontStyles.Normal; ShowSample(); return;
        }
        try
        {
            var fallback = FontProject.Roles(config).FirstOrDefault(item => item.Name == "default");
            var sources = FontProject.References(config, role.Index).Concat(role.Name == "default" || fallback == null ? [] : FontProject.References(config, fallback.Index)).ToArray();
            var choices = sources.Select(source => (Reference: source, Choice: Resolve(source.File))).ToArray();
            var result = await Task.Run(async () =>
            {
                await inspectionGate.WaitAsync();
                try
                {
                    var output = new List<(FontReference Reference, FontChoice? Choice, FontFaceInfo[]? Faces)>();
                    foreach (var item in choices)
                    {
                        if (generation != Volatile.Read(ref inspectionGeneration)) return Array.Empty<(FontReference Reference, FontChoice? Choice, FontFaceInfo[]? Faces)>();
                        output.Add((item.Reference, item.Choice, item.Choice == null ? null : ReadSafely(item.Choice)));
                    }
                    return output.ToArray();
                }
                finally { inspectionGate.Release(); }
            });
            if (generation != inspectionGeneration) return;
            foreach (var item in result) if (item.Choice != null && item.Faces != null) inspected[item.Choice.Path] = item.Faces;
            var faces = result.FirstOrDefault(item => item.Choice?.Path == choice.Path).Faces;
            if (faces == null) { Details.Text = "This font could not be inspected within the supported metadata limits. Try another file or verify it in the viewer."; return; }
            SetInspectedFaces(choice, reference, role, faces);
            var missing = new List<string>(); var unknown = false; var used = new Dictionary<string, int>();
            foreach (var rune in SampleText.Text.EnumerateRunes().Where(rune => !Rune.IsWhiteSpace(rune)).Distinct())
            {
                string? found = null;
                foreach (var item in result)
                {
                    // Conditional emoji files are renderer-dependent and cannot establish unconditional coverage.
                    if (item.Reference.Functor.Length > 0) { unknown = true; continue; }
                    if (item.Faces == null) continue;
                    var active = item.Reference.Collection ? item.Faces : item.Faces.Take(1);
                    if (active.Any(face => !face.Characters.Supported)) unknown = true;
                    if (active.Any(face => face.Characters.HasGlyph(rune.Value))) { found = item.Reference.File; break; }
                }
                if (found == null) missing.Add("U+" + rune.Value.ToString("X4") + " " + rune); else used[found] = used.GetValueOrDefault(found) + 1;
            }
            Coverage.Text = "Sample glyph mapping: " + string.Join(", ", used.Select(item => item.Key + " (" + item.Value + ")")) +
                (missing.Count == 0 ? "\nAll sample code points map in the inspected fallback files." : "\nNot mapped in inspected files: " + string.Join(" · ", missing)) +
                (unknown ? "\nConditional emoji, unsupported maps and native system fallbacks need viewer verification." : "") + "\nThis checks code points, not shaping, variation selectors or native rendering.";
        }
        catch (Exception error) { if (generation == inspectionGeneration) Details.Text = error.Message; }
    }
    private void ClearFace()
    {
        inspectedChoice = null; inspectedReference = null; inspectedRole = null;
        FaceChoice.ItemsSource = null; FacePanel.Visibility = Visibility.Collapsed; FaceCoverage.Text = "";
    }
    private void SetInspectedFaces(FontChoice choice, FontReference reference, FontRole role, FontFaceInfo[] faces)
    {
        var previous = inspectedChoice?.Path == choice.Path ? (FaceChoice.SelectedItem as FontFaceInfo)?.Index ?? 0 : 0;
        inspectedChoice = choice; inspectedReference = reference; inspectedRole = role;
        FaceChoice.ItemsSource = faces; FaceChoice.SelectedIndex = Math.Clamp(previous, 0, faces.Length - 1);
        FacePanel.Visibility = faces.Length > 1 ? Visibility.Visible : Visibility.Collapsed; ShowSelectedFace();
    }
    private void SelectFace(object sender, SelectionChangedEventArgs args) => ShowSelectedFace();
    private void ShowSelectedFace()
    {
        if (FaceChoice.SelectedItem is not FontFaceInfo face || inspectedChoice is not { } choice || inspectedReference is not { } reference || inspectedRole is not { } role) return;
        Details.Text = choice.Source + " · " + FaceChoice.Items.Count + " face(s)\n" + face.Label + "\n" +
            (face.Axes.Length == 0 ? "Static font" : string.Join(", ", face.Axes.Select(axis => axis.Tag + " " + axis.Minimum + ".." + axis.Maximum + " (default " + axis.Default + ")"))) +
            "\n" + face.EmbeddingNotice + "\n\n" + face.Copyright + "\n" + (face.License.Length == 0 ? "No license text in the font. Add its redistribution license when embedding." : face.License) + "\n" + face.LicenseUrl +
            (reference.Functor.Length == 0 ? "" : "\nNative character filter: " + reference.Functor) + "\nNative weight controls wght only. Other axes remain at the font default. Collection loading uses all faces in file order.";
        try
        {
            Specimen.FontFamily = SpecimenSmall.FontFamily = new FontFamily(new Uri(Path.GetDirectoryName(choice.Path)! + Path.DirectorySeparatorChar), "./" + Path.GetFileName(choice.Path) + "#" + face.Family);
            Specimen.FontWeight = SpecimenSmall.FontWeight = FontWeight.FromOpenTypeWeight(reference.Weight ?? (reference.Bold || role.Style.Contains("BOLD", StringComparison.Ordinal) || face.Style.Contains("Bold", StringComparison.OrdinalIgnoreCase) ? 700 : 400));
            Specimen.FontStyle = SpecimenSmall.FontStyle = role.Style.Contains("ITALIC", StringComparison.Ordinal) || face.Style.Contains("Italic", StringComparison.OrdinalIgnoreCase) ? FontStyles.Italic : FontStyles.Normal;
        }
        catch (ArgumentException) { Specimen.FontFamily = SpecimenSmall.FontFamily = new FontFamily("Segoe UI"); }
        ShowSample();
        var scalars = SampleText.Text.EnumerateRunes().Where(rune => !Rune.IsWhiteSpace(rune)).Distinct().ToArray();
        var missing = scalars.Where(rune => !face.Characters.HasGlyph(rune.Value)).Select(rune => "U+" + rune.Value.ToString("X4")).ToArray();
        FaceCoverage.Text = FaceChoice.Items.Count <= 1 ? "" : !face.Characters.Supported ? "Selected face has an unsupported Unicode map; coverage is unknown." :
            "Face " + face.Index + " only: " + (scalars.Length - missing.Length) + "/" + scalars.Length + " sample code points mapped." + (missing.Length == 0 ? "" : " Unmapped: " + string.Join(", ", missing)) + " Native collection loading remains unchanged.";
    }
    private void RefreshLicense()
    {
        EmbeddedLicensePanel.Visibility = Visibility.Collapsed;
        if (project == null || SelectedReference == null || !project.Edits.Assets.Any(asset => asset.Kind == "font" && asset.PayloadPath == "viewer/fonts/" + SelectedReference.File)) return;
        try
        {
            var license = FontProject.ReadLicense(directory, project, SelectedReference.File);
            LicensePath.Text = license.PayloadPath; PackagedLicense.Text = license.Text; RemoveLicenseButton.IsEnabled = license.Exists;
            EmbeddedLicensePanel.Visibility = Visibility.Visible;
        }
        catch (Exception error) { Status.Text = error.Message; }
    }
    private void SaveLicense(object sender, RoutedEventArgs args) => Action(() => SelectedReference != null && FontProject.SetLicense(directory, project!, catalog!, SelectedReference.File, PackagedLicense.Text), "License saved in the package.");
    private void RemoveLicense(object sender, RoutedEventArgs args) => Action(() => SelectedReference != null && FontProject.RemoveLicense(directory, project!, catalog!, SelectedReference.File), "Packaged license removed.");
    private FontFaceInfo[]? ReadSafely(FontChoice choice) { try { return Inspect(choice); } catch (Exception error) when (error is ThemeException or IOException or UnauthorizedAccessException) { return null; } }
    private void SampleChanged(object sender, TextChangedEventArgs args) { if (Specimen == null) return; ShowSample(); if (!refreshing && SelectedReference != null) _ = InspectSelection(); }
    private void ShowSample() { if (SampleText == null || SpecimenSmall == null) return; Specimen.Text = SampleText.Text; SpecimenSmall.Text = "Aa Bb 0123456789 · Русский · 中文 · 🙂"; }
    private void Action(Func<bool> edit, string message)
    {
        try
        {
            if (project == null || catalog == null || DataContext is MainWindow { CanEditDocument: false }) return;
            if (!edit()) { Status.Text = "No font changes."; return; }
            Edited?.Invoke(); SetProject(catalog, project, directory); Status.Text = message + " Native preview reloads the fonts.";
        }
        catch (Exception error) { Status.Text = error.Message; }
    }
    private FontReferenceEdit Draft()
    {
        Contract.Require(Available.SelectedItem is FontChoice, "font", "Choose a font file"); var choice = (FontChoice)Available.SelectedItem;
        Contract.Require(double.TryParse(SizeDelta.Text, NumberStyles.Float, CultureInfo.InvariantCulture, out var delta), "font", "Enter a size adjustment");
        int? weight = null; if (!string.IsNullOrWhiteSpace(Weight.Text)) { Contract.Require(int.TryParse(Weight.Text, NumberStyles.Integer, CultureInfo.InvariantCulture, out var parsed), "font", "Enter a whole weight or leave it blank"); weight = parsed; }
        return new(choice.File, delta, weight, Collection.IsChecked == true, ((ComboBoxItem)Hinting.SelectedItem).Content.ToString()!, SyntheticBold.IsChecked == true);
    }
    private void UseConfiguration(object sender, RoutedEventArgs args) => Action(() => Configuration.SelectedItem is string name && FontProject.UseConfiguration(directory, project!, catalog!, name), "Configuration applied.");
    private void ResetFonts(object sender, RoutedEventArgs args) => Action(() => FontProject.Reset(project!), "Inherited fonts restored.");
    private void SetFile(object sender, RoutedEventArgs args) => Action(() => SelectedRole != null && SelectedReference != null && FontProject.Apply(directory, project!, catalog!, FontProject.SetReference(config, SelectedRole.Index, SelectedReference.Index, Draft()), "Edit font file"), "File updated.");
    private void AddFallback(object sender, RoutedEventArgs args) => Action(() => SelectedRole != null && FontProject.Apply(directory, project!, catalog!, FontProject.AddReference(config, SelectedRole.Index, Draft()), "Add font fallback"), "Fallback added.");
    private void Move(int direction) => Action(() => SelectedRole != null && SelectedReference != null && FontProject.Apply(directory, project!, catalog!, FontProject.MoveReference(config, SelectedRole.Index, SelectedReference.Index, direction), "Reorder font fallback"), "Fallback reordered.");
    private void MoveUp(object sender, RoutedEventArgs args) => Move(-1);
    private void MoveDown(object sender, RoutedEventArgs args) => Move(1);
    private void RemoveFile(object sender, RoutedEventArgs args) => Action(() => SelectedRole != null && SelectedReference != null && FontProject.Apply(directory, project!, catalog!, FontProject.RemoveReference(config, SelectedRole.Index, SelectedReference.Index), "Remove font fallback"), "Fallback removed.");
    private void SelectSize(object sender, SelectionChangedEventArgs args) { if (NativeSize != null && config.Length > 0 && SizeName.SelectedItem is string name) NativeSize.Text = FontProject.Sizes(config)[name].ToString(CultureInfo.InvariantCulture); }
    private void SetNativeSize(object sender, RoutedEventArgs args) => Action(() => { Contract.Require(double.TryParse(NativeSize.Text, NumberStyles.Float, CultureInfo.InvariantCulture, out var size), "font", "Enter a point size"); return SizeName.SelectedItem is string name && FontProject.Apply(directory, project!, catalog!, FontProject.SetSize(config, name, size), "Edit font size " + name); }, "Size updated.");
    private void EmbedFont(object sender, RoutedEventArgs args)
    {
        if (project == null || catalog == null || SelectedRole == null || DataContext is MainWindow { CanEditDocument: false }) return;
        var picker = new OpenFileDialog { Filter = "OpenType font|*.ttf;*.otf;*.ttc;*.otc", Title = "Embed font in selected role" };
        if (picker.ShowDialog(Window.GetWindow(this)) != true) return;
        Action(() => { Contract.Require(new FileInfo(picker.FileName).Length <= 64L * 1024 * 1024, "font_data", "Font exceeds64 MiB"); return FontProject.Embed(directory, project, catalog, SelectedRole.Index, Path.GetExtension(picker.FileName), File.ReadAllBytes(picker.FileName), LicenseText.Text); }, "Font embedded at the start of this role.");
    }
}
