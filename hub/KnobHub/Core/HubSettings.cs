using System.IO;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization;

namespace KnobHub.Core;

/// <summary>Knob Hub's settings, in %LOCALAPPDATA%\KnobHub\settings.json.</summary>
internal sealed class HubSettings
{
    public static readonly string Folder =
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "KnobHub");

    private static string FilePath => Path.Combine(Folder, "settings.json");

    /// <summary>The knob's own settings as last seen (the knob keeps them too, so it works without the PC).</summary>
    public KnobSettings Knob { get; set; } = new();

    /// <summary>The apps in Home ring order; apps not listed yet go at the end, turned on.</summary>
    public List<AppState> Apps { get; set; } = [];

    /// <summary>Where the window was: left, top, right, bottom (screen pixels, not maximized) and 1 if maximized.</summary>
    public int[]? Window { get; set; }

    /// <summary>The GitHub repository ("owner/name") whose releases the Add apps page lists (see AppCatalog).</summary>
    public string AppsRepo { get; set; } = "Shyam-Sangeeth/KnobHub-Apps";

    /// <summary>The board the preview pretends to be while no knob is connected: the last knob's (see
    /// <see cref="KnobBoard"/>).</summary>
    public string PreviewBoard { get; set; } = KnobBoard.Default.Id;

    public static HubSettings Load()
    {
        try
        {
            if (File.Exists(FilePath))
                return JsonSerializer.Deserialize<HubSettings>(File.ReadAllText(FilePath), Json.Files) ?? new();
        }
        catch (Exception) { }  // unreadable: start over with defaults
        return new();
    }

    public void Save()
    {
        try
        {
            Directory.CreateDirectory(Folder);
            File.WriteAllText(FilePath, JsonSerializer.Serialize(this, Json.Files));
        }
        catch (IOException) { }
        catch (UnauthorizedAccessException) { }
    }
}

internal sealed class AppState
{
    public string Id { get; set; } = "";
    public bool Enabled { get; set; } = true;
    /// <summary>The app's settings changed on the Apps page: id to option index (omitted while none are).</summary>
    [JsonIgnore(Condition = JsonIgnoreCondition.WhenWritingNull)]
    public Dictionary<string, int>? Settings { get; set; }
}

/// <summary>The knob's settings. Sent to the knob as a "settings" message (snake_case names).</summary>
internal sealed class KnobSettings
{
    public static readonly string[] LedModes = ["Level meter", "App colour", "Off"];
    public static readonly string[] Timeouts = ["15 seconds", "30 seconds", "1 minute", "5 minutes", "Never"];
    public static readonly string[] HapticStrengths = ["Light", "Medium", "Strong"];

    public int Brightness { get; set; } = 80;
    public int LedBrightness { get; set; } = 70;
    public int LedMode { get; set; }
    public int Timeout { get; set; } = 1;
    public bool DimIdle { get; set; } = true;
    /// <summary>Lights > "When dimmed": off while the screen is dimmed (they're always off with the screen).</summary>
    public bool LedsIdleOff { get; set; } = true;
    /// <summary>The Home clock: 24-hour, or 12-hour with AM/PM.</summary>
    [JsonPropertyName("clock_24h")]
    public bool Clock24h { get; set; }
    /// <summary>Boards with a vibration motor: clicks and alerts you feel.</summary>
    public bool Vibration { get; set; } = true;
    /// <summary>Boards with haptics: 0 light, 1 medium, 2 strong (the knob motor's detents and the vibration).</summary>
    public int Haptics { get; set; } = 1;
    public string Name { get; set; } = "knob-7f3a";

    /// <summary>Takes the values a "settings" message from the knob carries.</summary>
    public void Apply(JsonObject msg)
    {
        if (msg["brightness"] is JsonValue b && b.TryGetValue(out int brightness)) Brightness = brightness;
        if (msg["led_brightness"] is JsonValue lb && lb.TryGetValue(out int ledBrightness)) LedBrightness = ledBrightness;
        if (msg["led_mode"] is JsonValue lm && lm.TryGetValue(out int ledMode)) LedMode = ledMode;
        if (msg["timeout"] is JsonValue t && t.TryGetValue(out int timeout)) Timeout = timeout;
        if (msg["dim_idle"] is JsonValue d && d.TryGetValue(out bool dimIdle)) DimIdle = dimIdle;
        if (msg["leds_idle_off"] is JsonValue lo && lo.TryGetValue(out bool ledsIdleOff)) LedsIdleOff = ledsIdleOff;
        if (msg["clock_24h"] is JsonValue c && c.TryGetValue(out bool clock24h)) Clock24h = clock24h;
        if (msg["vibration"] is JsonValue v && v.TryGetValue(out bool vibration)) Vibration = vibration;
        if (msg["haptics"] is JsonValue h && h.TryGetValue(out int haptics)) Haptics = Math.Clamp(haptics, 0, 2);
        if (msg["name"] is JsonValue n && n.TryGetValue(out string? name) && !string.IsNullOrWhiteSpace(name)) Name = name;
    }

    public JsonObject ToMessage()
    {
        var msg = (JsonObject)Json.ToNode(this)!;
        msg.Insert(0, "t", "settings");
        return msg;
    }
}
