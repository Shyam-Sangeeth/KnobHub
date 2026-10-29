using System.Text.Json;
using System.Text.Json.Nodes;
using System.Windows.Threading;
using KnobHub.Apps;
using KnobHub.Sdk;

namespace KnobHub.Core;

/// <summary>
/// Knob Hub's side of the link (see knob-os/src/core/link.h for the messages). It talks to the real knob when one is
/// connected and to the preview otherwise. While a real knob is connected the preview mirrors it: it gets every
/// message the knob gets, plus the knob's own actions ("key" messages) replayed, and what it sends is ignored.
/// </summary>
internal sealed class KnobSession : IDisposable
{
    private readonly IKnobConnection _knob;
    private readonly HubSettings _settings;
    private AppEntry? _open;              // the app on the knob's screen

    public KnobSession(HubSettings settings, IKnobConnection knob, Dispatcher ui)
    {
        _settings = settings;
        _knob = knob;
        Preview = new KnobPreview();
        Apps = new AppManager(settings, entry => new AppHost(entry, this, ui));
    }

    public KnobPreview Preview { get; }
    public AppManager Apps { get; }
    public KnobSettings KnobSettings => _settings.Knob;
    public HubSettings HubSettings => _settings;

    /// <summary>A real knob is connected (otherwise the preview is the knob).</summary>
    public bool KnobConnected => _knob.IsConnected;
    public string ConnectionKind => _knob.Kind;
    /// <summary>The Knob OS version of the knob in use (real or preview).</summary>
    public string OsVersion { get; private set; } = "";

    /// <summary>The board of the knob in use: the real knob's (from its hello), or the one the preview is.</summary>
    public KnobBoard Board => _knob.IsConnected ? _knobBoard : Preview.Board;
    private KnobBoard _knobBoard = KnobBoard.Default;

    /// <summary>The screen size of the knob in use, which apps draw their pictures for: the connected knob's, else the
    /// preview's (always 480 px).</summary>
    public int ScreenSize => _knob.IsConnected ? _knobBoard.Size : Preview.Size;

    /// <summary>The connection or the knob's details changed.</summary>
    public event Action? StateChanged;
    /// <summary>The knob changed its settings (on the knob itself).</summary>
    public event Action? SettingsChanged;
    public event Action<string>? Logged;

    public void Start(IEnumerable<(AppManifest, Func<KnobApp>)> builtInApps)
    {
        Apps.Load(builtInApps);
        Apps.Changed += SendApps;

        Preview.Sent += OnPreviewLine;
        StartPreview(KnobBoard.Find(_settings.PreviewBoard));

        _knob.LineReceived += OnKnobLine;
        _knob.ConnectedChanged += OnKnobConnected;
        _knob.Start();
    }

    // --- Knob Hub -> knob ---

    /// <summary>Sends to the knob in use; the preview always gets it (as the knob, or to mirror it).</summary>
    private void Send(JsonObject msg)
    {
        KnobText.FitAll(msg);
        MarkPictures(msg, ScreenSize);
        string line = msg.ToJsonString(Json.Link);
        if (_knob.IsConnected) _knob.Send(line);
        Preview.LinkLine(line);
    }

    /// <summary>Adds the screen size pictures (album art, drawn text) were drawn for, so a knob of another size scales
    /// them: the preview mirroring a bigger knob.</summary>
    private static void MarkPictures(JsonNode? node, int screen)
    {
        switch (node)
        {
            case JsonObject obj:
                if (obj.ContainsKey("w") && obj.ContainsKey("h") && (obj.ContainsKey("rgb565") || obj.ContainsKey("a8")))
                    obj["screen"] = screen;
                foreach (var (_, value) in obj.ToList()) MarkPictures(value, screen);
                break;
            case JsonArray array:
                foreach (var item in array) MarkPictures(item, screen);
                break;
        }
    }

    /// <summary>A touch position (x or y) on one screen size moved to another: the knob's and the preview's differ
    /// when the knob is bigger.</summary>
    private static int Rescale(int at, int from, int to) =>
        from == to ? at : Math.Clamp((int)((at + 0.5) * to / from), 0, to - 1);

