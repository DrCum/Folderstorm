// Drive the product's owned host-preview API. Desktop inspection/input uses Computer Use.
using Folderstorm.Theme;
using System.Diagnostics;

if (args.Length is < 6 or > 7 || args.Length == 7 && args[6] != "--close-after-ready") { Console.Error.WriteLine("host.exe base-folder project-or-dash receipt.json stop-file baseline [--close-after-ready]"); return 2; }
var catalog = new BaseCatalog(args[1]); var output = Path.GetFullPath(args[3]); var stop = Path.GetFullPath(args[4]);
var theme = args[2] == "-" ? null : PackageStore.Compile(args[2], ProjectStore.Open(args[2]), catalog);
var baseline = int.Parse(args[5]); var timer = Stopwatch.StartNew();
await using var preview = new HostPreviewSession(); var child = 0;
try
{
    await preview.StartAsync(args[0], theme, catalog, baseline); child = preview.ProcessId!.Value;
    var value = new { State = preview.Receipt!.State, ChildPid = child, Session = preview.DirectoryPath, Receipt = preview.Receipt, Revision = theme?.Revision ?? "", Executable = args[0], DriverPid = Environment.ProcessId, StartupSeconds = timer.Elapsed.TotalSeconds, Scope = "standalone production offline preview module, not a full viewer/host build" };
    Contract.AtomicWrite(output, Contract.Bytes(value)); Console.WriteLine(System.Text.Json.JsonSerializer.Serialize(value)); Console.Out.Flush();
    while (args.Length == 6 && !File.Exists(stop) && preview.ProcessId != null && timer.Elapsed < TimeSpan.FromMinutes(15)) await Task.Delay(250);
    await preview.CloseAsync();
    Contract.AtomicWrite(output, Contract.Bytes(new { State = "closed", ChildPid = child, Session = preview.DirectoryPath, Receipt = preview.Receipt, Revision = theme?.Revision ?? "", DriverPid = Environment.ProcessId, StopRequested = File.Exists(stop) })); return 0;
}
catch (Exception error)
{
    Contract.AtomicWrite(output, Contract.Bytes(new { State = "failed", ChildPid = child, Session = preview.DirectoryPath, Diagnostic = error.Message })); Console.Error.WriteLine(error); return 1;
}
