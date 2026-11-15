using KnobHub.Sdk;

namespace Screen.KnobApp;

/// <summary>
/// The PC's screens off and on from the knob: a ring with ON or OFF in the middle. Pressing turns them off, as Windows'
/// screen timeout does, and pressing again turns them back on; turning the knob does nothing. It follows the screens
/// whoever changes them (the mouse waking them, the timeout turning them off).
/// </summary>
public sealed class ScreenApp : KnobHub.Sdk.KnobApp
{
    private const string Screen = "screen";
    private const string OnColor = "#60A5FA";   // the app's blue: the screens are on
    private const string OffColor = "#5B6472";  // grey while they're off

    private readonly object sync = new();
    private DisplayPower? power;
    private bool waiting;        // a switch was asked for and Windows hasn't said it happened yet
    private Timer? waitLimit;    // gives up waiting after a while, so the spinner never stays

    public override void Start()
    {
        power = new DisplayPower();
        power.Changed += StateChanged;
    }

    public override void Stop()
    {
        if (power != null)
        {
            power.Changed -= StateChanged;
            power.Dispose();
            power = null;
        }
        lock (sync) waitLimit?.Dispose();
    }

    public override void Open() => Host.Show(Screen, Describe());

    public override void Input(KnobAppInput input)
    {
        switch (input.Kind)
        {
            case InputKind.Back:
                Host.Close();
                break;
            case InputKind.Press:
            case InputKind.Tap:
                Switch();
                break;
            // Turning does nothing: only a press switches the screens.
        }
    }

    private void Switch()
    {
        if (power == null) return;
        var on = power.IsOn;
        lock (sync)
        {
            if (waiting) return;
            waiting = true;
            waitLimit?.Dispose();
            waitLimit = new Timer(_ => StopWaiting(), null, TimeSpan.FromSeconds(5), Timeout.InfiniteTimeSpan);
        }
        if (Host.IsOpen) Host.Update(Screen, new { busy = true });
        if (on) DisplayPower.TurnOff();
        else DisplayPower.TurnOn();
    }

    private void StopWaiting()
    {
        lock (sync)
        {
            if (!waiting) return;
            waiting = false;
        }
        if (Host.IsOpen) Host.Update(Screen, Describe());
    }

    // Windows says the screens went off or came on: the knob follows.
    private void StateChanged()
    {
        lock (sync) waiting = false;
        if (Host.IsOpen) Host.Update(Screen, Describe());
    }

    private RingScreen Describe()
    {
        bool busy;
        lock (sync) busy = waiting;
        var on = power?.IsOn ?? true;
        return new RingScreen
        {
            Label = "SCREENS",
            Text = on ? "ON" : "OFF",
            Value = on ? 100 : 0,
            Color = on ? OnColor : OffColor,
            Busy = busy,
        };
    }
}
