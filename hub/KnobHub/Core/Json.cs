using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization;

namespace KnobHub.Core;

internal static class Json
{
    /// <summary>The link to the knob: snake_case names, no nulls (a missing field means "unchanged").</summary>
    public static readonly JsonSerializerOptions Link = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower,
        DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
    };

    /// <summary>Knob Hub's own files (settings.json, app.json).</summary>
    public static readonly JsonSerializerOptions Files = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        PropertyNameCaseInsensitive = true,
        WriteIndented = true,
    };

    /// <summary>Serializes with the object's runtime type, so a subclass (a screen) keeps all its fields.</summary>
    public static JsonNode? ToNode(object value) => JsonSerializer.SerializeToNode(value, value.GetType(), Link);

    public static string Str(this JsonObject o, string name) =>
        o[name] is JsonValue v && v.TryGetValue(out string? s) ? s : "";

    public static int Int(this JsonObject o, string name) =>
        o[name] is JsonValue v && v.TryGetValue(out int i) ? i : 0;
}
