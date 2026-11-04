// SPDX-License-Identifier: LGPL-2.1-or-later
using System.Diagnostics;
using System.Security.Cryptography;
using System.Text.Json;

namespace Folderstorm.Theme;

public sealed record HostPreviewReceipt(string State, string Revision, string Diagnostic, bool HighContrast, bool SystemHighContrast, Dictionary<string, uint> Colors);
public sealed class HostPreviewSession : IAsyncDisposable
{
    private readonly SemaphoreSlim lifecycle = new(1, 1);
    private Process? process;
    private DateTime started;
    private string requestId = "", revision = "";
    private long generation;
    public string DirectoryPath { get; private set; } = "";
    public int? ProcessId => process is { HasExited: false } ? process.Id : null;
    public HostPreviewReceipt? Receipt { get; private set; }
    public static void ValidateCapabilities(byte[] bytes)
    {
        using var document = Contract.Parse(bytes); var root = document.RootElement;
        Contract.Require(root.ValueKind == JsonValueKind.Object && root.TryGetProperty("host_preview_version", out var version) && version.ValueKind == JsonValueKind.Number && version.TryGetInt32(out var value) && value == 1 &&
            root.TryGetProperty("host_palette_version", out var palette) && palette.ValueKind == JsonValueKind.Number && palette.TryGetInt32(out var format) && format == 1 &&
            root.TryGetProperty("source_commit", out var source) && source.ValueKind == JsonValueKind.String && source.GetString() is { Length: 40 } commit && commit.All(char.IsAsciiHexDigit),
            "preview_unsupported", "Choose the matching host build with offline theme preview support.");
    }
    public static HostPreviewReceipt ReadStatus(byte[] bytes, string requestId, long generation, int pid, string revision)
    {
        Contract.Require(bytes.Length <= 8192, "size", "Host preview status exceeds 8 KiB");
        using var document = Contract.Parse(bytes); var root = document.RootElement;
        Contract.Require(root.ValueKind == JsonValueKind.Object && root.GetProperty("host_preview_version").GetInt32() == 1 && root.GetProperty("request_id").GetString() == requestId &&
            root.GetProperty("generation").GetInt64() == generation && root.GetProperty("pid").GetInt32() == pid, "preview_ownership", "Host preview response does not match the owned process.");
        var state = root.GetProperty("state").GetString()!; var actual = root.GetProperty("revision").GetString()!;
        Contract.Require(state is "ready" or "fallback_ready" or "closed" or "failed", "preview", "Invalid host preview state");
        Contract.Require(state != "ready" || actual == revision, "preview_ownership", "Host preview loaded a different revision");
        Contract.Require(state != "fallback_ready" || actual.Length == 0, "preview_ownership", "Host preview fallback retained a different revision");
        var colors = root.GetProperty("colors");
        Contract.Require(colors.ValueKind == JsonValueKind.Object && colors.EnumerateObject().Count() == HostPalette.Roles.Count && colors.EnumerateObject().All(color => HostPalette.Roles.Contains(color.Name) && color.Value.TryGetUInt32(out var rgb) && rgb <= 0xFFFFFF), "host_palette", "Invalid native host colors");
        return new(state, actual, root.GetProperty("diagnostic").GetString() ?? "", root.GetProperty("high_contrast").GetBoolean(), root.GetProperty("system_high_contrast").GetBoolean(), colors.EnumerateObject().ToDictionary(color => color.Name, color => color.Value.GetUInt32()));
    }
    public async Task StartAsync(string executable, ValidatedTheme? theme, BaseCatalog catalog, int baseline = 0, CancellationToken cancellation = default)
    {
        await lifecycle.WaitAsync(cancellation);
        try
        {
            await CloseOwnedAsync(); Contract.Require(baseline is 0 or 1, "host_preview", "Choose a built-in host preview palette");
            executable = Path.GetFullPath(executable); Contract.Require(File.Exists(executable), "viewer", "Matching session host executable is missing");
            var capabilities = Path.Combine(Path.GetDirectoryName(executable)!, "folderstorm-theme-capabilities.json");
            Contract.Require(File.Exists(capabilities), "preview_unsupported", "This host does not provide offline theme previews");
            ValidateCapabilities(LibraryCatalog.ReadBounded(capabilities, 65536));
            if (theme != null)
            {
                Contract.Require(theme.Files.ContainsKey(HostProject.PayloadPath), "host_palette", "The theme has no host palette");
                // Validate before publishing or starting a child, including caller-supplied records.
                theme = PackageStore.Validate(theme.Files, catalog); ThemeLibrary.Publish(theme, catalog: catalog);
            }
            revision = theme?.Revision ?? ""; Receipt = null;
            DirectoryPath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Folderstorm", "theme-host-preview", Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(DirectoryPath); var roaming = Path.Combine(DirectoryPath, "roaming"); var local = Path.Combine(DirectoryPath, "local");
            Directory.CreateDirectory(roaming); Directory.CreateDirectory(local);
            requestId = Convert.ToHexStringLower(RandomNumberGenerator.GetBytes(32)); ++generation;
            var request = Contract.Contained(DirectoryPath, "request.json");
            Contract.AtomicWrite(request, Contract.Bytes(new { HostPreviewVersion = 1, RequestId = requestId, Generation = generation, ThemeId = theme?.Manifest.ThemeId ?? "", Revision = revision, Baseline = baseline }));
            var start = new ProcessStartInfo(executable) { UseShellExecute = false, WorkingDirectory = Path.GetDirectoryName(executable)! };
            start.ArgumentList.Add("--fs-theme-host-preview"); start.ArgumentList.Add(request);
            start.Environment["APPDATA"] = roaming; start.Environment["LOCALAPPDATA"] = local;
            foreach (var key in start.Environment.Keys.Where(key => key.StartsWith("FOLDERSTORM_SESSION_", StringComparison.OrdinalIgnoreCase) || key.StartsWith("FOLDERSTORM_WORKER_", StringComparison.OrdinalIgnoreCase) || key.StartsWith("FOLDERSTORM_CONTROLLER_", StringComparison.OrdinalIgnoreCase)).ToArray()) start.Environment.Remove(key);
            process = Process.Start(start) ?? throw new ThemeException("viewer", "Host preview could not start"); started = process.StartTime;
            using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellation); timeout.CancelAfter(TimeSpan.FromSeconds(45));
            while (true)
            {
                timeout.Token.ThrowIfCancellationRequested(); Contract.Require(!process.HasExited && Owns(process), "preview_exit", "Host exited before its preview was ready");
                var status = Contract.Contained(DirectoryPath, "status.json");
                if (File.Exists(status))
                {
                    Receipt = ReadStatus(LibraryCatalog.ReadBounded(status, 8192), requestId, generation, process.Id, revision);
                    if (Receipt.State is "ready" or "fallback_ready") return;
                    if (Receipt.State == "failed") throw new ThemeException("preview", Receipt.Diagnostic);
                    throw new ThemeException("preview_exit", "Host preview closed before it was ready");
                }
                await Task.Delay(100, timeout.Token);
            }
        }
        catch { await CloseOwnedAsync(); throw; }
        finally { lifecycle.Release(); }
    }
    public async Task CloseAsync()
    {
        await lifecycle.WaitAsync(); try { await CloseOwnedAsync(); } finally { lifecycle.Release(); }
    }
    private async Task CloseOwnedAsync()
    {
        var owned = process; process = null; if (owned == null) return;
        try
        {
            if (!owned.HasExited && Owns(owned))
            {
                owned.CloseMainWindow(); using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(8));
                try { await owned.WaitForExitAsync(timeout.Token); }
                catch (OperationCanceledException) { if (!owned.HasExited && Owns(owned)) { owned.Kill(false); await owned.WaitForExitAsync(); } }
            }
        }
        finally { owned.Dispose(); }
    }
    private bool Owns(Process candidate)
    {
        try { using var current = Process.GetProcessById(candidate.Id); return current.StartTime == started; }
        catch (ArgumentException) { return false; }
    }
    public async ValueTask DisposeAsync() { await CloseAsync(); GC.SuppressFinalize(this); }
}
