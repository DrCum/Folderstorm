// Integration driver for the product's owned-process preview API.
// Actual UI inspection and input remain in the Computer Use skill.
using Folderstorm.Theme;
using System.Diagnostics;
using System.Text.Json;

if (args.Length != 5)
{
    Console.Error.WriteLine("viewer.exe base-folder package-or-dash receipt.json stop-file");
    return 2;
}
var executable = Path.GetFullPath(args[0]);
var receiptPath = Path.GetFullPath(args[3]);
var stopPath = Path.GetFullPath(args[4]);
var catalog = new BaseCatalog(args[1]);
var theme = args[2] == "-" ? null : PackageStore.ReadArchive(args[2], catalog);
using var capabilities = JsonDocument.Parse(File.ReadAllBytes(Path.Combine(Path.GetDirectoryName(executable)!, "folderstorm-theme-capabilities.json")));
var source = capabilities.RootElement.GetProperty("source_commit").GetString();
var timer = Stopwatch.StartNew();
await using var preview = new PreviewSession();
var child = 0;
try
{
    Contract.AtomicWrite(receiptPath, Contract.Bytes(new { State = "starting", Executable = executable, ViewerSource = source, Package = args[2], DriverPid = Environment.ProcessId }));
    await preview.StartAsync(executable, theme, catalog);
    child = preview.ProcessId ?? throw new ThemeException("preview_exit", "Owned preview closed before inspection");
    var ready = new { State = preview.Status, Executable = executable, ViewerSource = source, Package = args[2], DriverPid = Environment.ProcessId,
        ChildPid = child, Session = preview.DirectoryPath, Revision = theme?.Revision ?? "built-in", StartupSeconds = timer.Elapsed.TotalSeconds,
        NativeVisualAcceptance = "pending Computer Use inspection" };
    Contract.AtomicWrite(receiptPath, Contract.Bytes(ready));
    Console.WriteLine(JsonSerializer.Serialize(ready));
    Console.Out.Flush();
    while (!File.Exists(stopPath) && preview.ProcessId != null && timer.Elapsed < TimeSpan.FromMinutes(15)) await Task.Delay(250);
    var requested = File.Exists(stopPath);
    await preview.CloseAsync();
    Contract.AtomicWrite(receiptPath, Contract.Bytes(new { State = "closed", Executable = executable, ViewerSource = source, Package = args[2], DriverPid = Environment.ProcessId,
        ChildPid = child, Session = preview.DirectoryPath, CloseRequested = requested, NativeVisualAcceptance = "recorded separately" }));
    return 0;
}
catch (Exception error)
{
    Contract.AtomicWrite(receiptPath, Contract.Bytes(new { State = "failed", Executable = executable, ViewerSource = source, Package = args[2], ChildPid = child,
        Session = preview.DirectoryPath, Diagnostic = error.Message }));
    Console.Error.WriteLine(error.Message);
    return 1;
}
