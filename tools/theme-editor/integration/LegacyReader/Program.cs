// SPDX-License-Identifier: LGPL-2.1-or-later
using Folderstorm.Theme;

if (args.Length != 1) return 2;
var path = Path.Combine(args[0], ProjectStore.FileName); var before = File.ReadAllBytes(path);
var document = ProjectStore.OpenDocument(args[0]);
if (document.CanEdit || !document.ReadOnlyReasons.Any(reason => reason.Contains("Unsupported edit fields", StringComparison.Ordinal))) throw new Exception("Older reader did not preserve the new provenance field read-only");
try { ProjectStore.Save(args[0], document.Project!); throw new Exception("Older reader saved an unsupported project"); }
catch (ThemeException error) when (error.Code == "project_readonly") { }
if (!File.ReadAllBytes(path).SequenceEqual(before) || !document.OriginalBytes.SequenceEqual(before)) throw new Exception("Legacy reader changed source bytes");
Console.WriteLine("PASS retained legacy reader preserves compiled-manifest project bytes read-only and rejects Save before writing");
return 0;