    private void SendApps()
    {
        var items = new JsonArray();
        foreach (var entry in Apps.OnKnob)
        {
            var m = entry.Manifest;
            items.Add(new JsonObject { ["id"] = m.Id, ["name"] = m.Name, ["subtitle"] = m.Subtitle, ["icon"] = m.Icon, ["color"] = m.Color });
        }
        // The order is the whole Home ring's, Clock and Settings included.
        var order = new JsonArray(Apps.RingOrder.Select(id => (JsonNode)JsonValue.Create(id)).ToArray());
        Send(new JsonObject { ["t"] = "apps", ["items"] = items, ["order"] = order });
        SendIconList();
        // The open app was turned off: Knob OS closes it by itself when it's gone from the list.
        if (_open != null && !Apps.OnKnob.Contains(_open)) Closed(_open);
    }

    public void SendSettings()
    {
        _settings.Save();
        Send(_settings.Knob.ToMessage());
    }

    /// <summary>A knob action done in the preview: 0 turn right, 1 turn left, 2 press, 3 back. With a real knob
    /// connected it's done on the knob, which reports it back and so moves the preview.</summary>
    public void PreviewInput(int input)
    {
        if (_knob.IsConnected) _knob.Send(new JsonObject { ["t"] = "key", ["input"] = InputNames[input] }.ToJsonString());
        else Preview.Input(input);
    }

    private static readonly string[] InputNames = ["right", "left", "press", "back"];

    /// <summary>Starts the preview as the board (again, if it was another): it says hello and gets everything
    /// again.</summary>
    private void StartPreview(KnobBoard board)
    {
        if (Preview.Start(board)) Preview.LinkConnected(true);
        else Log(Preview.Error!);
    }

    /// <summary>A touch on the preview at its screen pixels (0 to 479). With a real knob connected it's done on the
    /// knob (at its own screen's pixels), which reports it back.</summary>
    public void PreviewTouch(int x, int y, bool down)
    {
        if (_knob.IsConnected)
        {
            int size = _knobBoard.Size;
            _knob.Send(new JsonObject
            {
                ["t"] = "touch", ["x"] = Rescale(x, Preview.Size, size), ["y"] = Rescale(y, Preview.Size, size), ["down"] = down,
            }.ToJsonString());
        }
        else Preview.Touch(x, y, down);
    }

    // --- Apps -> knob (through AppHost) ---

    public void ShowScreen(AppEntry entry, string screen, KnobScreen content)
    {
        if (_open != entry) return;
        Send(new JsonObject { ["t"] = "screen", ["app"] = entry.Id, ["screen"] = screen, ["template"] = content.Template, ["data"] = Json.ToNode(content) });
    }

    public void UpdateScreen(AppEntry entry, string screen, object changes)
    {
        if (_open != entry) return;
        Send(new JsonObject { ["t"] = "update", ["app"] = entry.Id, ["screen"] = screen, ["data"] = Json.ToNode(changes) });
    }

    public void CloseApp(AppEntry entry)
    {
        if (_open != entry) return;
        Send(new JsonObject { ["t"] = "close", ["app"] = entry.Id });
        Closed(entry);
    }

    public void Toast(string text) => Send(new JsonObject { ["t"] = "toast", ["text"] = text });

    public void Log(string message) => Logged?.Invoke(message);

    // --- knob -> Knob Hub ---

    private void OnKnobConnected(bool connected)
    {
        Log(connected ? $"Knob connected ({_knob.Kind})" : "Knob disconnected: the preview is the knob now");
        // Disconnected: the preview already shows what the knob showed and takes over from there, timing its own screen
        // timeout again.
        if (!connected) Preview.LinkLine(new JsonObject { ["t"] = "idle", ["follow"] = false }.ToJsonString());
        StateChanged?.Invoke();
    }

