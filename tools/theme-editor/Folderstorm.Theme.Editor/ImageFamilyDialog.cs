// SPDX-License-Identifier: LGPL-2.1-or-later
using System.Globalization;
using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Documents;
using System.Windows.Media;

namespace Folderstorm.Theme.Editor;

public sealed class ImageFamilyDialog : Window
{
    private readonly FamilyImage[] entries;
    private readonly TextureGeometry? sourceGuides;
    private readonly Dictionary<string, (CheckBox Check, Image After, TextBlock Dimensions)> cards = new(StringComparer.Ordinal);
    private readonly WrapPanel cardPanel = new();
    private readonly StackPanel options = new();
    private readonly ComboBox operation = new() { SelectedIndex = 0 };
    private readonly TextBox tint = new() { Text = "#FFFFFFFF" };
    private readonly TextBox width = new() { Text = "32" }, height = new() { Text = "24" };
    private readonly ComboBox interpolation = new() { SelectedIndex = 0 };
    private readonly Slider brightness = Slider(-1, 1, 0), contrast = Slider(-1, 1, 0), saturation = Slider(0, 2, 1), gamma = Slider(.1, 5, 1), alpha = Slider(0, 2, 1);
    private readonly TextBlock status = new() { TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 12, 0, 8) };
    private readonly Button preview = new() { Content = "Preview checked" }, apply = new() { Content = "Apply", IsEnabled = false };
    private readonly StackPanel tintPanel = new(), adjustPanel = new(), resizePanel = new();
    private readonly TextBlock guidePanel = new() { Text = "Scale the current image's clip and slice guides to each checked state.", TextWrapping = TextWrapping.Wrap };
    private FamilyResult[] prepared = [];
    private bool ready, busy, closed;
    public ImageEdit[] Edits { get; private set; } = [];
    public string EditLabel { get; private set; } = "Edit image family";

    public ImageFamilyDialog(Window? owner, string family, FamilyImage[] images, string selected, string initialTint)
    {
        if (owner != null) Owner = owner;
        Title = "Edit image family"; Width = 1060; Height = 720; MinWidth = 820; MinHeight = 540;
        WindowStartupLocation = owner == null ? WindowStartupLocation.CenterScreen : WindowStartupLocation.CenterOwner;
        Background = new SolidColorBrush(Color.FromRgb(17, 16, 22)); Foreground = Brushes.White;
        entries = images; sourceGuides = entries.FirstOrDefault(item => item.Name == selected)?.Geometry;
        var selectedImage = entries.FirstOrDefault(item => item.Name == selected)?.Image;
        if (selectedImage != null) { width.Text = selectedImage.Width.ToString(); height.Text = selectedImage.Height.ToString(); }
        tint.Text = initialTint;
        var root = new DockPanel { Margin = new Thickness(20) }; TextElement.SetForeground(root, Foreground); Content = root;
        var heading = new DockPanel { Margin = new Thickness(0, 0, 0, 14) }; DockPanel.SetDock(heading, Dock.Top); root.Children.Add(heading);
        var choices = new StackPanel { Orientation = Orientation.Horizontal }; DockPanel.SetDock(choices, Dock.Right); heading.Children.Add(choices);
        var all = new Button { Content = "All editable" }; var none = new Button { Content = "None" }; choices.Children.Add(all); choices.Children.Add(none);
        heading.Children.Add(new TextBlock { Text = family, FontSize = 22, VerticalAlignment = VerticalAlignment.Center, TextTrimming = TextTrimming.CharacterEllipsis });
        var actions = new StackPanel { Orientation = Orientation.Horizontal, HorizontalAlignment = HorizontalAlignment.Right, Margin = new Thickness(0, 14, 0, 0) };
        DockPanel.SetDock(actions, Dock.Bottom); root.Children.Add(actions);
        actions.Children.Add(new Button { Content = "Cancel", IsCancel = true }); actions.Children.Add(preview); actions.Children.Add(apply);
        var inspector = new StackPanel { Width = 260, Margin = new Thickness(18, 0, 0, 0) };
        var inspectorScroll = new ScrollViewer { Content = inspector, VerticalScrollBarVisibility = ScrollBarVisibility.Auto, HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled, Width = 292 };
        DockPanel.SetDock(inspectorScroll, Dock.Right); root.Children.Add(inspectorScroll);
        inspector.Children.Add(options);
        options.Children.Add(Label("Operation")); options.Children.Add(operation);
        foreach (var label in new[] { "Tint", "Adjust", "Resize", "Guides from current" }) operation.Items.Add(label);
        options.Children.Add(tintPanel); options.Children.Add(adjustPanel); options.Children.Add(resizePanel); options.Children.Add(guidePanel);
        tintPanel.Children.Add(Label("Color / opacity"));
        var colorRow = new DockPanel(); tintPanel.Children.Add(colorRow);
        var pick = new Button { Content = "Pick", Margin = new Thickness(8, 0, 0, 0) }; DockPanel.SetDock(pick, Dock.Right); colorRow.Children.Add(pick); colorRow.Children.Add(tint);
        pick.Click += (_, _) =>
        {
            try { var picker = new ColorPickerDialog(ThemeColor.Parse(tint.Text)) { Owner = this }; if (picker.ShowDialog() == true) tint.Text = picker.SelectedColor.Hex(); }
            catch (ThemeException exception) { status.Text = exception.Message; }
        };
        foreach (var (label, slider) in new[] { ("Brightness", brightness), ("Contrast", contrast), ("Saturation", saturation), ("Gamma", gamma), ("Alpha", alpha) })
        { adjustPanel.Children.Add(Label(label)); adjustPanel.Children.Add(slider); AutomationProperties.SetName(slider, "Family " + label); slider.ValueChanged += (_, _) => Invalidate(); }
        resizePanel.Children.Add(Label("Width")); resizePanel.Children.Add(width); resizePanel.Children.Add(Label("Height")); resizePanel.Children.Add(height);
        interpolation.Items.Add("Nearest pixel"); interpolation.Items.Add("Bilinear"); resizePanel.Children.Add(Label("Interpolation")); resizePanel.Children.Add(interpolation);
        inspector.Children.Add(status);
        var scroller = new ScrollViewer { Content = cardPanel, VerticalScrollBarVisibility = ScrollBarVisibility.Auto, HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled };
        root.Children.Add(scroller);
        foreach (var entry in entries) AddCard(entry);
        AutomationProperties.SetName(tint, "Family tint"); AutomationProperties.SetName(width, "Family image width"); AutomationProperties.SetName(height, "Family image height");
        AutomationProperties.SetName(operation, "Family operation"); AutomationProperties.SetName(interpolation, "Family resize interpolation");
        operation.SelectionChanged += (_, _) => { ShowOptions(); Invalidate(); };
        interpolation.SelectionChanged += (_, _) => Invalidate();
        tint.TextChanged += (_, _) => Invalidate(); width.TextChanged += (_, _) => Invalidate(); height.TextChanged += (_, _) => Invalidate();
        all.Click += (_, _) => { if (!busy) foreach (var entry in entries.Where(item => item.CanEdit)) cards[entry.Name].Check.IsChecked = true; };
        none.Click += (_, _) => { if (!busy) foreach (var card in cards.Values) card.Check.IsChecked = false; };
        preview.Click += async (_, _) => await PreviewAsync();
        apply.Click += (_, _) =>
        {
            if (busy || prepared.Length == 0) return;
            try { Edits = prepared.Where(item => item.Changed).Select(item => item.Encode()).ToArray(); EditLabel = operation.Text + " " + family; DialogResult = true; }
            catch (Exception exception) { Edits = []; status.Text = exception.Message; }
        };
        Closed += (_, _) => closed = true;
        ready = true; ShowOptions(); Invalidate();
    }
    private static Slider Slider(double minimum, double maximum, double value) => new()
    { Minimum = minimum, Maximum = maximum, Value = value, SmallChange = .01, LargeChange = .1, AutoToolTipPlacement = System.Windows.Controls.Primitives.AutoToolTipPlacement.TopLeft, AutoToolTipPrecision = 2 };
    private static TextBlock Label(string text) => new() { Text = text, Foreground = new SolidColorBrush(Color.FromRgb(173, 169, 186)), Margin = new Thickness(0, 12, 0, 6) };
    private void AddCard(FamilyImage entry)
    {
        var panel = new StackPanel();
        var check = new CheckBox { Content = entry.State, IsChecked = entry.CanEdit, IsEnabled = entry.CanEdit, Foreground = Brushes.White, Margin = new Thickness(0, 0, 0, 8) };
        AutomationProperties.SetName(check, "Edit " + entry.Name); panel.Children.Add(check);
        panel.Children.Add(new TextBlock { Text = entry.Name, FontSize = 11, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 0, 0, 8) });
        var pair = new System.Windows.Controls.Primitives.UniformGrid { Columns = 2 }; panel.Children.Add(pair);
        var before = Thumbnail(entry.Image); var after = Thumbnail(null);
        foreach (var (label, image) in new[] { ("Before", before), ("After", after) })
        {
            var sample = new StackPanel(); sample.Children.Add(new TextBlock { Text = label, FontSize = 11, Margin = new Thickness(0, 0, 0, 4) });
            sample.Children.Add(new Border { Background = (Brush)Application.Current.FindResource("Checkerboard"), Child = image, Padding = new Thickness(5), Margin = new Thickness(0, 0, 5, 0) }); pair.Children.Add(sample);
        }
        var dimensions = new TextBlock { Text = entry.Error ?? $"{entry.Image!.Width} × {entry.Image.Height}", FontSize = 11, TextWrapping = TextWrapping.Wrap, Margin = new Thickness(0, 8, 0, 0) }; panel.Children.Add(dimensions);
        cards.Add(entry.Name, (check, after, dimensions)); check.Checked += (_, _) => Invalidate(); check.Unchecked += (_, _) => Invalidate();
        cardPanel.Children.Add(new Border { Width = 220, Padding = new Thickness(12), Margin = new Thickness(0, 0, 10, 10), BorderThickness = new Thickness(1), BorderBrush = new SolidColorBrush(Color.FromRgb(81, 68, 95)), Child = panel });
    }
    private static Image Thumbnail(RasterImage? image)
    {
        var control = new Image { Width = 90, Height = 76, Stretch = Stretch.Uniform, Source = image == null ? null : ImageCodec.ToBitmap(image) };
        RenderOptions.SetBitmapScalingMode(control, BitmapScalingMode.NearestNeighbor); return control;
    }
    private void ShowOptions()
    {
        tintPanel.Visibility = operation.SelectedIndex == 0 ? Visibility.Visible : Visibility.Collapsed;
        adjustPanel.Visibility = operation.SelectedIndex == 1 ? Visibility.Visible : Visibility.Collapsed;
        resizePanel.Visibility = operation.SelectedIndex == 2 ? Visibility.Visible : Visibility.Collapsed;
        guidePanel.Visibility = operation.SelectedIndex == 3 ? Visibility.Visible : Visibility.Collapsed;
    }
    private void Invalidate()
    {
        if (!ready || busy) return;
        prepared = []; apply.IsEnabled = false;
        var count = cards.Values.Count(item => item.Check.IsChecked == true); preview.IsEnabled = count > 0;
        status.Text = count + " checked. Preview before applying.";
        foreach (var entry in entries) { cards[entry.Name].After.Source = null; cards[entry.Name].Dimensions.Text = entry.Error ?? $"{entry.Image!.Width} × {entry.Image.Height}"; }
    }
    private FamilySettings Settings()
    {
        var mode = (FamilyOperation)operation.SelectedIndex;
        int Dimension(TextBox box) { Contract.Require(int.TryParse(box.Text, NumberStyles.Integer, CultureInfo.InvariantCulture, out var value), "image", "Use whole pixel dimensions"); return value; }
        return new(mode, mode == FamilyOperation.Tint ? ThemeColor.Parse(tint.Text) : ThemeColor.Parse("#FFFFFFFF"), brightness.Value, contrast.Value, saturation.Value, gamma.Value, alpha.Value,
            mode == FamilyOperation.Resize ? Dimension(width) : 32, mode == FamilyOperation.Resize ? Dimension(height) : 24,
            interpolation.SelectedIndex == 0 ? ImageInterpolation.Nearest : ImageInterpolation.Bilinear, sourceGuides);
    }
    private async Task PreviewAsync()
    {
        if (busy) return;
        try
        {
            var selection = entries.Where(entry => cards[entry.Name].Check.IsChecked == true).ToArray(); var settings = Settings();
            busy = true; options.IsEnabled = cardPanel.IsEnabled = preview.IsEnabled = apply.IsEnabled = false; status.Text = "Preparing samples…";
            var results = await Task.Run(() => ImageFamilyEdit.Preview(selection, settings));
            if (closed) return;
            prepared = results;
            foreach (var result in prepared)
            {
                var card = cards[result.Source.Name]; card.After.Source = ImageCodec.ToBitmap(result.Image);
                card.Dimensions.Text = $"{result.Source.Image!.Width} × {result.Source.Image.Height} → {result.Image.Width} × {result.Image.Height}";
            }
            var count = prepared.Count(item => item.Changed); apply.Content = "Apply to " + count; apply.IsEnabled = count > 0;
            status.Text = count == 0 ? "No pixel or guide changes." : count + " images will change in one Undo step.";
        }
        catch (Exception exception) { prepared = []; status.Text = exception.Message; }
        finally { busy = false; if (!closed) { options.IsEnabled = cardPanel.IsEnabled = true; preview.IsEnabled = cards.Values.Any(item => item.Check.IsChecked == true); } }
    }
}
