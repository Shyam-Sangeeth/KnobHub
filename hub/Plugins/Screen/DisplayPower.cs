using System.Runtime.InteropServices;

namespace Screen.KnobApp;

/// <summary>
/// The PC's displays as Windows sees them: off the way the screen timeout turns them off, on again the way input
/// wakes them, and news whenever they go off or come on (whoever did it). Windows treats all the displays as one here.
/// </summary>
internal sealed class DisplayPower : IDisposable
{
    // GUID_CONSOLE_DISPLAY_STATE: the console's displays are off (0), on (1) or dimmed (2).
    private static readonly Guid ConsoleDisplayState = new("6FE69556-704A-47A0-8F24-C28D936FDA47");

    private readonly NotifyCallback callback;   // kept alive while Windows holds it
    private readonly IntPtr parameters;         // DEVICE_NOTIFY_SUBSCRIBE_PARAMETERS, kept until unregistered
    private IntPtr registration;
    private int state = -1;                     // as Windows last said; -1 until it has

    /// <summary>True while the displays are on (or dimmed). True too until Windows has said, as they usually are.</summary>
    public bool IsOn => Volatile.Read(ref state) != 0;

    /// <summary>The displays went off or came on. Raised on a thread of Windows'.</summary>
    public event Action? Changed;

    public DisplayPower()
    {
        callback = OnNotify;
        parameters = Marshal.AllocHGlobal(IntPtr.Size * 2);
        Marshal.WriteIntPtr(parameters, Marshal.GetFunctionPointerForDelegate(callback));
        Marshal.WriteIntPtr(parameters, IntPtr.Size, IntPtr.Zero);
        var setting = ConsoleDisplayState;
        // Windows tells the current state straight away, then each change.
        if (PowerSettingRegisterNotification(ref setting, DeviceNotifyCallback, parameters, out registration) != 0)
            registration = IntPtr.Zero;
    }

    /// <summary>Turns the displays off, as the screen timeout does.</summary>
    public static void TurnOff() => PostMessage(HwndBroadcast, WmSysCommand, ScMonitorPower, MonitorOff);

    /// <summary>
    /// Turns the displays on again. Windows wakes them on input, and a press on the knob isn't the PC's input, so this
    /// moves the mouse by a pixel and back (nothing is clicked), and also asks Windows for the display directly.
    /// </summary>
    public static void TurnOn()
    {
        SetThreadExecutionState(EsDisplayRequired);
        PostMessage(HwndBroadcast, WmSysCommand, ScMonitorPower, MonitorOn);
        var inputs = new[] { MouseMove(1), MouseMove(-1) };
        SendInput((uint)inputs.Length, inputs, Marshal.SizeOf<Input>());
    }

    private static Input MouseMove(int dx) => new() { Type = InputMouse, Mouse = new MouseInput { Dx = dx, Flags = MouseEventMove } };

    private uint OnNotify(IntPtr context, uint type, IntPtr setting)
    {
        if (type != PbtPowerSettingChange || setting == IntPtr.Zero) return 0;
        // POWERBROADCAST_SETTING: the setting's GUID, the data's length, then the data (here a 32-bit state).
        if (Marshal.PtrToStructure<Guid>(setting) != ConsoleDisplayState || Marshal.ReadInt32(setting, 16) < 4) return 0;
        var now = Marshal.ReadInt32(setting, 20);
        if (Interlocked.Exchange(ref state, now) == now) return 0;
        try
        {
            Changed?.Invoke();
        }
        catch (Exception)
        {
            // On Windows' thread: nothing may get out of here.
        }
        return 0;
    }

    public void Dispose()
    {
        if (registration != IntPtr.Zero) PowerSettingUnregisterNotification(registration);
        registration = IntPtr.Zero;
        Marshal.FreeHGlobal(parameters);
    }

    private const uint DeviceNotifyCallback = 2, PbtPowerSettingChange = 0x8013;
    private const uint WmSysCommand = 0x0112;
    private static readonly IntPtr HwndBroadcast = 0xFFFF, ScMonitorPower = 0xF170, MonitorOff = 2, MonitorOn = -1;
    private const uint EsDisplayRequired = 0x00000002;
    private const uint InputMouse = 0, MouseEventMove = 0x0001;

    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    private delegate uint NotifyCallback(IntPtr context, uint type, IntPtr setting);

    [StructLayout(LayoutKind.Sequential)]
    private struct MouseInput
    {
        public int Dx, Dy;
        public uint MouseData, Flags, Time;
        public IntPtr ExtraInfo;
    }

    // INPUT with the mouse member of its union (the largest, so the size is right).
    [StructLayout(LayoutKind.Sequential)]
    private struct Input
    {
        public uint Type;
        public MouseInput Mouse;
    }

    [DllImport("powrprof.dll")]
    private static extern uint PowerSettingRegisterNotification(ref Guid setting, uint flags, IntPtr recipient, out IntPtr registration);

    [DllImport("powrprof.dll")]
    private static extern uint PowerSettingUnregisterNotification(IntPtr registration);

    [DllImport("user32.dll")]
    private static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    private static extern uint SendInput(uint count, Input[] inputs, int size);

    [DllImport("kernel32.dll")]
    private static extern uint SetThreadExecutionState(uint flags);
}
