using System.Text.Json.Nodes;

namespace KnobHub.Core;

/// <summary>
/// A knob board: a round ESP32 display with a screen of some size and maybe LEDs, a vibration motor or a motor on the
/// knob (knob-os/src/core/board.h). A knob says which it is in its hello; the preview pretends to be one.
/// </summary>
internal sealed record KnobBoard(string Id, string Name, int Size, bool Touch, int Leds, bool Vibration, bool Motor)
{
    /// <summary>The boards Knob OS knows (knob-os/src/core/boards.cpp), the first being the default.</summary>
    public static readonly KnobBoard[] Known =
    [
        new("crowpanel-1.28", "CrowPanel 1.28\"", 240, true, 5, false, false),
        new("crowpanel-1.46", "CrowPanel 1.46\"", 360, true, 1, false, false),
        new("motor-knob", "Motor knob", 240, true, 8, true, true),
    ];

    public static KnobBoard Default => Known[0];

    public static KnobBoard Find(string id) => Known.FirstOrDefault(b => b.Id == id) ?? Default;

    /// <summary>The board a knob's hello describes. Knob OS 0.1 didn't say: it only ran on the 1.28" CrowPanel.</summary>
    public static KnobBoard FromHello(JsonObject hello)
    {
        string id = hello.Str("board");
        if (id.Length == 0) return Default;
        var known = Known.FirstOrDefault(b => b.Id == id);
        int size = hello.Int("size");
        return new KnobBoard(id, known?.Name ?? id, size > 0 ? size : known?.Size ?? Default.Size, Bool(hello, "touch", true),
                             hello["leds"] is null ? known?.Leds ?? 0 : hello.Int("leds"), Bool(hello, "vibration", false),
                             Bool(hello, "motor", false));
    }

    public bool HasHaptics => Vibration || Motor;

    private static bool Bool(JsonObject msg, string name, bool fallback) =>
        msg[name] is JsonValue v && v.TryGetValue(out bool b) ? b : fallback;
}
