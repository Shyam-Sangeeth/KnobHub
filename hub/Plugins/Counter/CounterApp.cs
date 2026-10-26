using KnobHub.Sdk;

namespace Counter.KnobApp;

/// <summary>
/// A counter on the knob: it starts at 0 each time it's opened and goes up by one for each click turned to the right,
/// down by one for each click to the left, never below 0. The ring round the number fills as it counts; each time it's
/// full the next lap fills in a new random colour over the last one (counting back down shows the laps' colours again).
/// </summary>
public sealed class CounterApp : KnobHub.Sdk.KnobApp
{
    private const string Screen = "counter";
    private const int Lap = 20;  // counts to fill the ring once

    private readonly List<string> _colors = [];  // one per lap, picked as the laps are reached
    private int _count;

    public override IReadOnlyDictionary<string, KnobIcon> Icons { get; } = new Dictionary<string, KnobIcon>
    {
        ["counter.icon"] = KnobIcon.Glyph("123", "Segoe UI Semibold"),
    };

    public override void Open()
    {
        _count = 0;
        _colors.Clear();
        Host.Show(Screen, Describe());
    }

    public override void Input(KnobAppInput input)
    {
        switch (input.Kind)
        {
            case InputKind.Back:
                Host.Close();
                break;
            case InputKind.Rotate:
                int count = Math.Max(0, _count + input.Delta);
                if (count == _count) break;  // already at 0
                _count = count;
                Host.Update(Screen, Describe());
                break;
        }
    }

    // The ring shows the lap the count is in: full at 20, 40, ...; the lap before shows underneath in its colour.
    private RingScreen Describe()
    {
        int lap = _count == 0 ? 0 : (_count - 1) / Lap;
        int inLap = _count - lap * Lap;
        return new RingScreen
        {
            Text = _count.ToString(),
            Value = inLap * 100 / Lap,
            Color = LapColor(lap),
            TrackColor = lap > 0 ? LapColor(lap - 1) : "",  // "": the plain track again (counting back into the first lap)
        };
    }

    private string LapColor(int lap)
    {
        while (_colors.Count <= lap) _colors.Add(RandomColor(_colors.Count > 0 ? _colors[^1] : null));
        return _colors[lap];
    }

    /// <summary>A random colour that's bright enough to see on the knob's black (no near-black ones) and clearly
    /// different from the last lap's.</summary>
    private static string RandomColor(string? previous)
    {
        double previousHue = previous != null ? Hue(previous) : -1;
        double hue;
        do hue = Random.Shared.NextDouble() * 360;
        while (previousHue >= 0 && Math.Abs(((hue - previousHue + 540) % 360) - 180) < 50);
        double saturation = 0.55 + Random.Shared.NextDouble() * 0.45;
        double value = 0.8 + Random.Shared.NextDouble() * 0.2;
        return Hex(hue, saturation, value);
    }

    private static string Hex(double hue, double saturation, double value)
    {
        double c = value * saturation, x = c * (1 - Math.Abs(hue / 60 % 2 - 1)), m = value - c;
        var (r, g, b) = (int)(hue / 60) switch
        {
            0 => (c, x, 0.0),
            1 => (x, c, 0.0),
            2 => (0.0, c, x),
            3 => (0.0, x, c),
            4 => (x, 0.0, c),
            _ => (c, 0.0, x),
        };
        return $"#{(int)((r + m) * 255):X2}{(int)((g + m) * 255):X2}{(int)((b + m) * 255):X2}";
    }

    private static double Hue(string hex)
    {
        int rgb = Convert.ToInt32(hex[1..], 16);
        double r = (rgb >> 16 & 0xFF) / 255.0, g = (rgb >> 8 & 0xFF) / 255.0, b = (rgb & 0xFF) / 255.0;
        double max = Math.Max(r, Math.Max(g, b)), min = Math.Min(r, Math.Min(g, b)), d = max - min;
        if (d == 0) return 0;
        double hue = max == r ? (g - b) / d % 6 : max == g ? (b - r) / d + 2 : (r - g) / d + 4;
        return (hue * 60 + 360) % 360;
    }
}
