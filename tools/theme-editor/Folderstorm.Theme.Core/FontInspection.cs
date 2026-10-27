// SPDX-License-Identifier: LGPL-2.1-or-later
using System.Buffers.Binary;
using System.Text;

namespace Folderstorm.Theme;

public sealed record FontAxis(string Tag, double Minimum, double Default, double Maximum);
public sealed record FontFaceInfo(int Index, string Family, string Style, string Copyright, string License, string LicenseUrl,
    int EmbeddingFlags, FontAxis[] Axes, FontCharacterMap Characters)
{
    public string Label => Index + ": " + Family + " · " + Style;
    public string EmbeddingNotice => EmbeddingFlags < 0 ? "Embedding flags unavailable. Check the font license."
        : (EmbeddingFlags & 2) != 0 ? "Restricted embedding flag. Check the font license before sharing."
        : (EmbeddingFlags & 0x200) != 0 ? "Bitmap-only embedding flag. Check the font license before sharing."
        : "Embedding flags: 0x" + EmbeddingFlags.ToString("X4") + ". Redistribution rights come from the font license.";
}

// Reads metadata and Unicode mappings only. The native viewer still loads and renders fonts with FreeType.
public static class FontInspection
{
    internal static void Bounds(byte[] bytes, long offset, long length)
        => Contract.Require(offset >= 0 && length >= 0 && offset <= bytes.LongLength - length, "font_data", "Font table is truncated or out of range");
    internal static int U16(byte[] bytes, int offset) { Bounds(bytes, offset, 2); return BinaryPrimitives.ReadUInt16BigEndian(bytes.AsSpan(offset, 2)); }
    internal static uint U32(byte[] bytes, int offset) { Bounds(bytes, offset, 4); return BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(offset, 4)); }
    private static double Fixed(byte[] bytes, int offset) { Bounds(bytes, offset, 4); return BinaryPrimitives.ReadInt32BigEndian(bytes.AsSpan(offset, 4)) / 65536d; }
    public static FontFaceInfo[] Read(byte[] bytes)
    {
        Contract.Require(bytes.Length is >= 12 and <= 64 * 1024 * 1024, "font_data", "Use a font up to64 MiB");
        var collection = U32(bytes, 0) == 0x74746366;
        var count = collection ? U32(bytes, 8) : 1u;
        Contract.Require(count is > 0 and <= 128, "font_data", "Font collection exceeds128 faces");
        if (collection) Bounds(bytes, 12, count * 4L);
        var faces = new List<FontFaceInfo>(); long metadataBytes = 0;
        for (var index = 0; index < count; ++index)
        {
            var offset = collection ? (long)U32(bytes, 12 + index * 4) : 0;
            Bounds(bytes, offset, 12); var start = (int)offset;
            Contract.Require(U32(bytes, start) is 0x00010000 or 0x4f54544f or 0x74727565, "font_data", "Unsupported OpenType face signature");
            var tableCount = U16(bytes, start + 4); Contract.Require(tableCount is > 0 and <= 256, "font_data", "Invalid font table count");
            Bounds(bytes, start + 12L, tableCount * 16L); var tables = new Dictionary<string, byte[]>(StringComparer.Ordinal);
            for (var entry = 0; entry < tableCount; ++entry)
            {
                var record = start + 12 + entry * 16; var tag = Encoding.ASCII.GetString(bytes, record, 4);
                var tableOffset = U32(bytes, record + 8); var length = U32(bytes, record + 12); Bounds(bytes, tableOffset, length);
                Contract.Require(!tables.ContainsKey(tag), "font_data", "Duplicate font table: " + tag);
                // Only retain the tables used by this inspection; collection outlines can be large and shared.
                var inspect = tag is "name" or "fvar" or "OS/2" or "cmap" or "maxp";
                if (inspect) { metadataBytes += length * (tag == "cmap" ? 2L : 1L); Contract.Require(metadataBytes <= 128L * 1024 * 1024, "font_data", "Font collection metadata exceeds128 MiB"); }
                tables.Add(tag, inspect ? bytes.AsSpan((int)tableOffset, (int)length).ToArray() : []);
            }
            var names = tables.TryGetValue("name", out var nameBytes) ? Names(nameBytes) : new Dictionary<int, string>();
            var flags = tables.TryGetValue("OS/2", out var os2) && os2.Length >= 10 ? U16(os2, 8) : -1;
            var glyphCount = tables.TryGetValue("maxp", out var maxp) && maxp.Length >= 6 ? U16(maxp, 4) : 65536;
            faces.Add(new(index, names.GetValueOrDefault(16, names.GetValueOrDefault(1, "Unnamed face")), names.GetValueOrDefault(17, names.GetValueOrDefault(2, "Regular")),
                names.GetValueOrDefault(0, ""), names.GetValueOrDefault(13, ""), names.GetValueOrDefault(14, ""), flags,
                tables.TryGetValue("fvar", out var fvar) ? Axes(fvar) : [], new(tables.GetValueOrDefault("cmap", []), glyphCount)));
        }
        return faces.ToArray();
    }
    private static Dictionary<int, string> Names(byte[] table)
    {
        var count = U16(table, 2); var storage = U16(table, 4); Bounds(table, 6, count * 12L);
        Contract.Require(count <= 4096, "font_data", "Too many font names");
        var result = new Dictionary<int, (int Rank, string Text)>();
        for (var index = 0; index < count; ++index)
        {
            var record = 6 + index * 12; var platform = U16(table, record); var encoding = U16(table, record + 2); var language = U16(table, record + 4);
            var id = U16(table, record + 6); var length = U16(table, record + 8); var offset = storage + U16(table, record + 10); Bounds(table, offset, length);
            if (id is not (0 or 1 or 2 or 13 or 14 or 16 or 17) || !(platform == 0 || platform == 3 && encoding is 0 or 1 or 10)) continue;
            var rank = platform == 3 && language == 0x409 ? 3 : platform == 0 ? 2 : 1;
            if (result.TryGetValue(id, out var preferred) && preferred.Rank >= rank) continue;
            Contract.Require(length % 2 == 0, "font_data", "Invalid Unicode font name");
            var text = Encoding.BigEndianUnicode.GetString(table, offset, length).Replace("\0", "").Trim();
            if (text.Length > 0 && (!result.TryGetValue(id, out var current) || rank > current.Rank)) result[id] = (rank, text);
        }
        return result.ToDictionary(item => item.Key, item => item.Value.Text);
    }
    private static FontAxis[] Axes(byte[] table)
    {
        Contract.Require(U16(table, 0) == 1, "font_data", "Unsupported variable font table");
        var offset = U16(table, 4); var count = U16(table, 8); var size = U16(table, 10);
        Contract.Require(count <= 64 && size >= 20, "font_data", "Invalid variable font axes"); Bounds(table, offset, count * (long)size);
        var result = new List<FontAxis>();
        for (var index = 0; index < count; ++index)
        {
            var record = offset + index * size; var tag = Encoding.ASCII.GetString(table, record, 4);
            var min = Fixed(table, record + 4); var normal = Fixed(table, record + 8); var max = Fixed(table, record + 12);
            Contract.Require(min <= normal && normal <= max && result.All(axis => axis.Tag != tag), "font_data", "Invalid variable font range");
            result.Add(new(tag, min, normal, max));
        }
        return result.ToArray();
    }
}

