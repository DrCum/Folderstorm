// SPDX-License-Identifier: LGPL-2.1-or-later
using Folderstorm.Theme;
using System.Text;
using System.Text.Json;

try
{
    if (args.Length == 0) { Console.WriteLine("theme <catalog|create|set-color|validate|export|import|publish|inspect> ...\nUse --base <viewer or repository directory> for base-aware validation."); return 0; }
    var values = args.ToList();
    var baseIndex = values.IndexOf("--base"); var baseRoot = baseIndex >= 0 ? values[baseIndex + 1] : null;
    if (baseIndex >= 0) values.RemoveRange(baseIndex, 2);
    var schemeIndex = values.IndexOf("--scheme"); var scheme = schemeIndex >= 0 ? values[schemeIndex + 1] : "folderstorm";
    if (schemeIndex >= 0) values.RemoveRange(schemeIndex, 2);
    BaseCatalog Base(string skin = "ansastorm", string? selectedScheme = null) => new(baseRoot ?? throw new ThemeException("arguments", "--base is required"), skin, selectedScheme ?? scheme);
    ThemeProject Project(string path) => ProjectStore.Open(path);
    object result;
    switch (values[0])
    {
        case "capture-contract":
            using (var fixture = Contract.Parse(File.ReadAllBytes(values[1])))
            {
                var input = fixture.RootElement;
                result = PreviewCapture.Request(Encoding.UTF8.GetBytes(input.GetProperty("request").GetRawText()), input.GetProperty("request_id").GetString()!, input.GetProperty("generation").GetInt64(), input.GetProperty("revision").GetString()!);
            }
            break;
        case "manifest":
            result = PackageStore.Manifest(File.ReadAllBytes(values[1])); break;
        case "catalog":
            var catalog = Base();
            result = new { catalog.Skin, catalog.Scheme, catalog.Schemes, catalog.Layers, Colors = catalog.Colors.Values, Textures = catalog.Textures.Values, catalog.FontConfigurations }; break;
        case "create":
            Contract.Require(!Directory.Exists(values[1]), "project_exists", "Choose a new project directory");
            var created = ProjectStore.Create(Base(), values.Count > 2 ? values[2] : "Untitled theme"); ProjectStore.Save(values[1], created);
            result = new { Project = Path.GetFullPath(values[1]), created.ThemeId }; break;
        case "set-color":
            var edited = Project(values[1]); Contract.Rgba(values[3]);
            Contract.Require(Base(edited.BaseSkin, edited.BaseScheme).Colors.ContainsKey(values[2]), "color", values[2]);
            ColorAuthoring.SetLiteral(edited, Base(edited.BaseSkin, edited.BaseScheme), values[2], values[3], values[1]); ProjectStore.Save(values[1], edited);
            result = new { Saved = true }; break;
        case "validate":
        case "inspect":
            ValidatedTheme validated;
            if (Directory.Exists(values[1]))
            {
                var manifest = PackageStore.Manifest(File.ReadAllBytes(Path.Combine(values[1], "theme.json")));
                validated = PackageStore.ReadRevision(values[1], baseRoot == null ? null : Base(manifest.BaseSkin, manifest.BaseScheme));
            }
            else validated = PackageStore.ReadArchive(values[1], baseRoot == null ? null : Base());
            result = new { Valid = true, validated.Revision, validated.Manifest, BaseVerified = baseRoot != null }; break;
        case "export":
            var project = Project(values[1]); var theme = PackageStore.Compile(values[1], project, Base(project.BaseSkin, project.BaseScheme));
            PackageStore.Export(values[2], theme); result = new { Package = Path.GetFullPath(values[2]), theme.Revision }; break;
        case "import":
            var importBase = Base(); var imported = PackageStore.ReadArchive(values[1], importBase); PackageStore.ImportProject(values[2], imported, importBase);
            result = new { Project = Path.GetFullPath(values[2]), Origin = "compiled-package", imported.Revision }; break;
        case "publish":
            var publishBase = Base(); var published = PackageStore.ReadArchive(values[1], publishBase);
            result = new { LibraryRevision = ThemeLibrary.Publish(published, values.Count > 2 ? values[2] : null, publishBase), published.Revision }; break;
        default: throw new ThemeException("arguments", "Unknown command: " + values[0]);
    }
    Console.Write(Encoding.UTF8.GetString(Contract.Bytes(result))); return 0;
}
catch (ThemeException e) { Console.Error.WriteLine(JsonSerializer.Serialize(new { error = e.Code, detail = e.Message })); return 2; }
catch (Exception e) when (e is IOException or UnauthorizedAccessException or ArgumentException or IndexOutOfRangeException or ArgumentOutOfRangeException or InvalidDataException)
{ Console.Error.WriteLine(JsonSerializer.Serialize(new { error = "input", detail = e.Message })); return 2; }
