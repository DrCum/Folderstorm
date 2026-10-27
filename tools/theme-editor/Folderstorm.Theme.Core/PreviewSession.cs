// SPDX-License-Identifier: LGPL-2.1-or-later
using System.Diagnostics;
using System.Security.Cryptography;
using System.Text.Json;

namespace Folderstorm.Theme;

public sealed class PreviewSession : IAsyncDisposable
{
    private Process? process;
    private DateTime started;
    private string requestId = "";
    private long generation;
    private string revision = "";
    private bool canCapture;
    private readonly SemaphoreSlim lifecycle = new(1, 1);
    public string DirectoryPath { get; private set; } = "";
    public int? ProcessId => process is { HasExited: false } ? process.Id : null;
    public string Status { get; private set; } = "Closed";

    public static void ValidateCapabilities(byte[] bytes, bool requiresXui, bool requiresCapture = false, bool requiresHostPalette = false)
    {
        using var capabilities = Contract.Parse(bytes); var root = capabilities.RootElement;
        Contract.Require(root.ValueKind == JsonValueKind.Object && root.TryGetProperty("preview_version", out var version) && version.ValueKind == JsonValueKind.Number && version.TryGetInt32(out var number) && number == 1, "preview_unsupported", "Unsupported viewer preview protocol");
        Contract.Require(root.TryGetProperty("source_commit", out var source) && source.ValueKind == JsonValueKind.String && source.GetString() is { Length: 40 }, "preview_unsupported", "Viewer capability provenance is missing");
        if (requiresXui) Contract.Require(root.TryGetProperty("xui_rules_sha256", out var policy) && policy.ValueKind == JsonValueKind.String && policy.GetString() == XuiPolicy.Fingerprint, "preview_unsupported", "This viewer uses different widget rules. Choose the matching viewer build in Setup.");
        if (requiresCapture) Contract.Require(root.TryGetProperty("preview_capture_version", out var capture) && capture.ValueKind == JsonValueKind.Number && capture.TryGetInt32(out var captureVersion) && captureVersion == 1, "preview_unsupported", "Choose a matching viewer with preview image capture in Setup.");
        if (requiresHostPalette) Contract.Require(root.TryGetProperty("host_palette_version", out var host) && host.ValueKind == JsonValueKind.Number && host.TryGetInt32(out var hostVersion) && hostVersion == 1, "preview_unsupported", "Choose a matching viewer with host palette support in Setup.");
    }

