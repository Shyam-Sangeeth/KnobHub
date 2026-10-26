using System.Text.Json.Serialization;

namespace KnobHub.Sdk;

// The knob draws screens from templates; an app fills one in. Colors are "#RRGGBB". Icons are names: Knob Hub's
// built-in ones (music, play, game, headphones, speaker, settings, clock...) or the app's own (KnobApp.Icons).

/// <summary>A screen for the knob: one of <see cref="DialScreen"/>, <see cref="TilesScreen"/>,
/// <see cref="ListScreen"/>, <see cref="ControlsScreen"/>, <see cref="RingScreen"/>, <see cref="MediaScreen"/>.</summary>
public abstract class KnobScreen
{
    /// <summary>The Knob OS template that draws it.</summary>
    [JsonIgnore]
    public abstract string Template { get; }
}

/// <summary>One big value on a 270° arc, e.g. a volume. Turning reports rotate (+1/-1, the knob moves the value by
/// 2 right away); pressing reports press (e.g. mute); tapping the button reports tap with its id.</summary>
public sealed class DialScreen : KnobScreen
{
    /// <inheritdoc />
    public override string Template => "dial";
    /// <summary>Shown in capitals above the value.</summary>
    public string? Label { get; set; }
    public string? Icon { get; set; }
    public string? Color { get; set; }
    /// <summary>0-100.</summary>
    public int Value { get; set; }
    /// <summary>After the value; "%" if not set.</summary>
    public string? Unit { get; set; }
    /// <summary>0-1: a live meter inside the arc.</summary>
    public double? Level { get; set; }
    public bool Muted { get; set; }
    /// <summary>A button under the value, e.g. the output device.</summary>
    public DialButton? Button { get; set; }
    public string? Hint { get; set; }
}

/// <summary>The button on a <see cref="DialScreen"/>.</summary>
public sealed record DialButton(string Id, string Label);

/// <summary>Up to three round tiles in a row with pills under them, e.g. audio channels. Pressing a tile reports press
/// with its id; a pill reports tap.</summary>
public sealed class TilesScreen : KnobScreen
{
    /// <inheritdoc />
    public override string Template => "tiles";
    public string? Title { get; set; }
    /// <summary>The pills' accent: a toggle that's on is filled with it.</summary>
    public string? Color { get; set; }
    public List<Tile> Items { get; set; } = [];
    /// <summary>The tile id where focus starts.</summary>
    public string? Selected { get; set; }
    public List<Pill> Pills { get; set; } = [];
    public string? Hint { get; set; }
}

/// <summary>A tile on a <see cref="TilesScreen"/>.</summary>
/// <param name="Value">0-100: the ring around the tile.</param>
/// <param name="Tag">A word under the tile.</param>
/// <param name="TagIcon">An icon in a small badge on the tile's ring, at the bottom.</param>
/// <param name="Detail">Text inside the tile instead of the value's percentage, e.g. a device; long text scrolls.</param>
/// <param name="Busy">Something is under way (e.g. switching device): an arc spins round the ring.</param>
/// <param name="Muted">The icon is crossed out.</param>
public sealed record Tile(string Id, string Label, string Icon, string Color, int Value, string? Tag = null, string? TagIcon = null,
                          string? Detail = null, bool Busy = false, bool Muted = false);

/// <summary>A pill on a <see cref="TilesScreen"/>: an on/off toggle or a segmented choice.</summary>
public sealed class Pill
{
    public string Id { get; set; } = "";
    /// <summary>"toggle" or "segment".</summary>
    public string Kind { get; set; } = "toggle";
    public string? Label { get; set; }
    /// <summary>A toggle's icon left of its label.</summary>
    public string? Icon { get; set; }
    /// <summary>The icon's colour while the toggle is on (default the screen's <see cref="TilesScreen.Color"/>).</summary>
    public string? OnColor { get; set; }
    /// <summary>The icon's colour while it's off (default grey).</summary>
    public string? OffColor { get; set; }
    public bool On { get; set; }
    public List<string>? Options { get; set; }
    public int Selected { get; set; }