    private void OnKnobLine(string line)
    {
        if (Parse(line) is not { } msg) return;
        switch (msg.Str("t"))
        {
            case "key":
                int input = Array.IndexOf(InputNames, msg.Str("input"));
                if (input >= 0) Preview.Input(input);
                return;
            case "touch":
                Preview.Touch(Rescale(msg.Int("x"), _knobBoard.Size, Preview.Size), Rescale(msg.Int("y"), _knobBoard.Size, Preview.Size),
                    msg["down"] is JsonValue d && d.TryGetValue(out bool down) && down);
                return;
            case "icon_request":
                SendIcon(msg, toKnob: true);
                return;
            case "idle":
                // The preview follows the knob's screen state, so an input that only wakes the knob doesn't act on it.
                Preview.LinkLine(new JsonObject { ["t"] = "idle", ["state"] = msg.Str("state"), ["follow"] = true }.ToJsonString());
                return;
        }
        Handle(msg, fromKnob: true);
    }

    private void OnPreviewLine(string line)
    {
        if (Parse(line) is not { } msg) return;
        // The preview draws what the knob shows, so it needs the icons even while it's only mirroring.
        if (msg.Str("t") == "icon_request")
        {
            SendIcon(msg, toKnob: false);
            return;
        }
        if (_knob.IsConnected) return;  // only mirroring
        if (msg.Str("t") is "key" or "touch") return;
        Handle(msg, fromKnob: false);
    }

    // --- Icons (knob-os/src/core/icons.h) ---

    private readonly Dictionary<(KnobIcon Icon, int Size), byte[]> _drawnIcons = [];

    /// <summary>Every icon Knob Hub can draw now, with its fingerprint: the knob drops kept icons that aren't listed
    /// (their app was removed or turned off) and fetches again the ones whose fingerprint changed.</summary>
    private void SendIconList()
    {
        var known = new JsonObject();
        foreach (var (name, icon) in KnobIcons.BuiltIn) known[name] = KnobIcons.Hash(icon);
        // An app's own icon wins over a built-in one, and an earlier app's over a later one's (as in AppIcon).
        foreach (var entry in Apps.Entries.Reverse())
        {
            try
            {
                if (entry.App is { } app)
                    foreach (var (name, icon) in app.Icons) known[name] = KnobIcons.Hash(icon);
            }
            catch (Exception) { }  // a broken plugin doesn't stop the others' icons
        }
        Send(new JsonObject { ["t"] = "icons", ["known"] = known });
    }

    /// <summary>Answers an icon request with the icon drawn at the size asked for: a running app's own icon of that
    /// name, else Knob Hub's built-in one. To the knob (and the preview, which mirrors it) or only to the preview.</summary>
    private void SendIcon(JsonObject request, bool toKnob)
    {
        string name = request.Str("name");
        int size = request.Int("size");
        if (size is < 1 or > 128) return;
        var icon = AppIcon(name) ?? KnobIcons.BuiltIn.GetValueOrDefault(name);
        if (icon == null)
        {
            Log($"No icon called \"{name}\" (an app's screen uses it)");
            return;
        }
        if (!_drawnIcons.TryGetValue((icon, size), out var alpha))
        {
            try { alpha = KnobIcons.Draw(icon, size); }
            catch (Exception ex)
            {
                Log($"Couldn't draw the icon \"{name}\": {ex.Message}");
                return;
            }
            _drawnIcons[(icon, size)] = alpha;
        }
        string line = new JsonObject
        {
            ["t"] = "icon", ["name"] = name, ["size"] = size, ["w"] = size, ["h"] = size, ["hash"] = KnobIcons.Hash(icon),
            ["a8"] = Convert.ToBase64String(alpha),
        }.ToJsonString(Json.Link);
        if (toKnob && _knob.IsConnected) _knob.Send(line);
        Preview.LinkLine(line);
    }

    private KnobIcon? AppIcon(string name)
    {
        foreach (var entry in Apps.Entries)
        {
            try
            {
                if (entry.App?.Icons.TryGetValue(name, out var icon) == true) return icon;
            }
            catch (Exception) { }  // a broken plugin doesn't stop the others' icons
        }
        return null;
    }

