using System.Diagnostics;
using KnobHub.Sdk;
using Microsoft.Win32;

namespace AudioDesk.KnobApp;

/// <summary>
/// AudioDesk on the knob. Opening it shows the channels (Game, Media, Aux) with Sonar on/off and Headset | Home
/// Theater; pressing a channel opens its controls: volume, its own Sonar on/off and its output device (the knob's
/// press moves between them, turning changes them; tapping the icon mutes). Everything goes through AudioDesk, which
/// has to be running on the PC; if it stops, the app waits and comes back to the same screen once it's running again.
/// </summary>
public sealed class AudioDeskApp : KnobHub.Sdk.KnobApp
{
    private const string Channels = "channels", Offline = "offline";
    private const string VolumePrefix = "volume:";
    private const string VolumeStepSetting = "volume_step";
    private static readonly int[] VolumeSteps = [1, 2, 5, 10];  // percent per click of the knob, as the knob shows it
    private static readonly TimeSpan TurnWindow = TimeSpan.FromMilliseconds(800);
    private static readonly string[] ChannelIds = ["game", "media", "aux"];

    private readonly AudioDeskClient _client = new();
    private string _screen = "";  // the screen on show
    private string? _resume;      // the screen on show when AudioDesk went away: shown again once it's back
    private double _volumeTarget;  // quick turns build on the last target, not on AudioDesk's slightly older state
    private DateTime _lastTurn;

    public override void Start()
    {
        _client.Changed += OnChanged;
        _client.Error += text => { if (Host.IsOpen) Host.Toast(text); };
        _client.Start();
    }

    public override void Stop() => _client.Dispose();

    public override void Open() => Show(_client.Connected ? Channels : Offline);

    public override void Closed()
    {
        _screen = "";
        _resume = null;
    }

    // The channels' icons and Sonar's logo (Knob Hub has "music" built in).
    public override IReadOnlyDictionary<string, KnobIcon> Icons { get; } = new Dictionary<string, KnobIcon>
    {
        ["audiodesk.game"] = KnobIcon.Image(Resource("game")),
        ["audiodesk.aux"] = KnobIcon.Image(Resource("aux-cable")),
        ["audiodesk.sonar"] = KnobIcon.Image(Resource("sonar")),
    };

    private static byte[] Resource(string name)
    {
        using var stream = typeof(AudioDeskApp).Assembly.GetManifestResourceStream($"Icons.{name}.png")
                           ?? throw new InvalidOperationException($"The {name} icon is missing.");
        using var bytes = new MemoryStream();
        stream.CopyTo(bytes);
        return bytes.ToArray();
    }

    private const string SonarIcon = "audiodesk.sonar";
    private const string SonarOn = "#4ADE80", SonarOff = "#F87171";  // Sonar's logo: green on, red off
    private const string Accent = "#8B7CF6";

    public override IReadOnlyList<AppSetting> Settings { get; } =
        [new AppSetting(VolumeStepSetting, "Volume step", VolumeSteps.Select(s => $"{s}%").ToList(), Default: 1)];

    public override void SettingChanged(string id)
    {
        if (id == VolumeStepSetting && _screen.StartsWith(VolumePrefix)) Host.Update(_screen, new { step = StepPercent });
    }

    private int StepPercent => VolumeSteps[Math.Clamp(Host.Setting(VolumeStepSetting), 0, VolumeSteps.Length - 1)];

    public override void Input(KnobAppInput input)
    {
        var state = _client.State;
        if (_screen == Offline || state == null)
        {
            if (input.Kind == InputKind.Back) Host.Close();
            else if (input.Kind == InputKind.Press && input.Item == "start") StartAudioDesk();
            return;
        }

        if (_screen == Channels)
        {
            switch (input.Kind, input.Item)
            {
                case (InputKind.Back, _): Host.Close(); break;
                case (InputKind.Press, var channel) when ChannelIds.Contains(channel): Show(VolumePrefix + channel); break;
                case (InputKind.Tap, "sonar"): _client.SetSonar(input.Delta == 1); break;
                case (InputKind.Tap, "output"): _client.SetProfile(input.Delta == 1 ? "home_theater" : "headset"); break;
            }
            return;
        }

        if (_screen.StartsWith(VolumePrefix))
        {
            string id = _screen[VolumePrefix.Length..];
            if (state.Channel(id) is not { } channel) return;
            switch (input.Kind, input.Item)
            {
                case (InputKind.Back, _): Show(Channels); break;
                case (InputKind.Rotate, _):
                    double from = DateTime.UtcNow - _lastTurn < TurnWindow ? _volumeTarget : channel.Volume;
                    _volumeTarget = Math.Clamp(from + input.Delta * StepPercent / 100.0, 0, 1);
                    _lastTurn = DateTime.UtcNow;
                    _client.SetVolume(id, _volumeTarget);
                    if (channel.Muted) _client.SetMuted(id, false);  // like turning a volume knob up from mute
                    break;
                case (InputKind.Press, _): _client.SetMuted(id, !channel.Muted); break;
                case (InputKind.Tap, "sonar"): _client.SetChannelSonar(id, input.Delta == 1); break;
                case (InputKind.Select, var device) when device != channel.Device: _client.SetDevice(id, device); break;
            }
        }
    }

    // --- Screens ---

    private void Show(string screen)
    {
        _screen = screen;
        var state = _client.State;
        if (state == null || screen == Offline)
        {
            _screen = Offline;
            Host.Show(Offline, OfflineScreen());
        }
        else if (screen.StartsWith(VolumePrefix) && state.Channel(screen[VolumePrefix.Length..]) is { } channel) Host.Show(screen, ChannelControls(channel));
        else
        {
            _screen = Channels;  // also when a channel's screen was asked for and AudioDesk no longer has that channel
            Host.Show(Channels, ChannelsScreen(state));
        }
    }