    public static Pill Toggle(string id, string label, bool on, string? icon = null, string? onColor = null, string? offColor = null) =>
        new() { Id = id, Kind = "toggle", Label = label, On = on, Icon = icon, OnColor = onColor, OffColor = offColor };

    public static Pill Segment(string id, IEnumerable<string> options, int selected) =>
        new() { Id = id, Kind = "segment", Options = [.. options], Selected = selected };
}

/// <summary>A list of rows, three at a time, e.g. output devices. Pressing reports press with the row's id.</summary>
public sealed class ListScreen : KnobScreen
{
    /// <inheritdoc />
    public override string Template => "list";
    public string? Title { get; set; }
    public string? Color { get; set; }
    /// <summary>A small pill under the title.</summary>
    public string? Note { get; set; }
    public List<ListRow> Items { get; set; } = [];
    /// <summary>The row id where focus starts; otherwise the selected row.</summary>
    public string? Focus { get; set; }
    public string? Hint { get; set; }
}

/// <summary>A row on a <see cref="ListScreen"/>.</summary>
/// <param name="Selected">The current choice: shown with a tick.</param>
/// <param name="Value">Right-aligned text, e.g. "30 s".</param>
public sealed record ListRow(string Id, string Label, string? Detail = null, string? Icon = null, bool Selected = false,
                             string? Value = null, bool Danger = false);

/// <summary>A value with a toggle and a picker under it, e.g. an audio channel: its volume, a feature on/off and the
/// output device. The knob's press moves between them and turning changes the selected one: the value reports rotate
/// (+1/-1, the knob moves it by <see cref="Step"/> right away), the toggle reports tap (item = its id, delta 1 on, 0
/// off), the picker steps through its options and reports select (item = the option's id) once the knob rests.
/// Tapping the icon or the number, or pressing twice quickly, reports press (e.g. mute).</summary>
public sealed class ControlsScreen : KnobScreen
{
    /// <inheritdoc />
    public override string Template => "controls";
    /// <summary>Shown in capitals under the icon.</summary>
    public string? Label { get; set; }
    public string? Icon { get; set; }
    public string? Color { get; set; }
    /// <summary>0-100: the ring and the number at the bottom.</summary>
    public int Value { get; set; }
    /// <summary>How much one click of the knob changes the value.</summary>
    public int Step { get; set; } = 2;
    /// <summary>After the number; "%" if not set.</summary>
    public string? Unit { get; set; }
    /// <summary>The icon is crossed out and <see cref="MutedText"/> shows instead of the number.</summary>
    public bool Muted { get; set; }
    /// <summary>"Muted" if not set.</summary>
    public string? MutedText { get; set; }
    public ControlToggle? Toggle { get; set; }
    public ControlPicker? Picker { get; set; }
}

/// <summary>The toggle on a <see cref="ControlsScreen"/>: a pill with an optional icon, which is <paramref name="OnColor"/>
/// while on (default the screen's colour) and <paramref name="OffColor"/> while off (default grey).</summary>
/// <param name="Hidden">Not shown (e.g. not available right now).</param>
public sealed record ControlToggle(string Id, string Label, bool On, string? Icon = null, string? OnColor = null,
                                   string? OffColor = null, bool Hidden = false);

/// <summary>The picker on a <see cref="ControlsScreen"/>: a card showing an option's name and detail.</summary>
/// <param name="EmptyText">Shown when no option is selected; "None" if not set.</param>
public sealed record ControlPicker(string? Selected, List<PickerOption> Options, string? EmptyText = null);

/// <summary>An option of a <see cref="ControlPicker"/>: its name and a detail line.</summary>
public sealed record PickerOption(string Id, string Name, string? Detail = null);

