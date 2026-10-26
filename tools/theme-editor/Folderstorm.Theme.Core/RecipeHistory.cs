// SPDX-License-Identifier: LGPL-2.1-or-later
using System.Text;
using System.Text.Json;

namespace Folderstorm.Theme;

public static class RecipeHistory
{
    public const int StepLimit = 128;
    private static readonly Dictionary<string, string[]> Parameters = new(StringComparer.Ordinal)
    {
        ["replace"] = [], ["crop"] = ["x", "y", "width", "height"], ["resize"] = ["width", "height", "interpolation"],
        ["tint"] = ["color"], ["color-adjust"] = ["brightness", "contrast", "saturation", "gamma", "alpha_scale"], ["alpha"] = ["scale"],
        ["solid-fill"] = ["width", "height", "color"], ["linear-gradient"] = ["width", "height", "first", "last", "vertical"],
        ["border"] = ["thickness", "color"], ["nine-slice"] = ["left", "top", "right", "bottom", "clip_left", "clip_top", "clip_right", "clip_bottom"], ["pipeline"] = ["steps"]
    };
    public static string[] ReadOnlyReasons(Recipe recipe) => Inspect(recipe, 0).Distinct(StringComparer.Ordinal).ToArray();
    private static IEnumerable<string> Inspect(Recipe recipe, int depth)
    {
        if (depth >= 16) { yield return "Recipe nesting requires a newer editor."; yield break; }
        if (recipe.UnknownMetadata?.Count > 0) yield return "Unsupported recipe fields are preserved read-only.";
        if (!Parameters.TryGetValue(recipe.Operation, out var parameters)) { yield return "Unsupported recipe: " + recipe.Operation; yield break; }
        foreach (var name in recipe.Parameters.Keys)
            if (!parameters.Contains(name)) yield return "Unsupported " + recipe.Operation + " parameter: " + name;
        if (recipe.Operation != "pipeline") yield break;
        if (!recipe.Parameters.TryGetValue("steps", out var steps) || steps.ValueKind != JsonValueKind.Array || steps.GetArrayLength() is < 1 or > StepLimit)
        { yield return "Unsupported recipe pipeline structure."; yield break; }
        foreach (var element in steps.EnumerateArray())
        {
            Recipe? step = null;
            try { step = Contract.Read<Recipe>(Encoding.UTF8.GetBytes(element.GetRawText())); }
            catch (ThemeException) { }
            if (step?.Operation == null || step.Parameters == null) { yield return "Unsupported recipe pipeline step."; continue; }
            foreach (var reason in Inspect(step, depth + 1)) yield return reason;
        }
    }
    public static Recipe Append(Recipe? existing, Recipe operation)
    {
        Contract.Require(operation.Operation != "pipeline" && ReadOnlyReasons(operation).Length == 0, "recipe", "Choose a supported image operation");
        if (existing == null) return Contract.Read<Recipe>(Contract.Bytes(operation));
        var reasons = ReadOnlyReasons(existing);
        Contract.Require(reasons.Length == 0, "project_readonly", string.Join(" ", reasons));
        var steps = existing.Operation == "pipeline"
            ? existing.Parameters["steps"].EnumerateArray().Select(element => Contract.Read<Recipe>(Encoding.UTF8.GetBytes(element.GetRawText()))).ToList()
            : new List<Recipe> { existing };
        Contract.Require(steps.Count < StepLimit, "recipe", "Recipe history exceeds 128 steps");
        steps.Add(operation);
        return new("pipeline", new(StringComparer.Ordinal) { ["steps"] = JsonSerializer.SerializeToElement(steps, Contract.JsonOptions) });
    }
}