    private void HandleHello(JsonObject msg, bool fromKnob)
    {
        OsVersion = msg.Str("os");
        if (fromKnob)
        {
            // The preview mirrors the knob, so it becomes the same board (starting again if it wasn't; its screen
            // stays 480 px), and stays it when the knob goes.
            _knobBoard = KnobBoard.FromHello(msg);
            Log($"The knob is a {_knobBoard.Name} ({_knobBoard.Size} px)");
            if (KnobBoard.Known.Contains(_knobBoard) && _knobBoard.Id != _settings.PreviewBoard)
            {
                _settings.PreviewBoard = _knobBoard.Id;
                _settings.Save();
            }
            if (_knobBoard.Id != Preview.Board.Id) StartPreview(KnobBoard.Find(_knobBoard.Id));
            // Bring the preview to where the knob is; the knob reopens its app by itself.
            Preview.LinkLine(new JsonObject { ["t"] = "sync", ["page"] = msg.Str("page"), ["app"] = msg.Str("app") }.ToJsonString());
        }
        // After a reconnect, an app Knob Hub still thinks is open may not be on the knob any more: close it here. (An
        // app that is still open on the knob asks to be opened again by itself.) The preview only counts when it's
        // the knob, i.e. no real knob is connected.
        if ((fromKnob || !_knob.IsConnected) && _open != null && _open.Id != msg.Str("app")) Closed(_open);
        SendApps();
        SendSettings();
        StateChanged?.Invoke();
    }

    private void Handle(JsonObject msg, bool fromKnob)
    {
        switch (msg.Str("t"))
        {
            case "hello":
                HandleHello(msg, fromKnob);
                break;

            case "settings":
                _settings.Knob.Apply(msg);
                _settings.Save();
                if (fromKnob) Preview.LinkLine(_settings.Knob.ToMessage().ToJsonString(Json.Link));
                SettingsChanged?.Invoke();
                StateChanged?.Invoke();
                break;

            case "open":
                if (fromKnob)
                {
                    // The preview normally opened it too (it replays the knob's actions), but make sure: replayed
                    // touches can land differently, and an app screen only shows on a knob that has the app open.
                    Preview.LinkLine(new JsonObject { ["t"] = "sync", ["page"] = msg.Str("app"), ["app"] = msg.Str("app") }.ToJsonString());
                }
                Log($"Opened {msg.Str("app")}");
                Open(msg.Str("app"));
                break;

            case "input":
                if (_open?.App is { } app && _open.Id == msg.Str("app"))
                {
                    var input = new KnobAppInput(msg.Str("screen"), msg.Str("kind"), msg.Str("item"), msg.Int("delta"));
                    Guard(_open, () => app.Input(input));
                }
                break;
        }
    }

    private void Open(string id)
    {
        if (_open != null) Closed(_open);
        var entry = Apps.Find(id);
        if (entry?.App is not { } app || !entry.Enabled)
        {
            Send(new JsonObject { ["t"] = "toast", ["text"] = "That app isn't available" });
            Send(new JsonObject { ["t"] = "close", ["app"] = id });
            return;
        }
        _open = entry;
        entry.Host!.SetOpen(true);
        Guard(entry, app.Open);
    }

    private void Closed(AppEntry entry)
    {
        if (_open != entry) return;
        _open = null;
        entry.Host?.SetOpen(false);
        if (entry.App is { } app) Guard(entry, app.Closed);
    }

    /// <summary>An app's exception never takes Knob Hub down: it's logged and the knob says so.</summary>
    private void Guard(AppEntry entry, Action action)
    {
        try { action(); }
        catch (Exception ex)
        {
            Log($"{entry.Manifest.Name} failed: {ex}");
            Toast($"{entry.Manifest.Name} ran into a problem");
        }
    }

    private static JsonObject? Parse(string line)
    {
        try { return JsonNode.Parse(line) as JsonObject; }
        catch (JsonException) { return null; }
    }

    public void Dispose()
    {
        Apps.StopAll();
        _knob.Dispose();
        Preview.Dispose();
    }
}