/// <summary>A number (or a short text) in the middle of a full ring, e.g. a counter or a timer. The text is drawn as
/// big as fits inside the ring. Turning reports rotate (+1/-1) and pressing reports press; the app updates the
/// screen.</summary>
public sealed class RingScreen : KnobScreen
{
    /// <inheritdoc />
    public override string Template => "ring";
    public string? Text { get; set; }
    /// <summary>0-100: how far round the ring is filled, clockwise from the top.</summary>
    public int Value { get; set; }
    /// <summary>The fill.</summary>
    public string? Color { get; set; }
    /// <summary>The ring under the fill, e.g. the colour of a lap already completed; "" goes back to the default dark
    /// track.</summary>
    public string? TrackColor { get; set; }
    /// <summary>Small, above the text.</summary>
    public string? Label { get; set; }
}

/// <summary>What's playing. Turning seeks 5 s per step (the knob shows the new position right away) and reports
/// rotate +1/-1. Pressing, tapping the art or the play button reports tap "play"; the other buttons and swipes report
/// tap "previous" / "next". While <see cref="Playing"/>, the knob moves the position on by itself.</summary>
public sealed class MediaScreen : KnobScreen
{
    /// <inheritdoc />
    public override string Template => "media";
    public string? Title { get; set; }
    public string? Artist { get; set; }
    /// <summary>The app playing, e.g. "Spotify".</summary>
    public string? Source { get; set; }
    public bool Playing { get; set; }
    /// <summary>Seconds into the track (the arc around the edge).</summary>
    public double Position { get; set; }
    /// <summary>The track's length in seconds; 0 if unknown.</summary>
    public double Duration { get; set; }
    public bool CanSeek { get; set; } = true;
    public bool CanPrevious { get; set; } = true;
    public bool CanNext { get; set; } = true;
    public string? Color { get; set; }
    /// <summary>The album art, up to 80 x 80; <see cref="KnobImage.None"/> shows the music icon. Send it only when it
    /// changes: it's the biggest part of the message.</summary>
    public KnobImage? Art { get; set; }
    /// <summary>The title drawn as text (<see cref="KnobImage.FromAlpha"/>) when the knob's fonts can't draw it, e.g.
    /// Malayalam or Japanese; <see cref="KnobImage.None"/> goes back to <see cref="Title"/>. Up to 1024 px wide: past
    /// 170 px it scrolls round, like a long title.</summary>
    public KnobImage? TitleImage { get; set; }
    /// <summary>The same for <see cref="Artist"/> (scrolls past 160 px).</summary>
    public KnobImage? ArtistImage { get; set; }
}

/// <summary>A small picture for the knob: RGB565 pixels (what the knob's panel uses), or for drawn text alpha only
/// (the knob colours it like the text it stands for).</summary>
public sealed class KnobImage
{
    public int W { get; init; }
    public int H { get; init; }
    /// <summary>Base64 of W x H little-endian RGB565 pixels, row by row.</summary>
    public string? Rgb565 { get; init; }
    /// <summary>Base64 of W x H alpha bytes, row by row (drawn text).</summary>
    public string? A8 { get; init; }

    /// <summary>No picture.</summary>
    public static KnobImage None { get; } = new();

    /// <summary>Converts 32-bit BGRA pixels (e.g. WPF's Bgra32 or Pbgra32), row by row, <paramref name="stride"/>
    /// bytes apart.</summary>
    public static KnobImage FromBgra32(byte[] pixels, int width, int height, int stride)
    {
        var rgb565 = new byte[width * height * 2];
        for (int y = 0; y < height; y++)
        {
            for (int x = 0; x < width; x++)
            {
                int i = y * stride + x * 4;
                int v = ((pixels[i + 2] >> 3) << 11) | ((pixels[i + 1] >> 2) << 5) | (pixels[i] >> 3);
                int o = (y * width + x) * 2;
                rgb565[o] = (byte)v;
                rgb565[o + 1] = (byte)(v >> 8);
            }
        }
        return new KnobImage { W = width, H = height, Rgb565 = Convert.ToBase64String(rgb565) };
    }

    /// <summary>Alpha-only pixels (e.g. text drawn white on transparent, keeping its alpha), row by row.</summary>
    public static KnobImage FromAlpha(byte[] alpha, int width, int height) =>
        new() { W = width, H = height, A8 = Convert.ToBase64String(alpha) };
}
