// Native activation/recovery tests use a private empty profile and never log in.
using Folderstorm.Theme;
using System.Diagnostics;
using System.Text;
using System.Text.Json;
using System.Xml.Linq;

if (args.Length != 4) { Console.Error.WriteLine("viewer.exe no-op.fstheme evidence-directory observed-app-folder"); return 2; }
var executable = Path.GetFullPath(args[0]);
var viewerRoot = Path.GetDirectoryName(executable)!;
var evidence = Path.GetFullPath(args[2]);
if (Directory.Exists(evidence)) throw new Exception("Use a new evidence directory");
Directory.CreateDirectory(evidence);
Contract.PathName(args[3]);
if (args[3].Contains('/')) throw new Exception("Expected one observed app folder name");
var catalog = new BaseCatalog(viewerRoot);
var good = PackageStore.ReadArchive(args[1], catalog);
ThemeLibrary.Publish(good, catalog: catalog);
var local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
var profile = Path.Combine(local, "Folderstorm", "theme-recovery-tests", Guid.NewGuid().ToString("N"));
var roaming = Path.Combine(profile, "roaming");
var cache = Path.Combine(profile, "local");
var settings = Path.Combine(roaming, args[3], "user_settings");
Directory.CreateDirectory(settings); Directory.CreateDirectory(cache);
var selectionPath = Path.Combine(settings, "custom-theme.json");
var markerPath = Path.Combine(settings, "custom-theme.starting.json");
var lastGoodPath = Path.Combine(settings, "custom-theme.last-good.json");
var errorPath = Path.Combine(settings, "custom-theme.error.json");
var logPath = Path.Combine(roaming, args[3], "logs", "Folderstorm.log");
Contract.AtomicWrite(Path.Combine(settings, "settings.xml"), Encoding.UTF8.GetBytes("<llsd><map><key>AutoLogin</key><map><key>Type</key><string>Boolean</string><key>Value</key><boolean>false</boolean></map></map></llsd>"));
var colorsPath = Path.Combine(settings, "colors.xml");
Contract.AtomicWrite(colorsPath, Encoding.UTF8.GetBytes("<colors><color name=\"ButtonImageColor\" value=\"0.11 0.22 0.33 1\"/></colors>"));
var defaults = Contract.Xml(File.ReadAllBytes(Path.Combine(viewerRoot, "app_settings", "settings.xml"))).Root!.Element("map")!;
string Value(XElement map, string name) => map.Elements("key").First(k => k.Value == name).ElementsAfterSelf().First().Value;
string DefaultValue(string name) => Value(defaults.Elements("key").First(k => k.Value == name).ElementsAfterSelf().First(), "Value");
var baseline = new[] { "SkinCurrent", "SkinCurrentTheme", "FSFontSettingsFile" }.ToDictionary(k => k, DefaultValue);
Contract.AtomicWrite(Path.Combine(evidence, "baseline-settings.json"), Contract.Bytes(baseline));
var goodSelection = new ThemeSelection(good.Manifest.ThemeId, good.Revision);
var results = new List<object>();
var ownedFixtures = new List<string>();
bool Selected(ThemeSelection expected) { try { return Contract.Read<ThemeSelection>(File.ReadAllBytes(selectionPath)) == expected; } catch { return false; } }
bool LastGood() { try { return Contract.Read<ThemeSelection>(File.ReadAllBytes(lastGoodPath)) == goodSelection && !File.Exists(markerPath); } catch { return false; } }
bool IsNullSelection() { try { using var json = Contract.Parse(File.ReadAllBytes(selectionPath)); return json.RootElement.ValueKind == JsonValueKind.Null; } catch { return false; } }
string Error() { try { return JsonDocument.Parse(File.ReadAllBytes(errorPath)).RootElement.GetProperty("diagnostic").GetString() ?? ""; } catch { return ""; } }
void Select(ThemeSelection value) => Contract.AtomicWrite(selectionPath, Contract.Bytes(value));

