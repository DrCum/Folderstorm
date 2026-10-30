// SPDX-License-Identifier: LGPL-2.1-or-later
using Folderstorm.Theme;
using System.Text;
using System.Text.Json.Nodes;

internal static class CaptureChecks
{
    public static void Register(Action<string, Action> test, string root, BaseCatalog catalog)
    {
        void Assert(bool value, string message) { if (!value) throw new Exception(message); }
        void Reject(string code, Action action) { try { action(); throw new Exception("Expected " + code); } catch (ThemeException error) { Assert(error.Code == code, error.Message); } }
        var owner = new string('a', 64); var revision = new string('b', 64); var nonce = new string('c', 64);
        var request = new CaptureRequest(1, owner, 7, revision, nonce, 1280, 900);
        test("library capture contracts bind owner, revision, generation, nonce and bounded dimensions", () =>
        {
            Assert(PreviewCapture.Request(Contract.Bytes(request), owner, 7, revision) == request, "Valid request changed");
            Assert(PreviewCapture.Request(Contract.Bytes(request with { Revision = "" }), owner, 7, "").Revision == "", "Base preview capture refused");
            foreach (var changed in new[] { request with { RequestId = new string('d', 64) }, request with { Revision = new string('e', 64) }, request with { Generation = 8 } })
                Reject("capture_ownership", () => PreviewCapture.Request(Contract.Bytes(changed), owner, 7, revision));
            foreach (var changed in new[] { request with { CaptureVersion = 2 }, request with { CaptureId = "../bad" }, request with { MaxWidth = 0 }, request with { MaxHeight = 2049 }, request with { Generation = 0 } })
                Reject("capture_request", () => PreviewCapture.Request(Contract.Bytes(changed), owner, 7, revision));
            Reject("size", () => PreviewCapture.Request(new byte[4097], owner, 7, revision));
            Reject("duplicate_key", () => PreviewCapture.Request(Encoding.UTF8.GetBytes("{\"capture_version\":1,\"capture_version\":1}"), owner, 7, revision));
            var changedJson = JsonNode.Parse(Contract.Bytes(request))!; changedJson["path"] = "C:/outside.png";
            Reject("schema", () => PreviewCapture.Request(Encoding.UTF8.GetBytes(changedJson.ToJsonString()), owner, 7, revision));
            Reject("capture_request", () => PreviewCapture.FileName("../../outside"));
            var fixtures = Path.Combine(root, "capture-fixtures"); Directory.CreateDirectory(fixtures);
            var cases = new List<(string Name, CaptureRequest Request, bool Accepted)> { ("valid", request, true), ("base", request with { Revision = "" }, true), ("wide", request with { MaxWidth = 2048, MaxHeight = 2048 }, true), ("owner", request with { RequestId = new string('d', 64) }, false), ("revision", request with { Revision = new string('d', 64) }, false), ("generation", request with { Generation = 8 }, false), ("negative", request with { MaxWidth = -1 }, false), ("oversize", request with { MaxHeight = 2049 }, false), ("nonce-path", request with { CaptureId = "../../bad" }, false), ("version", request with { CaptureVersion = 2 }, false), ("uppercase", request with { CaptureId = nonce.ToUpperInvariant() }, false) };
            foreach (var item in cases) Contract.AtomicWrite(Path.Combine(fixtures, item.Name + ".json"), Contract.Bytes(new { RequestId = owner, Generation = 7, Revision = item.Name == "base" ? "" : revision, Request = item.Request, item.Accepted }));
            Contract.AtomicWrite(Path.Combine(root, "capture-fixtures.json"), Contract.Bytes(new { Directory = fixtures, Cases = cases.Count }));
        });
        test("library capture receipt rejects stale ownership, altered pixels, paths, sizes and missing capability", () =>
        {
            var source = catalog.Textures.Values.First(texture => texture.Source?.EndsWith(".png", StringComparison.OrdinalIgnoreCase) == true).Source!;
            var png = File.ReadAllBytes(Path.Combine(catalog.Root, source));
            var width = (int)System.Buffers.Binary.BinaryPrimitives.ReadUInt32BigEndian(png.AsSpan(16, 4)); var height = (int)System.Buffers.Binary.BinaryPrimitives.ReadUInt32BigEndian(png.AsSpan(20, 4));
            var receipt = new JsonObject { ["capture_version"] = 1, ["request_id"] = owner, ["generation"] = 7, ["revision"] = revision, ["capture_id"] = nonce, ["pid"] = 123, ["state"] = "captured", ["diagnostic"] = "", ["image"] = PreviewCapture.FileName(nonce), ["sha256"] = Contract.Hash(png), ["width"] = width, ["height"] = height, ["source_width"] = 1280, ["source_height"] = 900 };
            byte[] Bytes(JsonObject value) => Encoding.UTF8.GetBytes(value.ToJsonString());
            Assert(PreviewCapture.Response(Bytes(receipt), request, 123, png).Png.SequenceEqual(png), "Valid capture bytes changed");
            foreach (var key in new[] { "request_id", "revision", "capture_id" }) { var changed = (JsonObject)receipt.DeepClone(); changed[key] = new string('d', 64); Reject("capture_ownership", () => PreviewCapture.Response(Bytes(changed), request, 123, png)); }
            foreach (var key in new[] { "pid", "generation" }) { var changed = (JsonObject)receipt.DeepClone(); changed[key] = 999; Reject("capture_ownership", () => PreviewCapture.Response(Bytes(changed), request, 123, png)); }
            var image = (JsonObject)receipt.DeepClone(); image["image"] = "../outside.png"; Reject("integrity", () => PreviewCapture.Response(Bytes(image), request, 123, png));
            var corrupt = png.ToArray(); corrupt[^1] ^= 1; Reject("integrity", () => PreviewCapture.Response(Bytes(receipt), request, 123, corrupt));
            var dimensions = (JsonObject)receipt.DeepClone(); dimensions["width"] = width + 1; Reject("image", () => PreviewCapture.Response(Bytes(dimensions), request, 123, png));
            var failed = (JsonObject)receipt.DeepClone(); failed["state"] = "failed"; failed["diagnostic"] = "Could not capture"; Reject("capture", () => PreviewCapture.Response(Bytes(failed), request, 123, []));
            var caps = Contract.Bytes(new { PreviewVersion = 1, SourceCommit = new string('f', 40), XuiRulesSha256 = XuiPolicy.Fingerprint });
            PreviewSession.ValidateCapabilities(caps, true); Reject("preview_unsupported", () => PreviewSession.ValidateCapabilities(caps, true, true));
            PreviewSession.ValidateCapabilities(Contract.Bytes(new { PreviewVersion = 1, PreviewCaptureVersion = 1, SourceCommit = new string('f', 40), XuiRulesSha256 = XuiPolicy.Fingerprint }), true, true);
        });
    }
}
