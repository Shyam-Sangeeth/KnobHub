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
/// <para>It pretends to be one board (see <see cref="KnobBoard"/>), but with a 480 px screen whatever the board's: only
/// the rest of the board (LEDs, motors, so its settings) is the board's. Knob OS starts only once per DLL, so a
/// different board loads a fresh copy of the DLL.</para>
/// </summary>
internal sealed class KnobPreview : IDisposable
{
    private const string Dll = "knob_preview.dll";

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    private delegate void SendCallback(IntPtr line);

    // The DLL's exports (knob-os/src/hal/preview/preview.cpp), from the copy that's loaded.
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int BoardFn(byte[] id);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate void StartFn(SendCallback send);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate void VoidFn();
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate void IntFn(int value);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate void TouchFn(int x, int y, int down);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate void LineFn(byte[] line);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate IntPtr FrameFn(out uint serial);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate int GetIntFn();
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate uint MotorFn(out int positions, out int position, out int wrap, out int detent);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] private delegate uint LedsFn([Out] uint[] colors, int max, out int count, out int arcStart, out int arcSweep);

    private sealed class Library(IntPtr handle, string path)
    {
        public readonly IntPtr Handle = handle;
        public readonly string Path = path;
        public BoardFn Board = null!;
        public StartFn Start = null!;
        public VoidFn Tick = null!;
        public IntFn Input = null!, LinkConnected = null!;
        public TouchFn Touch = null!;
        public LineFn LinkLine = null!;
        public FrameFn Frame = null!;
        public GetIntFn Brightness = null!, Vibration = null!;
        public MotorFn Motor = null!;
        public LedsFn Leds = null!;
    }

    // Ticks about every frame while the preview is on show, 4 times a second while it isn't (see Shown).
    private static readonly TimeSpan ShownTick = TimeSpan.FromMilliseconds(16), HiddenTick = TimeSpan.FromMilliseconds(250);

    private readonly DispatcherTimer _timer = new(DispatcherPriority.Render) { Interval = HiddenTick };
    private bool _shown;
    private readonly SendCallback _send;  // kept alive: the DLL holds on to it
    private Library? _lib;
    private uint _shownSerial = uint.MaxValue;
    private uint _motorSerial;
    private uint _ledsSerial;
    private readonly uint[] _ledColors = new uint[16];

    public KnobPreview()
    {
        _send = OnSend;
        _timer.Tick += (_, _) => Tick();
    }

    /// <summary>The board it pretends to be (but for its screen size: see <see cref="Size"/>).</summary>
    public KnobBoard Board { get; private set; } = KnobBoard.Default;

    /// <summary>The screen's size in pixels (it's square): always 480, shown one pixel each. Knob OS draws it like a
    /// knob of that size; pictures drawn for the connected knob's size are scaled to it.</summary>
    public int Size { get; } = 480;

    /// <summary>The knob's screen, updated as it redraws.</summary>
    public WriteableBitmap Screen { get; } = new(480, 480, 96, 96, PixelFormats.Bgr32, null);

    /// <summary>Whether the preview is on show. While it isn't (Knob Hub's window in the tray or minimized, or another
    /// page open), Knob OS still gets every message and keeps up with the knob, but ticks only 4 times a second and
    /// doesn't copy its screen: on show again, the screen is as it is now straight away.</summary>
    public bool Shown
    {
        get => _shown;
        set
        {
            if (value == _shown) return;
            _shown = value;
            _timer.Interval = value ? ShownTick : HiddenTick;
            if (value) Tick();
        }
    }

    /// <summary>Why the preview isn't running (knob_preview.dll missing), or null.</summary>
    public string? Error { get; private set; }

    /// <summary>The screen brightness Knob OS is set to (5-100).</summary>
    public int Brightness { get; private set; } = 100;

    /// <summary>How the motor on the knob feels now (boards with a motor), or null before Knob OS set it.</summary>
    public HapticFeel? MotorFeel { get; private set; }

    /// <summary>A message the preview knob sends to Knob Hub. Raised after the call that produced it returns,
    /// so handlers can call back into the preview.</summary>
    public event Action<string>? Sent;

    public event Action? BrightnessChanged;

    /// <summary>It started again as another board.</summary>
    public event Action? BoardChanged;

    /// <summary>The vibration motor played an effect: 0 tick, 1 bump, 2 click, 3 alert.</summary>
    public event Action<int>? Vibrated;

    public event Action? MotorFeelChanged;

    /// <summary>The LEDs' colours (boards with LEDs), in order round the ring, brightness applied; empty before Knob
    /// OS set them.</summary>
    public Color[] Leds { get; private set; } = [];

    /// <summary>Where the screen's arc for the LEDs' level is, to show them along it: its start in degrees clockwise
    /// from 3 o'clock, and how far round it goes.</summary>
    public (int Start, int Sweep) LedArc { get; private set; } = (270, 360);

    public event Action? LedsChanged;

    /// <summary>Starts Knob OS as the board (the default one if Knob OS doesn't know it). Starting again replaces the
    /// running one; the caller connects it again (<see cref="LinkConnected"/>), and it says hello.</summary>
    public bool Start(KnobBoard board)
    {
        string source = System.IO.Path.Combine(AppContext.BaseDirectory, Dll);
        if (!File.Exists(source))
        {
            Error = "knob_preview.dll is missing: build it in knob-os with pio run -e preview, then rebuild Knob Hub.";
            return false;
        }
        Stop();
        try
        {
            _lib = Load(source);
            if (_lib.Board(Utf8(board.Id)) <= 0) board = KnobBoard.Default;  // a board this Knob OS doesn't know
            _lib.Start(_send);
        }
        catch (Exception ex) when (ex is DllNotFoundException or BadImageFormatException or EntryPointNotFoundException or IOException)
        {
            Error = "The preview couldn't start: " + ex.Message;
            Stop();
            return false;
        }
        Error = null;
        bool changed = board != Board;
        Board = board;
        _shownSerial = uint.MaxValue;
        _motorSerial = 0;
        MotorFeel = null;
        _ledsSerial = 0;
        if (Leds.Length > 0)
        {
            Leds = [];
            LedsChanged?.Invoke();
        }
        _timer.Start();
        Tick();
        if (changed) BoardChanged?.Invoke();
        return true;
    }

    /// <summary>0 turn right, 1 turn left, 2 press, 3 back.</summary>
    public void Input(int input) => _lib?.Input(input);

    /// <summary>A touch at screen pixels (0 to <see cref="Size"/> - 1): down while the finger (mouse button) is on
    /// the screen.</summary>
    public void Touch(int x, int y, bool down) => _lib?.Touch(x, y, down ? 1 : 0);

    public void LinkConnected(bool connected) => _lib?.LinkConnected(connected ? 1 : 0);

    public void LinkLine(string line) => _lib?.LinkLine(Utf8(line));

    private void Tick()
    {
        if (_lib is not { } lib) return;
        lib.Tick();
        IntPtr frame = lib.Frame(out uint serial);
        if (_shown && serial != _shownSerial)
        {
            _shownSerial = serial;
            int size = Screen.PixelWidth;
            Screen.WritePixels(new Int32Rect(0, 0, size, size), frame, size * size * 4, size * 4);
        }
        int brightness = lib.Brightness();
        if (brightness != Brightness)
        {
            Brightness = brightness;
            BrightnessChanged?.Invoke();
        }
        for (int effect = lib.Vibration(); effect >= 0; effect = lib.Vibration()) Vibrated?.Invoke(effect);
        uint motor = lib.Motor(out int positions, out int position, out int wrap, out int detent);
        if (motor != _motorSerial)
        {
            _motorSerial = motor;
            MotorFeel = new HapticFeel(positions, position, wrap != 0, detent);
            MotorFeelChanged?.Invoke();
        }
        uint leds = lib.Leds(_ledColors, _ledColors.Length, out int count, out int arcStart, out int arcSweep);
        if (leds != _ledsSerial)
        {
            _ledsSerial = leds;
            LedArc = (arcStart, arcSweep);
            Leds = _ledColors.Take(Math.Min(count, _ledColors.Length))
                .Select(c => Color.FromRgb((byte)(c >> 16), (byte)(c >> 8), (byte)c)).ToArray();
            LedsChanged?.Invoke();
        }
    }

    private void OnSend(IntPtr line)
    {
        string text = Marshal.PtrToStringUTF8(line) ?? "";
        // Called from inside Knob OS: handle it once Knob OS is done with whatever it was doing.
        Dispatcher.CurrentDispatcher.BeginInvoke(() => Sent?.Invoke(text));
    }

    /// <summary>Loads a private copy of the DLL, so each start gets a fresh Knob OS.</summary>
    private static Library Load(string source)
    {
        string folder = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "KnobHub", "Preview");
        Directory.CreateDirectory(folder);
        // Copies left by an earlier Knob Hub that didn't close normally.
        foreach (string old in Directory.GetFiles(folder, "knob_preview-*.dll"))
        {
            try { File.Delete(old); }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { }  // in use: another Knob Hub's
        }
        string path = System.IO.Path.Combine(folder, $"knob_preview-{Guid.NewGuid():N}.dll");
        File.Copy(source, path);
        IntPtr handle = NativeLibrary.Load(path);
        T Get<T>(string name) where T : Delegate => Marshal.GetDelegateForFunctionPointer<T>(NativeLibrary.GetExport(handle, name));
        try
        {
            return new Library(handle, path)
            {
                Board = Get<BoardFn>("kp_board"),
                Start = Get<StartFn>("kp_start"),
                Tick = Get<VoidFn>("kp_tick"),
                Input = Get<IntFn>("kp_input"),
                LinkConnected = Get<IntFn>("kp_link_connected"),
                Touch = Get<TouchFn>("kp_touch"),
                LinkLine = Get<LineFn>("kp_link_line"),
                Frame = Get<FrameFn>("kp_frame"),
                Brightness = Get<GetIntFn>("kp_brightness"),
                Vibration = Get<GetIntFn>("kp_vibration"),
                Motor = Get<MotorFn>("kp_motor"),
                Leds = Get<LedsFn>("kp_leds"),
            };
        }
        catch (EntryPointNotFoundException)
        {
            NativeLibrary.Free(handle);
            throw;
        }
    }

    private void Stop()
    {
        _timer.Stop();
        if (_lib is not { } lib) return;
        _lib = null;
        NativeLibrary.Free(lib.Handle);
        try { File.Delete(lib.Path); }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { }
    }

    private static byte[] Utf8(string text)
    {
        byte[] bytes = new byte[Encoding.UTF8.GetByteCount(text) + 1];
        Encoding.UTF8.GetBytes(text, bytes);
        return bytes;
    }

    public void Dispose() => Stop();
}

/// <summary>How turning the knob feels on a board with a motor (knob-os/src/core/haptics.h): detent positions (0 =
/// endless), the one it's on, whether it wraps round, and the detent strength (0 fine, 1 medium, 2 firm).</summary>
internal sealed record HapticFeel(int Positions, int Position, bool Wrap, int Detent)
{
    public bool Bounded => Positions > 0 && !Wrap;
}
