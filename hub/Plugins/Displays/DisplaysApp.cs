using KnobHub.Sdk;

namespace Displays.KnobApp;

/// <summary>
/// Switches the main display from the knob. Opening it shows the displays Windows shows the desktop on as cards: the
/// main one first, with a green tick, then the others left to right as they're arranged. Turning goes through them;
/// pressing makes the one on show the main display (where the Start menu, the taskbar's clock and new windows go), like
/// "Make this my main display" in Windows' Settings. The displays keep their places round it, and the new main one
/// moves to the front with the knob still on it. While the app is open it follows changes made elsewhere (a display
/// plugged in or out, Settings).
/// </summary>
public sealed class DisplaysApp : KnobHub.Sdk.KnobApp
{
    private const string Screen = "displays";
    private const string Accent = "#2DD4BF";
    private static readonly TimeSpan Refresh = TimeSpan.FromSeconds(2);

    private readonly object _lock = new();
    private Timer? _timer;
    private List<Display>? _shown;  // what the cards show, to update them only when something changed
    private bool _switching;

    public override void Open()
    {
        var displays = Read();
        lock (_lock)
        {
            _shown = displays;
            _timer ??= new Timer(_ => Poll(), null, Refresh, Refresh);
        }
        Host.Show(Screen, Describe(displays));
    }

    public override void Closed()
    {
        lock (_lock)
        {
            _timer?.Dispose();
            _timer = null;
        }
    }

    public override void Stop() => Closed();

    public override void Input(KnobAppInput input)
    {
        switch (input.Kind)
        {
            case InputKind.Back: Host.Close(); break;
            case InputKind.Press when input.Item != "": MakeMain(input.Item); break;
        }
    }

    private void MakeMain(string id)
    {
        lock (_lock)
        {
            if (_switching || _shown?.FirstOrDefault(d => d.Id == id) is not { IsMain: false }) return;
            _switching = true;
        }
        // Windows takes a moment to set the displays' modes again (the screens may go black briefly): off the UI thread.
        Task.Run(() =>
        {
            try { DisplayConfig.MakeMain(id); }
            catch (Exception ex)
            {
                Host.Log("Couldn't make it the main display: " + ex.Message);
                Host.Toast("Couldn't switch the main display");
            }
            lock (_lock) _switching = false;
            Show(Read());
        });
    }

    // Every 2 s while the app is open: displays plugged in or out, or the main one changed in Settings.
    private void Poll()
    {
        bool switching;
        List<Display>? shown;
        lock (_lock) (switching, shown) = (_switching, _shown);
        if (switching || !Host.IsOpen) return;
        var displays = Read();
        if (displays != null && shown != null && displays.SequenceEqual(shown)) return;
        Show(displays);
    }

    // The knob keeps the card on show (the tick moves to it after a switch).
    private void Show(List<Display>? displays)
    {
        lock (_lock) _shown = displays;
        if (Host.IsOpen) Host.Update(Screen, Describe(displays));
    }

    private List<Display>? Read()
    {
        try { return DisplayConfig.Active(); }
        catch (Exception ex)
        {
            Host.Log("Couldn't read the displays: " + ex.Message);
            return null;
        }
    }

    private static CardsScreen Describe(List<Display>? displays) => new()
    {
        Color = Accent,
        Items = displays?.Select(d => new Card(d.Id, d.Name, "monitor", Selected: d.IsMain)).ToList() ?? [],
        EmptyText = "Couldn't read the displays",
    };
}
