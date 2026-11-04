// SPDX-License-Identifier: LGPL-2.1-or-later
using Folderstorm.Theme;
using System.Text;
using System.Text.Json.Nodes;

internal static class HostPreviewChecks
{
    public static void Register(Action<string, Action> test, string root, BaseCatalog catalog)
    {
        static void Assert(bool value, string message) { if (!value) throw new Exception(message); }
        static void Reject(Action action)
        { try { action(); } catch (Exception error) when (error is ThemeException or System.Text.Json.JsonException or InvalidOperationException or KeyNotFoundException) { return; } throw new Exception("Invalid host preview contract accepted"); }
        test("host preview receipt binds version, owner, generation, process and exact revision while bounding RGB roles", () =>
        {
            var id = new string('a', 64); var revision = new string('b', 64);
            var value = new JsonObject { ["host_preview_version"] = 1, ["request_id"] = id, ["generation"] = 3, ["pid"] = 1234, ["state"] = "ready", ["revision"] = revision, ["diagnostic"] = "", ["high_contrast"] = false, ["system_high_contrast"] = false,
                ["colors"] = new JsonObject(HostPalette.Midnight.Colors.Select(color => new KeyValuePair<string, JsonNode?>(color.Key, JsonValue.Create(Convert.ToUInt32(color.Value[1..], 16))))) };
            byte[] Bytes() => Encoding.UTF8.GetBytes(value.ToJsonString());
            var receipt = HostPreviewSession.ReadStatus(Bytes(), id, 3, 1234, revision);
            Assert(receipt.State == "ready" && receipt.Colors["accent"] == 0xBFA4FC, "Receipt changed native RGB");
            Reject(() => HostPreviewSession.ReadStatus(Bytes(), new string('c', 64), 3, 1234, revision));
            Reject(() => HostPreviewSession.ReadStatus(Bytes(), id, 4, 1234, revision));
            Reject(() => HostPreviewSession.ReadStatus(Bytes(), id, 3, 4321, revision));
            Reject(() => HostPreviewSession.ReadStatus(Bytes(), id, 3, 1234, new string('d', 64)));
            value["state"] = "fallback_ready"; Reject(() => HostPreviewSession.ReadStatus(Bytes(), id, 3, 1234, revision));
            value["revision"] = ""; Assert(HostPreviewSession.ReadStatus(Bytes(), id, 3, 1234, revision).State == "fallback_ready", "Built-in fallback rejected");
            value["colors"]!["accent"] = 0x1000000; Reject(() => HostPreviewSession.ReadStatus(Bytes(), id, 3, 1234, revision));
            value["colors"]!["accent"] = 0; value["colors"]!["extra"] = 0; Reject(() => HostPreviewSession.ReadStatus(Bytes(), id, 3, 1234, revision));
            Reject(() => HostPreviewSession.ReadStatus(new byte[8193], id, 3, 1234, revision));
        });
        test("host preview rejects older capability before publishing, creating a session or executing a child", () =>
        {
            var directory = Path.Combine(root, "old-host-preview"); Directory.CreateDirectory(directory);
            var executable = Path.Combine(directory, "host.exe"); File.WriteAllBytes(executable, []);
            var capabilities = Path.Combine(directory, "folderstorm-theme-capabilities.json");
            File.WriteAllText(capabilities, "{\"host_palette_version\":1,\"source_commit\":\"" + new string('a', 40) + "\"}");
            var session = new HostPreviewSession(); var before = Directory.EnumerateFiles(directory).ToDictionary(path => path, Contract.HashFile);
            Reject(() => session.StartAsync(executable, null, catalog).GetAwaiter().GetResult());
            Assert(session.ProcessId == null && session.DirectoryPath.Length == 0 && before.All(file => Contract.HashFile(file.Key) == file.Value), "Old capabilities created or ran a preview");
            session.DisposeAsync().GetAwaiter().GetResult();
            HostPreviewSession.ValidateCapabilities(Encoding.UTF8.GetBytes("{\"host_preview_version\":1,\"host_palette_version\":1,\"source_commit\":\"" + new string('a', 40) + "\"}"));
            Reject(() => HostPreviewSession.ValidateCapabilities(Encoding.UTF8.GetBytes("{\"host_preview_version\":1.0,\"host_palette_version\":1,\"source_commit\":\"" + new string('a', 40) + "\"}")));
        });
    }
}