    public async Task StartAsync(string executable, ValidatedTheme? theme, BaseCatalog catalog, CancellationToken cancellation = default, bool requiresCapture = false)
    {
        await lifecycle.WaitAsync(cancellation);
        try
        {
        await CloseOwnedAsync();
        executable = Path.GetFullPath(executable);
        Contract.Require(File.Exists(executable), "viewer", "Choose the matching viewer executable");
        var capabilitiesPath = Path.Combine(Path.GetDirectoryName(executable)!, "folderstorm-theme-capabilities.json");
        Contract.Require(File.Exists(capabilitiesPath), "preview_unsupported", "This viewer does not provide offline theme previews");
        var capabilities = File.ReadAllBytes(capabilitiesPath);
        ValidateCapabilities(capabilities, theme?.Manifest.Files.Any(file => file.Kind == "xui") == true, requiresCapture, theme?.Files.ContainsKey(HostProject.PayloadPath) == true);
        using (var document = Contract.Parse(capabilities)) canCapture = document.RootElement.TryGetProperty("preview_capture_version", out var capture) && capture.ValueKind == JsonValueKind.Number && capture.TryGetInt32(out var captureVersion) && captureVersion == 1;
        revision = theme?.Revision ?? "";
        if (theme != null) ThemeLibrary.Publish(theme, catalog: catalog);
        DirectoryPath = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Folderstorm", "theme-preview", Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(DirectoryPath);
        var roaming = Path.Combine(DirectoryPath, "roaming"); var local = Path.Combine(DirectoryPath, "local");
        Directory.CreateDirectory(roaming); Directory.CreateDirectory(local);
        requestId = Convert.ToHexStringLower(RandomNumberGenerator.GetBytes(32)); ++generation;
        var request = Path.Combine(DirectoryPath, "request.json");
        Contract.AtomicWrite(request, Contract.Bytes(new
        { PreviewVersion = 1, RequestId = requestId, Generation = generation, ThemeId = theme?.Manifest.ThemeId ?? "", Revision = theme?.Revision ?? "" }));
        var info = new ProcessStartInfo(executable) { UseShellExecute = false, WorkingDirectory = Path.GetDirectoryName(executable)! };
        info.ArgumentList.Add("--fs-theme-preview"); info.ArgumentList.Add(request);
        info.Environment["APPDATA"] = roaming; info.Environment["LOCALAPPDATA"] = local;
        foreach (var key in info.Environment.Keys.Where(k => k.StartsWith("FOLDERSTORM_SESSION_", StringComparison.OrdinalIgnoreCase) ||
            k.StartsWith("FOLDERSTORM_WORKER_", StringComparison.OrdinalIgnoreCase) || k.StartsWith("FOLDERSTORM_CONTROLLER_", StringComparison.OrdinalIgnoreCase)).ToArray()) info.Environment.Remove(key);
        process = Process.Start(info) ?? throw new ThemeException("viewer", "Viewer could not start"); started = process.StartTime;
        Status = "Starting";
        try
        {
            using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellation); timeout.CancelAfter(TimeSpan.FromSeconds(45));
            while (true)
            {
                timeout.Token.ThrowIfCancellationRequested();
                Contract.Require(!process.HasExited, "preview_exit", "Viewer exited before its preview was ready");
                var statusPath = Path.Combine(DirectoryPath, "status.json");
                if (File.Exists(statusPath))
                {
                    using var status = Contract.Parse(File.ReadAllBytes(statusPath)); var root = status.RootElement;
                    Contract.Require(root.GetProperty("request_id").GetString() == requestId && root.GetProperty("generation").GetInt64() == generation &&
                        root.GetProperty("pid").GetInt32() == process.Id, "preview_ownership", "Preview response does not match the owned viewer");
                    Status = root.GetProperty("state").GetString()!;
                    if (Status is "ready" or "fallback_ready")
                    {
                        Contract.Require(Status != "ready" || root.GetProperty("revision").GetString() == revision, "preview_ownership", "Preview loaded a different revision");
                        return;
                    }
                    if (Status == "failed") throw new ThemeException("preview", root.GetProperty("diagnostic").GetString()!);
                }
                await Task.Delay(150, timeout.Token);
            }
        }
        catch { await CloseOwnedAsync(); throw; }
        }
        finally { lifecycle.Release(); }
    }
    public async Task<CaptureResult> CaptureAsync(CancellationToken cancellation = default)
    {
        await lifecycle.WaitAsync(cancellation);
        try
        {
            Contract.Require(process is { HasExited: false } && Owns(process) && Status == "ready", "capture", "Start a valid native preview before capturing");
            Contract.Require(canCapture, "preview_unsupported", "This viewer does not support preview image capture");
            var owned = process;
            var captureId = Convert.ToHexStringLower(RandomNumberGenerator.GetBytes(32));
            var request = new CaptureRequest(1, requestId, generation, revision, captureId, 1280, 900);
            Contract.AtomicWrite(Contract.Contained(DirectoryPath, "capture-request.json"), Contract.Bytes(request));
            using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellation); timeout.CancelAfter(TimeSpan.FromSeconds(12));
            while (true)
            {
                timeout.Token.ThrowIfCancellationRequested();
                Contract.Require(!owned.HasExited && Owns(owned), "preview_exit", "Preview exited during capture");
                var response = Contract.Contained(DirectoryPath, "capture-status.json");
                if (File.Exists(response))
                {
                    var bytes = LibraryCatalog.ReadBounded(response, 8192);
                    using var status = Contract.Parse(bytes);
                    if (status.RootElement.GetProperty("capture_id").GetString() == captureId)
                    {
                        // Derive the only readable image name from our nonce, never from the receipt.
                        var image = Contract.Contained(DirectoryPath, PreviewCapture.FileName(captureId));
                        var png = status.RootElement.GetProperty("state").GetString() == "captured" ? LibraryCatalog.ReadBounded(image, 32L * 1024 * 1024) : [];
                        return PreviewCapture.Response(bytes, request, owned.Id, png);
                    }
                }
                await Task.Delay(100, timeout.Token);
            }
        }
        finally { lifecycle.Release(); }
    }
    public async Task CloseAsync()
    {
        await lifecycle.WaitAsync();
        try { await CloseOwnedAsync(); }
        finally { lifecycle.Release(); }
    }
    private async Task CloseOwnedAsync()
    {
        var owned = process; process = null; canCapture = false;
        if (owned == null) return;
        try
        {
            if (!owned.HasExited && Owns(owned))
            {
                owned.CloseMainWindow();
                using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(8));
                try { await owned.WaitForExitAsync(timeout.Token); }
                catch (OperationCanceledException) { if (!owned.HasExited && Owns(owned)) { owned.Kill(false); await owned.WaitForExitAsync(); } }
            }
        }
        finally { owned.Dispose(); Status = "Closed"; }
    }
    private bool Owns(Process candidate)
    {
        try { using var current = Process.GetProcessById(candidate.Id); return current.StartTime == started; }
        catch (ArgumentException) { return false; }
    }
    public async ValueTask DisposeAsync() { await CloseAsync(); GC.SuppressFinalize(this); }
}
