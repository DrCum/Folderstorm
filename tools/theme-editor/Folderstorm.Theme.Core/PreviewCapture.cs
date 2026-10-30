// SPDX-License-Identifier: LGPL-2.1-or-later
using System.Buffers.Binary;

namespace Folderstorm.Theme;

public sealed record CaptureRequest(int CaptureVersion, string RequestId, long Generation, string Revision, string CaptureId, int MaxWidth, int MaxHeight);
public sealed record CaptureResult(byte[] Png, int Width, int Height, int SourceWidth, int SourceHeight, string Revision, string CaptureId);

public static class PreviewCapture
{
    public static CaptureRequest Request(byte[] bytes, string requestId, long generation, string revision)
    {
        Contract.Require(bytes.Length <= 4096, "size", "Capture request exceeds 4096 bytes");
        var request = Contract.Read<CaptureRequest>(bytes);
        Contract.Require(request.CaptureVersion == 1 && request.RequestId != null && Contract.IsHash(request.RequestId) && request.CaptureId != null && Contract.IsHash(request.CaptureId) &&
            request.Generation > 0 && request.Revision != null && (request.Revision.Length == 0 || Contract.IsHash(request.Revision)) &&
            request.MaxWidth is > 0 and <= 2048 && request.MaxHeight is > 0 and <= 2048, "capture_request", "Invalid preview capture request");
        Contract.Require(request.RequestId == requestId && request.Generation == generation && request.Revision == revision, "capture_ownership", "Capture belongs to a different preview");
        return request;
    }
    public static CaptureResult Response(byte[] bytes, CaptureRequest request, int pid, byte[] png)
    {
        Contract.Require(bytes.Length <= 8192, "size", "Capture response exceeds 8192 bytes");
        using var status = Contract.Parse(bytes); var root = status.RootElement;
        Contract.Require(root.GetProperty("capture_version").GetInt32() == 1 && root.GetProperty("request_id").GetString() == request.RequestId &&
            root.GetProperty("generation").GetInt64() == request.Generation && root.GetProperty("revision").GetString() == request.Revision &&
            root.GetProperty("capture_id").GetString() == request.CaptureId && root.GetProperty("pid").GetInt32() == pid, "capture_ownership", "Capture response does not match the owned preview");
        Contract.Require(root.GetProperty("state").GetString() == "captured", "capture", root.GetProperty("diagnostic").GetString() ?? "Preview capture failed");
        Contract.Require(root.GetProperty("image").GetString() == FileName(request.CaptureId) && png.Length <= 32 * 1024 * 1024 &&
            root.GetProperty("sha256").GetString() == Contract.Hash(png), "integrity", "Captured PNG differs from its receipt");
        PackageStore.ValidateImage(png, "preview.png");
        var width = checked((int)BinaryPrimitives.ReadUInt32BigEndian(png.AsSpan(16, 4))); var height = checked((int)BinaryPrimitives.ReadUInt32BigEndian(png.AsSpan(20, 4)));
        var sourceWidth = root.GetProperty("source_width").GetInt32(); var sourceHeight = root.GetProperty("source_height").GetInt32();
        Contract.Require(width == root.GetProperty("width").GetInt32() && height == root.GetProperty("height").GetInt32() && width <= request.MaxWidth && height <= request.MaxHeight &&
            sourceWidth is > 0 and <= 16384 && sourceHeight is > 0 and <= 16384 && (long)sourceWidth * sourceHeight <= 16000000, "image", "Invalid capture dimensions");
        return new(png, width, height, sourceWidth, sourceHeight, request.Revision, request.CaptureId);
    }
    public static string FileName(string captureId)
    {
        Contract.Require(Contract.IsHash(captureId), "capture_request", "Invalid capture ID"); return "capture-" + captureId + ".png";
    }
}
