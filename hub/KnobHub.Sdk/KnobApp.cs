namespace KnobHub.Sdk;

/// <summary>
/// An app on the knob. Knob Hub creates one instance when the app is turned on and keeps it until the app is turned
/// off or Knob Hub exits. Knob Hub calls it on its UI thread; the app may call <see cref="Host"/> from any thread.
/// </summary>
/// <remarks>
/// A plugin is a folder with an <c>app.json</c> (id, name, subtitle, icon, color, version, author, description,
/// assembly, type) and the assembly with a public <see cref="KnobApp"/> subclass that has a parameterless
/// constructor. Package the folder as a zip renamed to <c>.knobapp</c> to install it from Knob Hub.
/// </remarks>
public abstract class KnobApp
{
    private IKnobHost? _host;

    /// <summary>How the app talks to the knob. Set before <see cref="Start"/>.</summary>
    protected IKnobHost Host => _host ?? throw new InvalidOperationException("The app hasn't been started yet.");

    /// <summary>Called by Knob Hub once, before anything else.</summary>
    public void Attach(IKnobHost host) => _host = host;

    /// <summary>The app was turned on (or Knob Hub started): connect to whatever it controls.</summary>
    public virtual void Start() { }

    /// <summary>The app was turned off or Knob Hub is exiting.</summary>
    public virtual void Stop() { }

    /// <summary>The app was opened on the knob: show its first screen with <see cref="IKnobHost.Show"/>.</summary>
    public abstract void Open();

    /// <summary>Something was done on one of the app's screens. "back" means a long press: usually a screen up,
    /// or <see cref="IKnobHost.Close"/> on the first screen.</summary>
    public virtual void Input(KnobAppInput input) { }

    /// <summary>The app left the knob's screen (closed, or another app was opened).</summary>
    public virtual void Closed() { }

    /// <summary>The app's settings, shown on its row on Knob Hub's Apps page and kept by Knob Hub. Read their values
    /// with <see cref="IKnobHost.Setting"/>.</summary>
    public virtual IReadOnlyList<AppSetting> Settings => [];

    /// <summary>A setting was changed on the Apps page.</summary>
    public virtual void SettingChanged(string id) { }

    /// <summary>The icons the app's screens use by name, beyond the ones Knob Hub has built in (music, play, game,
    /// headphones, speaker, settings...). The knob asks for an icon when it shows it, and Knob Hub draws it at that
    /// size. Names are shared by all apps, so make yours distinctive.</summary>
    public virtual IReadOnlyDictionary<string, KnobIcon> Icons { get; } = new Dictionary<string, KnobIcon>();
}

/// <summary>An icon for the knob. Knob Hub draws it at the size the knob shows it, fitted to a square, and sends only
/// its shape: the knob colours it (like text).</summary>
public abstract record KnobIcon
{
    /// <summary>A character of an icon font, e.g. Segoe Fluent Icons' "" (a game controller).</summary>
    public static KnobIcon Glyph(string text, string font = "Segoe Fluent Icons") => new GlyphIcon(text, font);

    /// <summary>A picture (e.g. a PNG's bytes): its alpha is the shape. Make it square and at least 64 x 64.</summary>
    public static KnobIcon Image(byte[] data) => new ImageIcon(data);

    /// <summary>A vector shape in SVG/WPF path syntax, e.g. "M2,2 L22,12 L2,22 Z"; filled.</summary>
    public static KnobIcon Path(string data) => new PathIcon(data);
}

public sealed record GlyphIcon(string Text, string Font) : KnobIcon;
public sealed record ImageIcon(byte[] Data) : KnobIcon;
public sealed record PathIcon(string Data) : KnobIcon;

/// <summary>A setting on the Apps page: a choice of <paramref name="Options"/> (a drop-down).</summary>
/// <param name="Id">The app's own name for it, passed to <see cref="IKnobHost.Setting"/>.</param>
/// <param name="Label">Shown next to the drop-down.</param>
/// <param name="Default">The option chosen until it's changed (an index into <paramref name="Options"/>).</param>
public sealed record AppSetting(string Id, string Label, IReadOnlyList<string> Options, int Default = 0);

/// <summary>What an app can do on the knob.</summary>
public interface IKnobHost
{
    /// <summary>The app is on the knob's screen.</summary>
    bool IsOpen { get; }

    /// <summary>Shows a screen. <paramref name="screen"/> is the app's own name for it; it comes back in
    /// <see cref="KnobAppInput.Screen"/> and is how <see cref="Update"/> finds it.</summary>
    void Show(string screen, KnobScreen content);

    /// <summary>Changes parts of the screen on show without rebuilding it, e.g. <c>new { value = 40 }</c> on a
    /// dial. Property names are written in snake_case. Ignored if that screen isn't on show.</summary>
    void Update(string screen, object changes);

    /// <summary>Closes the app: the knob goes back to Home.</summary>
    void Close();

    /// <summary>A short message over whatever the knob shows.</summary>
    void Toast(string text);

    /// <summary>Writes to Knob Hub's log.</summary>
    void Log(string message);

    /// <summary>One of the app's <see cref="KnobApp.Settings"/>: the chosen option's index.</summary>
    int Setting(string id);

    /// <summary>The knob's round screen, its diameter in pixels: 240 on the 1.28" CrowPanel, 360 on the 1.46" (480
    /// with no knob connected: Knob Hub's preview's). Knob OS scales screens to it by itself; this is only for pictures an app
    /// draws (sized for 240, times <c>ScreenSize / 240.0</c>).</summary>
    int ScreenSize { get; }
}

/// <summary>Something done on one of the app's screens.</summary>
/// <param name="Screen">The screen's name, as given to <see cref="IKnobHost.Show"/>.</param>
/// <param name="Kind">One of <see cref="InputKind"/>'s values.</param>
/// <param name="Item">The item's id (a tile, list row, pill or button), or "".</param>
/// <param name="Delta">+1/-1 per step for <see cref="InputKind.Rotate"/>; for a toggle pill 1 = on, 0 = off; for a
/// segment pill the new option's index.</param>
public sealed record KnobAppInput(string Screen, string Kind, string Item, int Delta);

/// <summary>The kinds of <see cref="KnobAppInput"/>.</summary>
public static class InputKind
{
    /// <summary>Pressed the knob (or tapped) on an item.</summary>
    public const string Press = "press";
    /// <summary>Tapped a control such as a pill or button.</summary>
    public const string Tap = "tap";
    /// <summary>Turned the knob where the turn is the value (a dial).</summary>
    public const string Rotate = "rotate";
    /// <summary>Picked an option, on a <see cref="ControlsScreen"/>'s picker; the item is its id.</summary>
    public const string Select = "select";
    /// <summary>Long press.</summary>
    public const string Back = "back";
}
