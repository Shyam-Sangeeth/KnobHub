using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;

namespace KnobHub.Core;

/// <summary>
/// Knob OS running inside Knob Hub (knob_preview.dll, built from the same code as the knob's firmware). It draws the
/// live preview: fed the same messages as the real knob plus the real knob's actions, it shows what the knob shows.
/// With no knob connected it is the knob.
/// </summary>
internal sealed class KnobPreview : IDisposable
{
    public const int Size = 240;
    private const string Dll = "knob_preview.dll";

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    private delegate void SendCallback(IntPtr line);

    [DllImport(Dll)] private static extern void kp_start(SendCallback send);
    [DllImport(Dll)] private static extern void kp_tick();
    [DllImport(Dll)] private static extern void kp_input(int input);
    [DllImport(Dll)] private static extern void kp_touch(int x, int y, int down);
    [DllImport(Dll)] private static extern void kp_link_connected(int connected);
    [DllImport(Dll)] private static extern void kp_link_line(byte[] line);
    [DllImport(Dll)] private static extern IntPtr kp_frame(out uint serial);
    [DllImport(Dll)] private static extern int kp_brightness();

    private readonly DispatcherTimer _timer = new(DispatcherPriority.Render) { Interval = TimeSpan.FromMilliseconds(16) };
    private readonly SendCallback _send;  // kept alive: the DLL holds on to it
    private uint _shownSerial = uint.MaxValue;
    private bool _started;

    public KnobPreview()
    {
        _send = OnSend;
        _timer.Tick += (_, _) => Tick();
    }

    /// <summary>The knob's screen, updated as it redraws.</summary>
    public WriteableBitmap Screen { get; } = new(Size, Size, 96, 96, PixelFormats.Bgr32, null);

    /// <summary>Why the preview isn't running (knob_preview.dll missing), or null.</summary>
    public string? Error { get; private set; }

    /// <summary>The screen brightness Knob OS is set to (5-100).</summary>
    public int Brightness { get; private set; } = 100;

    /// <summary>A message the preview knob sends to Knob Hub. Raised after the call that produced it returns,
    /// so handlers can call back into the preview.</summary>
    public event Action<string>? Sent;

    public event Action? BrightnessChanged;

    public bool Start()
    {
        if (!File.Exists(Path.Combine(AppContext.BaseDirectory, "knob_preview.dll")))
        {
            Error = "knob_preview.dll is missing: build it in knob-os with pio run -e preview, then rebuild Knob Hub.";
            return false;
        }
        try
        {
            kp_start(_send);
        }
        catch (Exception ex) when (ex is DllNotFoundException or BadImageFormatException or EntryPointNotFoundException)
        {
            Error = "The preview couldn't start: " + ex.Message;
            return false;
        }
        _started = true;
        _timer.Start();
        Tick();
        return true;
    }

    /// <summary>0 turn right, 1 turn left, 2 press, 3 back.</summary>
    public void Input(int input)
    {
        if (_started) kp_input(input);
    }

    /// <summary>A touch at screen pixels (0-239): down while the finger (mouse button) is on the screen.</summary>
    public void Touch(int x, int y, bool down)
    {
        if (_started) kp_touch(x, y, down ? 1 : 0);
    }

    public void LinkConnected(bool connected)
    {
        if (_started) kp_link_connected(connected ? 1 : 0);
    }

    public void LinkLine(string line)
    {
        if (!_started) return;
        byte[] bytes = new byte[Encoding.UTF8.GetByteCount(line) + 1];
        Encoding.UTF8.GetBytes(line, bytes);
        kp_link_line(bytes);
    }

    private void Tick()
    {
        kp_tick();
        IntPtr frame = kp_frame(out uint serial);
        if (serial != _shownSerial)
        {
            _shownSerial = serial;
            Screen.WritePixels(new Int32Rect(0, 0, Size, Size), frame, Size * Size * 4, Size * 4);
        }
        int brightness = kp_brightness();
        if (brightness != Brightness)
        {
            Brightness = brightness;
            BrightnessChanged?.Invoke();
        }
    }

    private void OnSend(IntPtr line)
    {
        string text = Marshal.PtrToStringUTF8(line) ?? "";
        // Called from inside Knob OS: handle it once Knob OS is done with whatever it was doing.
        Dispatcher.CurrentDispatcher.BeginInvoke(() => Sent?.Invoke(text));
    }

    public void Dispose() => _timer.Stop();
}
