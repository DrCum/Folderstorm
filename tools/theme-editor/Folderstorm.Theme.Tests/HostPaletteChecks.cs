// SPDX-License-Identifier: LGPL-2.1-or-later
using Folderstorm.Theme;
using System.Text;

internal static class HostPaletteChecks
{
    public static void Register(Action<string, Action> test)
    {
        void Assert(bool value, string message) { if (!value) throw new Exception(message); }
        void Reject(string code, Action action) { try { action(); throw new Exception("Expected " + code); } catch (ThemeException error) { Assert(error.Code == code, error.Message); } }
        test("host palette retains the seven production Midnight and Daylight RGB roles", () =>
        {
            foreach (var palette in new[] { HostPalette.Midnight, HostPalette.Daylight })
            {
                var bytes = HostPalette.Bytes(palette); var copy = HostPalette.Read(bytes);
                Assert(copy.Colors.Count == 7 && HostPalette.Roles.All(copy.Colors.ContainsKey) && HostPalette.Bytes(copy).SequenceEqual(bytes), "Host role/serialization round trip failed");
            }
            Assert(HostPalette.Midnight.Colors["background"] == "#111016" && HostPalette.Midnight.Colors["border"] == "#736787" && HostPalette.Daylight.Colors["text"] == "#211A2B" && HostPalette.Daylight.Colors["accent"] == "#6C3FA8", "Host production defaults drifted");
            var other = HostPalette.Midnight; other.Colors["background"] = "#000000";
            Assert(HostPalette.Midnight.Colors["background"] == "#111016", "A caller mutated shared defaults");
        });
        test("host palette rejects unknown versions, roles, fields, duplicate keys, alpha and oversized JSON", () =>
        {
            var palette = HostPalette.Midnight; var text = Encoding.UTF8.GetString(HostPalette.Bytes(palette));
            Reject("host_palette_version", () => HostPalette.Read(Encoding.UTF8.GetBytes(text.Replace("\"format_version\": 1", "\"format_version\": 2", StringComparison.Ordinal))));
            Reject("host_palette", () => HostPalette.Read(Encoding.UTF8.GetBytes(text.Replace("\"border\"", "\"unknown\"", StringComparison.Ordinal))));
            Reject("host_palette", () => HostPalette.Read(Encoding.UTF8.GetBytes(text.Replace("#111016", "#111016FF", StringComparison.Ordinal))));
            Reject("schema", () => HostPalette.Read(Encoding.UTF8.GetBytes(text.Replace("\"format_version\": 1", "\"extra\": true, \"format_version\": 1", StringComparison.Ordinal))));
            Reject("duplicate_key", () => HostPalette.Read(Encoding.UTF8.GetBytes(text.Replace("\"border\":", "\"border\": \"#000000\", \"border\":", StringComparison.Ordinal))));
            Reject("host_palette", () => HostPalette.Validate(palette with { Colors = new(palette.Colors, StringComparer.Ordinal) { ["text"] = "#１２３４５６" } }));
            Reject("host_palette", () => HostPalette.Validate(palette with { Colors = new Dictionary<string, string>(StringComparer.Ordinal) }));
            Reject("size", () => HostPalette.Read(Encoding.UTF8.GetBytes(new string(' ', HostPalette.Limit + 1))));
        });
        test("host palette editing preserves source, normalizes opaque picker input and makes equivalent input a no-op", () =>
        {
            var palette = HostPalette.Midnight; var before = HostPalette.Bytes(palette);
            var edited = HostPalette.WithColor(palette, "accent", "#64D8B2FF");
            Assert(edited.Colors["accent"] == "#64D8B2" && HostPalette.Bytes(palette).SequenceEqual(before) && edited.Colors["panel"] == palette.Colors["panel"], "Host edit changed source or unrelated roles");
            Assert(ReferenceEquals(edited, HostPalette.WithColor(edited, "accent", "#64d8b2")), "Equivalent input created a replacement");
            Reject("host_palette", () => HostPalette.WithColor(palette, "accent", "#12345680"));
            Reject("host_palette", () => HostPalette.WithColor(palette, "unknown", "#123456"));
            Assert(HostPalette.Bytes(palette).SequenceEqual(before), "Rejected edit changed source");
        });
    }
}
