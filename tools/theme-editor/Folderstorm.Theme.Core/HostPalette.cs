// SPDX-License-Identifier: LGPL-2.1-or-later
namespace Folderstorm.Theme;

public sealed record HostPaletteDefinition
{
    public required int FormatVersion { get; init; }
    public required Dictionary<string, string> Colors { get; init; }
}

// Host GDI brushes use opaque RGB, separate from viewer RGBA colors.
public static class HostPalette
{
    public const int Limit = 4096;
    private static readonly string[] names = ["background", "panel", "input", "text", "muted", "accent", "border"];
    public static IReadOnlyList<string> Roles { get; } = Array.AsReadOnly(names);
    public static HostPaletteDefinition Midnight => Create(["#111016", "#1A1921", "#111016", "#F5F3FA", "#ADA9BA", "#BFA4FC", "#736787"]);
    public static HostPaletteDefinition Daylight => Create(["#F7F5FB", "#FFFFFF", "#F7F5FB", "#211A2B", "#61566F", "#6C3FA8", "#897D9D"]);
    private static HostPaletteDefinition Create(string[] colors) => new() { FormatVersion = 1, Colors = names.Zip(colors).ToDictionary(pair => pair.First, pair => pair.Second, StringComparer.Ordinal) };
    public static HostPaletteDefinition Read(byte[] bytes)
    {
        Contract.Require(bytes.Length <= Limit, "size", "Host palette exceeds4 KiB");
        var palette = Contract.Read<HostPaletteDefinition>(bytes); Validate(palette); return palette;
    }
    public static byte[] Bytes(HostPaletteDefinition palette)
    {
        Validate(palette); var bytes = Contract.Bytes(palette);
        Contract.Require(bytes.Length <= Limit, "size", "Host palette exceeds4 KiB"); return bytes;
    }
    public static void Validate(HostPaletteDefinition palette)
    {
        Contract.Require(palette.FormatVersion == 1, "host_palette_version", "Unsupported host palette format");
        Contract.Require(palette.Colors != null && palette.Colors.Count == names.Length && palette.Colors.Keys.All(name => names.Contains(name, StringComparer.Ordinal)),
            "host_palette", "Define the seven host color roles");
        foreach (var value in palette.Colors.Values)
            Contract.Require(value is { Length: 7 } && value[0] == '#' && value[1..].All(char.IsAsciiHexDigit), "host_palette", "Host colors use #RRGGBB");
    }
    public static HostPaletteDefinition WithColor(HostPaletteDefinition palette, string role, string value)
    {
        Validate(palette); Contract.Require(names.Contains(role, StringComparer.Ordinal), "host_palette", "Choose a known host color role");
        var color = ThemeColor.Parse(value); Contract.Require(color.A == 1, "host_palette", "Host colors must be opaque");
        var hex = color.Hex(false);
        if (string.Equals(palette.Colors[role], hex, StringComparison.OrdinalIgnoreCase)) return palette;
        return palette with { Colors = new(palette.Colors, StringComparer.Ordinal) { [role] = hex } };
    }
}
