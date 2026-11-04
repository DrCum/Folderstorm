// Exercises product preview ownership with two live, independent viewer children.
using Folderstorm.Theme;
using System.Diagnostics;
using System.Text.Json;

if (args.Length != 4) { Console.Error.WriteLine("viewer.exe noop.fstheme combined.fstheme receipt.json"); return 2; }
var executable = Path.GetFullPath(args[0]);
var catalog = new BaseCatalog(Path.GetDirectoryName(executable)!);
var firstTheme = PackageStore.ReadArchive(args[1], catalog);
var nextTheme = PackageStore.ReadArchive(args[2], catalog);
var records = new List<object>();
void Record(string name, object value)
{
    records.Add(new { Name = name, Value = value });
    Contract.AtomicWrite(Path.GetFullPath(args[3]), Contract.Bytes(records));
    Console.WriteLine(name); Console.Out.Flush();
}
bool Alive(int id, DateTime started)
{
    try { using var process = Process.GetProcessById(id); return !process.HasExited && process.StartTime == started; }
    catch (ArgumentException) { return false; }
}
void Require(bool condition, string message) { if (!condition) throw new Exception(message); }
await using var sibling = new PreviewSession();
await using var primary = new PreviewSession();
await sibling.StartAsync(executable, null, catalog);
var siblingPid = sibling.ProcessId!.Value;
using var siblingProcess = Process.GetProcessById(siblingPid);
var siblingStarted = siblingProcess.StartTime;
var siblingDirectory = sibling.DirectoryPath;
var siblingRequest = File.ReadAllBytes(Path.Combine(siblingDirectory, "request.json"));
var siblingStatus = File.ReadAllBytes(Path.Combine(siblingDirectory, "status.json"));
Record("sibling-ready", new { siblingPid, siblingDirectory });
await primary.StartAsync(executable, firstTheme, catalog);
var oldPid = primary.ProcessId!.Value;
using var oldProcess = Process.GetProcessById(oldPid);
var oldStarted = oldProcess.StartTime;
var oldDirectory = primary.DirectoryPath;
Require(oldPid != siblingPid && oldDirectory != siblingDirectory, "Preview ownership was shared");
Require(Alive(siblingPid, siblingStarted), "Starting primary affected sibling");
Record("two-independent-previews", new { primaryPid = oldPid, primaryDirectory = oldDirectory, siblingPid, siblingDirectory });
await primary.StartAsync(executable, nextTheme, catalog);
Require(!Alive(oldPid, oldStarted), "Refreshing left the previous primary alive");
Require(Alive(siblingPid, siblingStarted), "Refreshing primary affected sibling");
Require(primary.DirectoryPath != oldDirectory && primary.DirectoryPath != siblingDirectory, "Refresh reused a profile");
using var response = JsonDocument.Parse(File.ReadAllBytes(Path.Combine(primary.DirectoryPath, "status.json")));
Require(response.RootElement.GetProperty("revision").GetString() == nextTheme.Revision, "Refresh loaded a stale theme");
Require(File.ReadAllBytes(Path.Combine(siblingDirectory, "request.json")).SequenceEqual(siblingRequest), "Sibling request changed");
Require(File.ReadAllBytes(Path.Combine(siblingDirectory, "status.json")).SequenceEqual(siblingStatus), "Sibling status changed");
var newPid = primary.ProcessId!.Value;
using var newProcess = Process.GetProcessById(newPid);
var newStarted = newProcess.StartTime;
Record("refresh-owned-child-only", new { oldPid, newPid, profile = primary.DirectoryPath, revision = nextTheme.Revision, siblingUnaffected = true });
await primary.CloseAsync();
Require(!Alive(newPid, newStarted) && Alive(siblingPid, siblingStarted), "Closing primary affected the wrong process");
Record("close-owned-child-only", new { primaryClosed = true, siblingAlive = true });
await sibling.CloseAsync();
Require(!Alive(siblingPid, siblingStarted), "Sibling did not close");
Record("passed", new { siblingClosed = true, noLogin = true, noInstallation = true });
return 0;