    // AudioDesk reported a change (from a background thread): bring the screen on show up to date.
    private void OnChanged()
    {
        if (!Host.IsOpen) return;
        var state = _client.State;
        if (state == null)
        {
            if (_screen == Offline) return;
            _resume = _screen;
            Show(Offline);
            return;
        }
        if (_screen == Offline)
        {
            Show(_resume ?? Channels);
            _resume = null;
        }
        else if (_screen == Channels) Host.Update(Channels, ChannelsScreen(state));
        else if (_screen.StartsWith(VolumePrefix) && state.Channel(_screen[VolumePrefix.Length..]) is { } channel)
        {
            var controls = ChannelControls(channel);
            // While the knob is turning it shows its own value; AudioDesk catches up.
            bool turning = DateTime.UtcNow - _lastTurn < TurnWindow;
            Host.Update(_screen, turning
                ? new { controls.Muted, controls.Toggle, controls.Picker }
                : new { controls.Value, controls.Muted, controls.Toggle, controls.Picker });
        }
    }

    private static TilesScreen ChannelsScreen(AudioDeskState state)
    {
        var screen = new TilesScreen { Title = "AUDIODESK", Color = Accent, Selected = "media" };
        foreach (var channel in state.Channels)
        {
            var (label, icon, color) = Look(channel.Id);
            // The ring is the volume (spinning while the channel switches device); inside, the icon (crossed out when
            // muted) and the device it plays on (Sonar's output while it goes through Sonar); Sonar's badge at the bottom.
            // No text under the circle.
            string? badge = channel.ThroughSonar ? SonarIcon : null;
            screen.Items.Add(new Tile(channel.Id, label, icon, color, channel.Muted ? 0 : Percent(channel.Volume), null, badge,
                                      Short(channel.DeviceName, 60), channel.Switching, channel.Muted));
        }
        if (state.SonarAvailable) screen.Pills.Add(Pill.Toggle("sonar", "Sonar", state.Sonar, SonarIcon, SonarOn, SonarOff));
        screen.Pills.Add(Pill.Segment("output", ["Headset", "Home Theater"], state.Profile switch
        {
            "headset" => 0,
            "home_theater" => 1,
            _ => -1,  // a custom layout: neither
        }));
        return screen;
    }

    // The volume, the channel's own Sonar on/off (the toggle; hidden where Sonar can't take the channel) and its output
    // device (the picker; while it goes through Sonar, the devices Sonar can play it on).
    private ControlsScreen ChannelControls(ChannelState channel)
    {
        var (label, icon, color) = Look(channel.Id);
        string? via = channel.ThroughSonar ? "Sonar" : null;
        return new ControlsScreen
        {
            Label = label.ToUpperInvariant(),
            Icon = icon,
            Color = color,
            Value = Percent(channel.Volume),
            Step = StepPercent,
            Muted = channel.Muted,
            Toggle = new ControlToggle("sonar", "Sonar", channel.ThroughSonar, SonarIcon, SonarOn, SonarOff, Hidden: !channel.SonarAvailable),
            Picker = new ControlPicker(channel.Device, channel.Devices
                .Select(d => new PickerOption(d.Id, d.Name, channel.Switching && d.Id == channel.Device
                    ? "Switching..."
                    : string.Join(" Â· ", new[] { via, d.Format }.Where(p => !string.IsNullOrEmpty(p)))))
                .ToList(), EmptyText: "No device"),
        };
    }

    // A Start button when AudioDesk can be found on this PC; otherwise just a hint (a button would do nothing).
    private ListScreen OfflineScreen() => AudioDeskPath() != null
        ? new() { Title = "AUDIODESK", Note = "AudioDesk isn't running", Items = [new ListRow("start", "Start AudioDesk", "On this PC", "play")] }
        : new() { Title = "AUDIODESK", Note = "AudioDesk isn't running", Hint = "Open it on your PC" };

    private static (string Label, string Icon, string Color) Look(string channel) => channel switch
    {
        "game" => ("Game", "audiodesk.game", "#4CC2FF"),
        "media" => ("Media", "music", "#FF9F43"),
        _ => ("Aux", "audiodesk.aux", "#C084FC"),
    };

    private static int Percent(double volume) => (int)Math.Round(volume * 100);

    /// <summary>"Speakers (Realtek USB Audio)" -> "Speakers": the knob has little room.</summary>
    private static string Short(string name, int max = 24)
    {
        int bracket = name.IndexOf(" (", StringComparison.Ordinal);
        if (bracket > 0) name = name[..bracket];
        return name.Length <= max ? name : name[..(max - 1)] + "â€¦";
    }

    // --- Starting AudioDesk ---

    /// <summary>AudioDesk's exe: its Start with Windows entry, or where its installer puts it.</summary>
    private static string? AudioDeskPath()
    {
        try
        {
            using var run = Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Run");
            if (run?.GetValue("AudioDesk") is string command)
            {
                string exe = command.StartsWith('"') ? command[1..command.IndexOf('"', 1)] : command.Split(' ')[0];
                if (File.Exists(exe)) return exe;
            }
        }
        catch (Exception) { }
        string installed = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Programs", "AudioDesk", "AudioDesk.exe");
        return File.Exists(installed) ? installed : null;
    }

    private void StartAudioDesk()
    {
        if (AudioDeskPath() is not { } exe) return;
        try
        {
            Process.Start(new ProcessStartInfo(exe, "--minimized") { UseShellExecute = true });
            Host.Toast("Starting AudioDeskâ€¦");
        }
        catch (Exception ex) { Host.Log("Couldn't start AudioDesk: " + ex.Message); }
    }
}
