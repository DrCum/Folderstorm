// SPDX-License-Identifier: LGPL-2.1-or-later
using System.Text.Json;

namespace Folderstorm.Theme;

public enum PixelTool { Inspect, Pen, Line, Circle }
public readonly record struct PixelPoint(int X, int Y);

public sealed partial class RasterImage
{
    public bool PixelEquals(RasterImage other) => Width == other.Width && Height == other.Height && pixels.AsSpan().SequenceEqual(other.pixels);

    public RasterImage Draw(PixelTool tool, IReadOnlyList<PixelPoint> points, ThemeColor color, int size = 1)
    {
        Contract.Require(tool is PixelTool.Pen or PixelTool.Line or PixelTool.Circle && size is >= 1 and <= 16,
            "drawing", "Choose pen, line or circle with a 1–16 pixel brush");
        Contract.Require(points.Count is >= 1 and <= PixelStroke.PointLimit && (tool == PixelTool.Pen || points.Count == 2) &&
            points.All(point => point.X >= 0 && point.Y >= 0 && point.X < Width && point.Y < Height), "drawing", "Drawing points must fit inside the image");
        var output = ToRgba(); var before = (size - 1) / 2;
        void Stamp(int cx, int cy)
        {
            for (var y = Math.Max(0, cy - before); y < Math.Min(Height, cy - before + size); ++y)
                for (var x = Math.Max(0, cx - before); x < Math.Min(Width, cx - before + size); ++x)
                    Write(output, (y * Width + x) * 4, color);
        }
        void Line(PixelPoint first, PixelPoint last)
        {
            var x = first.X; var y = first.Y; var dx = Math.Abs(last.X - x); var dy = -Math.Abs(last.Y - y);
            var sx = x < last.X ? 1 : -1; var sy = y < last.Y ? 1 : -1; var error = dx + dy;
            while (true)
            {
                Stamp(x, y); if (x == last.X && y == last.Y) break;
                var twice = 2 * error;
                if (twice >= dy) { error += dy; x += sx; }
                if (twice <= dx) { error += dx; y += sy; }
            }
        }
        if (tool == PixelTool.Circle)
        {
            var center = points[0]; var edge = points[1]; var dx = edge.X - center.X; var dy = edge.Y - center.Y;
            var radius = (int)Math.Round(Math.Sqrt((long)dx * dx + (long)dy * dy), MidpointRounding.AwayFromZero);
            var x = radius; var y = 0; var error = 1 - radius;
            while (x >= y)
            {
                Stamp(center.X + x, center.Y + y); Stamp(center.X + y, center.Y + x);
                Stamp(center.X - x, center.Y + y); Stamp(center.X - y, center.Y + x);
                Stamp(center.X + x, center.Y - y); Stamp(center.X + y, center.Y - x);
                Stamp(center.X - x, center.Y - y); Stamp(center.X - y, center.Y - x);
                ++y;
                if (error < 0) error += 2 * y + 1;
                else { --x; error += 2 * (y - x) + 1; }
            }
        }
        else
        {
            Stamp(points[0].X, points[0].Y);
            for (var index = 1; index < points.Count; ++index) Line(points[index - 1], points[index]);
        }
        return new(Width, Height, output);
    }
}

// A gesture previews pixels without touching the project. The caller commits once on release.
public sealed class PixelStroke
{
    public const int PointLimit = 4096;
    private readonly List<PixelPoint> points;
    public RasterImage Original { get; }
    public RasterImage Preview { get; private set; }
    public PixelTool Tool { get; }
    public ThemeColor Color { get; }
    public int Size { get; }
    public bool HasChanges => !Preview.PixelEquals(Original);
    public PixelStroke(RasterImage original, PixelTool tool, PixelPoint start, ThemeColor color, int size = 1)
    {
        Original = original; Tool = tool; Color = color; Size = size;
        points = tool == PixelTool.Pen ? [start] : [start, start];
        Preview = original.Draw(tool, points, color, size);
    }
    public void Move(PixelPoint point)
    {
        if (point == points[^1]) return;
        if (Tool == PixelTool.Pen)
        {
            Contract.Require(points.Count < PointLimit, "drawing", "Stroke limit reached. Start a new stroke.");
            var next = Preview.Draw(PixelTool.Line, [points[^1], point], Color, Size);
            points.Add(point); Preview = next;
        }
        else
        {
            var next = Original.Draw(Tool, [points[0], point], Color, Size);
            points[1] = point; Preview = next;
        }
    }
    public Recipe Recipe => new("pixel-draw", new(StringComparer.Ordinal)
    {
        ["tool"] = JsonSerializer.SerializeToElement(Tool.ToString().ToLowerInvariant()),
        ["color"] = JsonSerializer.SerializeToElement(Color.Hex()), ["size"] = JsonSerializer.SerializeToElement(Size),
        ["points"] = JsonSerializer.SerializeToElement(points, Contract.JsonOptions)
    });
}