public sealed class FontCharacterMap
{
    private readonly byte[] table;
    private readonly int format, glyphCount;
    public bool Supported => format != 0;
    internal FontCharacterMap(byte[] cmap, int glyphCount)
    {
        this.glyphCount = glyphCount; table = [];
        if (cmap.Length == 0) return;
        var count = FontInspection.U16(cmap, 2); FontInspection.Bounds(cmap, 4, count * 8L);
        Contract.Require(count <= 1024, "font_data", "Too many character maps"); var rank = 0;
        for (var index = 0; index < count; ++index)
        {
            var record = 4 + index * 8; var platform = FontInspection.U16(cmap, record); var encoding = FontInspection.U16(cmap, record + 2);
            if (!(platform == 0 || platform == 3 && encoding is 1 or 10)) continue;
            var offset = (long)FontInspection.U32(cmap, record + 4); FontInspection.Bounds(cmap, offset, 2);
            var candidate = FontInspection.U16(cmap, (int)offset); var priority = candidate switch { 12 => 3, 4 => 2, 13 => 1, _ => 0 };
            if (priority <= rank) continue;
            var length = candidate == 4 ? FontInspection.U16(cmap, (int)offset + 2) : (long)FontInspection.U32(cmap, (int)offset + 4);
            FontInspection.Bounds(cmap, offset, length); table = cmap.AsSpan((int)offset, (int)length).ToArray(); format = candidate; rank = priority;
        }
        if (format == 4)
        {
            var segments = FontInspection.U16(table, 6); Contract.Require(segments > 0 && segments % 2 == 0, "font_data", "Invalid character segments");
            FontInspection.Bounds(table, 14, segments * 4L + 2);
            var count4 = segments / 2; var previous = -1;
            for (var index = 0; index < count4; ++index) { var end = FontInspection.U16(table, 14 + index * 2); var start = FontInspection.U16(table, 16 + count4 * 2 + index * 2); Contract.Require(start <= end && start > previous, "font_data", "Unordered character segments"); previous = end; }
        }
        else if (format is 12 or 13)
        {
            var groups = FontInspection.U32(table, 12); FontInspection.Bounds(table, 16, groups * 12L); long previous = -1;
            for (var index = 0; index < groups; ++index) { var record = 16 + index * 12; var start = FontInspection.U32(table, record); var end = FontInspection.U32(table, record + 4); Contract.Require(start <= end && end <= 0x10ffff && start > previous, "font_data", "Unordered character groups"); previous = end; }
        }
    }
    public bool HasGlyph(int scalar)
    {
        if (scalar < 0 || scalar > 0x10ffff || scalar is >= 0xd800 and <= 0xdfff) return false;
        ulong glyph = 0;
        if (format == 4 && scalar <= 0xffff)
        {
            var count = FontInspection.U16(table, 6) / 2;
            for (var index = 0; index < count; ++index)
            {
                if (scalar > FontInspection.U16(table, 14 + index * 2)) continue;
                var start = FontInspection.U16(table, 16 + count * 2 + index * 2); if (scalar < start) return false;
                var delta = FontInspection.U16(table, 16 + count * 4 + index * 2); var rangeAt = 16 + count * 6 + index * 2; var range = FontInspection.U16(table, rangeAt);
                glyph = range == 0 ? (uint)((scalar + delta) & 0xffff) : (uint)FontInspection.U16(table, rangeAt + range + (scalar - start) * 2);
                if (range != 0 && glyph != 0) glyph = (glyph + (uint)delta) & 0xffff; break;
            }
        }
        else if (format is 12 or 13)
        {
            var low = 0; var high = (int)FontInspection.U32(table, 12) - 1;
            while (low <= high)
            {
                var middle = low + (high - low) / 2; var record = 16 + middle * 12; var start = FontInspection.U32(table, record); var end = FontInspection.U32(table, record + 4);
                if (scalar < start) high = middle - 1; else if (scalar > end) low = middle + 1;
                else { glyph = FontInspection.U32(table, record + 8); if (format == 12) glyph += (uint)scalar - start; break; }
            }
        }
        return glyph > 0 && glyph < (ulong)glyphCount;
    }
}
