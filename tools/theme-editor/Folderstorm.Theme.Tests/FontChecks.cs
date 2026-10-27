// SPDX-License-Identifier: LGPL-2.1-or-later
using Folderstorm.Theme;
using System.Buffers.Binary;
using System.Text;
using System.Xml.Linq;

internal static class FontChecks
{
    public static void Register(Action<string, Action> test, string root, BaseCatalog catalog)
    {
        void Assert(bool condition, string message) { if (!condition) throw new Exception(message); }
        void Reject(Action action) { try { action(); throw new Exception("Expected font rejection"); } catch (ThemeException) { } }
        var font = File.ReadAllBytes(Path.Combine(catalog.Root, "fonts", "InterVariableFont.ttf"));
        test("font metadata reads variable axes, license and Unicode glyph mapping", () =>
        {
            var faces = FontInspection.Read(font); var face = faces.Single();
            Assert(face.Family.Contains("Inter", StringComparison.Ordinal) && face.Axes.Any(axis => axis.Tag == "wght" && axis.Minimum <= 400 && axis.Maximum >= 700), "Variable font metadata");
            Assert(face.License.Length > 0 && face.Characters.Supported && face.Characters.HasGlyph('A') && face.Characters.HasGlyph('Я') && !face.Characters.HasGlyph(0x10ffff), "License or cmap mapping");
            foreach (var length in new[] { 0, 11, 24, font.Length / 2 }) Reject(() => FontInspection.Read(font.AsSpan(0, length).ToArray()));
            var bad = (byte[])font.Clone(); BinaryPrimitives.WriteUInt32BigEndian(bad.AsSpan(20, 4), uint.MaxValue); Reject(() => FontInspection.Read(bad));
        });
        test("font collections retain all face indexes and shared-table offsets", () =>
        {
            var count = BinaryPrimitives.ReadUInt16BigEndian(font.AsSpan(4, 2)); var collection = new byte[font.Length + 20]; font.CopyTo(collection, 20);
            Encoding.ASCII.GetBytes("ttcf").CopyTo(collection, 0); BinaryPrimitives.WriteUInt32BigEndian(collection.AsSpan(4, 4), 0x10000);
            BinaryPrimitives.WriteUInt32BigEndian(collection.AsSpan(8, 4), 2); BinaryPrimitives.WriteUInt32BigEndian(collection.AsSpan(12, 4), 20); BinaryPrimitives.WriteUInt32BigEndian(collection.AsSpan(16, 4), 20);
            for (var index = 0; index < count; ++index) { var position = 20 + 12 + index * 16 + 8; BinaryPrimitives.WriteUInt32BigEndian(collection.AsSpan(position, 4), BinaryPrimitives.ReadUInt32BigEndian(collection.AsSpan(position, 4)) + 20); }
            var faces = FontInspection.Read(collection); Assert(faces.Length == 2 && faces[1].Index == 1 && faces.All(face => face.Characters.HasGlyph('A')), "Collection faces or global offsets lost");
            BinaryPrimitives.WriteUInt32BigEndian(collection.AsSpan(16, 4), uint.MaxValue); Reject(() => FontInspection.Read(collection));
            const int header = 12 + 128 * 4; var largeMetadata = new byte[header + font.Length + 1024 * 1024]; font.CopyTo(largeMetadata, header);
            Encoding.ASCII.GetBytes("ttcf").CopyTo(largeMetadata, 0); BinaryPrimitives.WriteUInt32BigEndian(largeMetadata.AsSpan(4, 4), 0x10000); BinaryPrimitives.WriteUInt32BigEndian(largeMetadata.AsSpan(8, 4), 128);
            for (var index = 0; index < 128; ++index) BinaryPrimitives.WriteUInt32BigEndian(largeMetadata.AsSpan(12 + index * 4, 4), header);
            for (var index = 0; index < count; ++index)
            {
                var record = header + 12 + index * 16; var position = record + 8;
                BinaryPrimitives.WriteUInt32BigEndian(largeMetadata.AsSpan(position, 4), BinaryPrimitives.ReadUInt32BigEndian(largeMetadata.AsSpan(position, 4)) + header);
                if (Encoding.ASCII.GetString(largeMetadata, record, 4) == "cmap") BinaryPrimitives.WriteUInt32BigEndian(largeMetadata.AsSpan(record + 12, 4), 1024 * 1024);
            }
            try { FontInspection.Read(largeMetadata); throw new Exception("Collection metadata budget not enforced"); } catch (ThemeException error) { Assert(error.Code == "font_data" && error.Message.Contains("metadata", StringComparison.Ordinal), "Unexpected collection-budget rejection"); }
        });
        test("font reference operations preserve OS groups, styles and native-only attributes", () =>
        {
            var source = Encoding.UTF8.GetBytes("<fonts><font name='SansSerif' font_style='BOLD' comment='keep'><file functor='is_emoji_use_color'>InterVariableFont.ttf</file><file>DejaVuSans.ttf</file><os name='Windows'><file>arial.ttf</file></os><os name='Mac'><file>Helvetica.dfont</file></os></font><font_size name='Small' size='9'/></fonts>");
            var roles = FontProject.Roles(source); Assert(roles.Single().Style == "BOLD", "Role style flattened");
            var edited = FontProject.SetReference(source, 0, 0, new("InterVariableFont.ttf", .5, 650, true, "no_hinting", true));
            var xml = Contract.Xml(edited); var selected = FontProject.References(edited, 0)[0];
            Assert(selected.Weight == 650 && selected.Collection && selected.Functor == "is_emoji_use_color" && selected.Hinting == "no_hinting" && xml.Descendants("os").Last().Value == "Helvetica.dfont" && (string?)xml.Descendants("font").Single().Attribute("comment") == "keep", "Font edit lost untouched data");
            var moved = FontProject.MoveReference(edited, 0, 0, 1); Assert(FontProject.References(moved, 0)[1].File == "InterVariableFont.ttf", "Fallback order unchanged");
            Reject(() => FontProject.MoveReference(source, 0, 1, 1)); Reject(() => FontProject.SetReference(source, 0, 0, new("../bad.ttf"))); Reject(() => FontProject.SetReference(source, 0, 0, new("InterVariableFont.ttf", Weight: 1001)));
            Assert(FontProject.Sizes(FontProject.SetSize(source, "Small", 13.5))["Small"] == 13.5, "Named size edit failed"); Reject(() => FontProject.SetSize(source, "Small", double.NaN));
            var malformed = Contract.Xml(source); malformed.Descendants("file").First().SetAttributeValue("size_delta", "bad"); Reject(() => FontProject.References(Contract.XmlBytes(malformed), 0));
        });
        test("font edits validate before one checkpoint, survive save and reset with Undo", () =>
        {
            var directory = Path.Combine(root, "font-project"); var project = ProjectStore.Create(catalog, "Font tools"); project.Edits.Colors["ThemeAccent"] = "1 0 0 1";
            var baseBytes = FontProject.Read(directory, project, catalog); var role = FontProject.Roles(baseBytes).First(role => role.Name == "SansSerif" && role.Style.Length == 0);
            var edited = FontProject.SetReference(baseBytes, role.Index, 0, new("InterVariableFont.ttf", .5, 650));
            Assert(FontProject.Apply(directory, project, catalog, edited, "Font weight") && project.Undo.Count == 1, "Font transaction did not commit once");
            Assert(!FontProject.Apply(directory, project, catalog, edited, "No-op") && project.Undo.Count == 1, "Font no-op added history");
            var repeated = FontProject.SetReference(FontProject.Read(directory, project, catalog), role.Index, 0, new("InterVariableFont.ttf", .5, 650));
            Assert(!FontProject.Apply(directory, project, catalog, repeated, "Repeated reference") && project.Undo.Count == 1, "Re-serialized unchanged font reference added history");
            var before = Contract.Bytes(project); var bad = Contract.Xml(edited); bad.Root!.Add(new XElement("callback", "bad")); Reject(() => FontProject.Apply(directory, project, catalog, Contract.XmlBytes(bad), "Invalid"));
            Assert(Contract.Bytes(project).SequenceEqual(before), "Invalid font edit changed project/history");
            ProjectStore.Save(directory, project); var reopened = ProjectStore.Open(directory); Assert(FontProject.Read(directory, reopened, catalog).SequenceEqual(edited), "Saved font config changed");
            Assert(FontProject.Reset(reopened) && reopened.Edits.Colors.Count == 1 && reopened.Edits.Assets.Count == 0 && reopened.UndoEdit() && FontProject.Read(directory, reopened, catalog).SequenceEqual(edited), "Reset/Undo lost other edits or config");
            PackageStore.Compile(directory, reopened, catalog);
        });
        test("font embedding uses content-scoped filenames and carries license without flattening other roles", () =>
        {
            var directory = Path.Combine(root, "font-embed"); var project = ProjectStore.Create(catalog, "Embedded font"); var source = FontProject.Read(directory, project, catalog);
            var role = FontProject.Roles(source).First(role => role.Name == "Monospace"); var sans = FontProject.Roles(source).First(role => role.Name == "SansSerif" && role.Style.Length == 0);
            var beforeSans = FontProject.References(source, sans.Index);
            Assert(FontProject.Embed(directory, project, catalog, role.Index, ".TTF", font, "Test license receipt"), "Font embed failed");
            var first = FontProject.References(FontProject.Read(directory, project, catalog), role.Index)[0];
            Assert(first.File.StartsWith("font-", StringComparison.Ordinal) && project.Undo.Count == 1 && beforeSans.SequenceEqual(FontProject.References(FontProject.Read(directory, project, catalog), sans.Index)), "Embedding altered unrelated role or history");
            var theme = PackageStore.Compile(directory, project, catalog); Assert(theme.Files["viewer/fonts/" + first.File].SequenceEqual(font) && theme.Files.Keys.Any(path => path.StartsWith("licenses/font-", StringComparison.Ordinal)), "Font/license export missing");
            var library = ThemeLibrary.Publish(theme, Path.Combine(root, "font-library"));
            Contract.AtomicWrite(Path.Combine(directory, "result.json"), Contract.Bytes(new { revision = theme.Revision, path = library }));
            Assert(project.UndoEdit() && project.Edits.Assets.Count == 0 && project.RedoEdit() && FontProject.Reset(project) && project.Edits.Assets.Count == 0, "Font embedding Undo/Redo/reset failed");
        });
    }
}