async Task Run(string name, Func<bool> signal, bool reset = false)
{
    var directory = Path.Combine(evidence, name); Directory.CreateDirectory(directory);
    File.Copy(selectionPath, Path.Combine(directory, "selection-before.json"));
    Contract.AtomicWrite(errorPath, "null\n"u8.ToArray());
    var info = new ProcessStartInfo(executable) { WorkingDirectory = viewerRoot, UseShellExecute = false, RedirectStandardOutput = true, RedirectStandardError = true };
    info.Environment["APPDATA"] = roaming; info.Environment["LOCALAPPDATA"] = cache;
    foreach (var key in info.Environment.Keys.Where(k => k.StartsWith("FOLDERSTORM_SESSION_", StringComparison.OrdinalIgnoreCase) || k.StartsWith("FOLDERSTORM_WORKER_", StringComparison.OrdinalIgnoreCase) || k.StartsWith("FOLDERSTORM_CONTROLLER_", StringComparison.OrdinalIgnoreCase)).ToArray()) info.Environment.Remove(key);
    if (reset) info.ArgumentList.Add("--reset-custom-theme");
    using var child = Process.Start(info) ?? throw new Exception("Viewer did not start");
    var started = child.StartTime;
    using var stdout = File.Create(Path.Combine(directory, "stdout.log")); using var stderr = File.Create(Path.Combine(directory, "stderr.log"));
    var output = child.StandardOutput.BaseStream.CopyToAsync(stdout); var errors = child.StandardError.BaseStream.CopyToAsync(stderr);
    var timer = Stopwatch.StartNew(); bool observed = false; bool killed = false;
    try
    {
        while (!child.HasExited && timer.Elapsed < TimeSpan.FromSeconds(75))
        {
            child.Refresh();
            if (child.MainWindowHandle != IntPtr.Zero && signal()) { observed = true; break; }
            await Task.Delay(200);
        }
    }
    finally
    {
        if (!child.HasExited)
        {
            child.CloseMainWindow();
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(8));
            try { await child.WaitForExitAsync(timeout.Token); }
            catch (OperationCanceledException)
            {
                using var current = Process.GetProcessById(child.Id);
                if (current.StartTime != started) throw new Exception("Owned child identity changed");
                child.Kill(false); killed = true; await child.WaitForExitAsync();
            }
        }
        await Task.WhenAll(output, errors);
    }
    var settingsDocument = Contract.Xml(File.ReadAllBytes(Path.Combine(settings, "settings.xml"))).Root!.Element("map")!;
    var preserved = baseline.All(pair => !settingsDocument.Elements("key").Any(k => k.Value == pair.Key) || Value(settingsDocument.Elements("key").First(k => k.Value == pair.Key).ElementsAfterSelf().First(), "Value") == pair.Value);
    var color = Contract.Xml(File.ReadAllBytes(colorsPath)).Root!.Elements("color").Single(c => (string?)c.Attribute("name") == "ButtonImageColor");
    var rgba = Contract.Rgba((string)color.Attribute("value")!);
    var colorsPreserved = rgba.Zip(new[] { .11, .22, .33, 1 }).All(p => Math.Abs(p.First - p.Second) < .00001);
    foreach (var path in new[] { selectionPath, markerPath, lastGoodPath, errorPath, Path.Combine(settings, "settings.xml"), colorsPath, logPath })
        if (File.Exists(path)) File.Copy(path, Path.Combine(directory, Path.GetFileName(path)));
    var result = new { Name = name, Passed = observed && preserved && colorsPreserved, Observed = observed, BaselineSettingsPreserved = preserved, PersonalColorPreserved = colorsPreserved,
        ChildPid = child.Id, Seconds = timer.Elapsed.TotalSeconds, OwnedChildForcedAfterTimeout = killed, Diagnostic = Error(), Profile = profile };
    results.Add(result); Contract.AtomicWrite(Path.Combine(evidence, "results.json"), Contract.Bytes(results));
    Console.WriteLine(JsonSerializer.Serialize(result)); Console.Out.Flush();
    if (!result.Passed) throw new Exception("Native recovery check failed: " + name);
}

ThemeSelection Fixture(string kind)
{
    var manifest = good.Manifest with { ThemeId = Guid.NewGuid().ToString("D"), DisplayName = "Private recovery test " + kind };
    var files = good.Files.ToDictionary(p => p.Key, p => p.Value.ToArray(), StringComparer.Ordinal);
    if (kind == "changed-base")
    {
        var dependencies = Contract.Read<DependencySet>(files["base-dependencies.json"]);
        var first = dependencies.Files.First(f => f.Present);
        dependencies = dependencies with { Files = dependencies.Files.Select(f => f.Path == first.Path ? f with { Sha256 = new string('0', 64) } : f).ToArray() };
        files["base-dependencies.json"] = Contract.Bytes(dependencies);
        manifest = manifest with { BaseFingerprint = Contract.Hash(files["base-dependencies.json"]), Files = manifest.Files.Select(f => f.Path == "base-dependencies.json" ? f with { Size = files[f.Path].Length, Sha256 = Contract.Hash(files[f.Path]) } : f).ToArray() };
    }
    files["theme.json"] = Contract.Bytes(manifest); var revision = Contract.Hash(files["theme.json"]);
    if (kind == "corrupt") files["base-dependencies.json"] = [.. files["base-dependencies.json"], 32];
    var directory = Contract.Contained(ThemeLibrary.DefaultRoot, manifest.ThemeId + "/" + revision);
    if (Directory.Exists(directory)) throw new Exception("Fresh fixture revision already exists");
    Directory.CreateDirectory(directory);
    ownedFixtures.Add(directory);
    foreach (var file in files) Contract.AtomicWrite(Contract.Contained(directory, file.Key), file.Value);
    Contract.AtomicWrite(Path.Combine(evidence, "fixture-" + kind + ".json"), Contract.Bytes(new { manifest.ThemeId, Revision = revision, Directory = directory, IntentionalInvalidFixture = true }));
    return new(manifest.ThemeId, revision);
}

try
{
Select(goodSelection); await Run("valid-selection", () => Selected(goodSelection) && LastGood());
var missing = new ThemeSelection(Guid.NewGuid().ToString("D"), new string('0', 64));
Select(missing); Contract.AtomicWrite(markerPath, Contract.Bytes(missing));
await Run("startup-rollback", () => Selected(goodSelection) && LastGood());
Select(goodSelection); await Run("explicit-reset", IsNullSelection, reset: true);
Select(missing); await Run("missing-revision", () => IsNullSelection() && Error().Length > 0);
Select(Fixture("changed-base")); await Run("changed-base", () => IsNullSelection() && Error().StartsWith("base_changed:", StringComparison.Ordinal));
Select(Fixture("corrupt")); await Run("corrupt-revision", () => IsNullSelection() && Error().StartsWith("integrity:", StringComparison.Ordinal));
Console.WriteLine("Six native recovery cases passed; no login or installation performed.");
}
finally
{
    // All Run paths close their owned child before returning or throwing.
    var archive = Path.Combine(evidence, "fixture-bytes");
    Directory.CreateDirectory(archive);
    foreach (var directory in ownedFixtures)
        Directory.Move(directory, Path.Combine(archive, Path.GetFileName(Path.GetDirectoryName(directory)!) + "-" + Path.GetFileName(directory)));
}
return 0;
