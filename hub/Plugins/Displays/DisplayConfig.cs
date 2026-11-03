using System.ComponentModel;
using System.Runtime.InteropServices;

namespace Displays.KnobApp;

/// <summary>A display Windows shows the desktop on.</summary>
/// <param name="Id">Its adapter and source; stays the same while it's connected.</param>
/// <param name="Name">The monitor's own name, e.g. "LG ULTRAGEAR" (both names, joined, for a duplicated display).</param>
/// <param name="IsMain">The main display: the Start menu, the taskbar's clock and new windows go there.</param>
internal sealed record Display(string Id, string Name, bool IsMain);

/// <summary>Windows' display configuration through the CCD API (QueryDisplayConfig, SetDisplayConfig), the one
/// Settings > Display uses.</summary>
internal static class DisplayConfig
{
    /// <summary>The displays in use: the main one first, then the others left to right as they're arranged.</summary>
    public static List<Display> Active()
    {
        var (paths, modes) = Query();
        var displays = new List<(Display Display, int X, int Y)>();
        // A duplicated display is one source shown on several monitors (one path each).
        foreach (var group in paths.GroupBy(p => SourceId(p.SourceInfo)))
        {
            var first = group.First();
            if (first.SourceInfo.ModeInfoIdx >= modes.Length) continue;
            var mode = modes[first.SourceInfo.ModeInfoIdx];
            var names = group.Select(p => MonitorName(p.TargetInfo)).Where(n => n != "").Distinct().ToList();
            string name = names.Count > 0 ? string.Join(" + ", names) : "Display " + DisplayNumber(first.SourceInfo);
            displays.Add((new Display(group.Key, name, mode.X == 0 && mode.Y == 0), mode.X, mode.Y));
        }
        return [.. displays.OrderByDescending(d => d.Display.IsMain).ThenBy(d => d.X).ThenBy(d => d.Y).Select(d => d.Display)];
    }

    /// <summary>Makes a display the main one, like Settings' "Make this my main display": the main display is always
    /// at the desktop's origin, so every display moves by the same amount and they keep their places round it.</summary>
    public static void MakeMain(string id)
    {
        var (paths, modes) = Query();
        var path = paths.FirstOrDefault(p => SourceId(p.SourceInfo) == id);
        if (SourceId(path.SourceInfo) != id || path.SourceInfo.ModeInfoIdx >= modes.Length)
            throw new InvalidOperationException("That display isn't connected any more.");
        var main = modes[path.SourceInfo.ModeInfoIdx];
        if (main.X == 0 && main.Y == 0) return;
        for (int i = 0; i < modes.Length; i++)
        {
            if (modes[i].InfoType != ModeInfoTypeSource) continue;
            modes[i].X -= main.X;
            modes[i].Y -= main.Y;
        }
        Check(SetDisplayConfig((uint)paths.Length, paths, (uint)modes.Length, modes,
                               SdcApply | SdcUseSuppliedDisplayConfig | SdcSaveToDatabase | SdcAllowChanges));
    }

    private static (PathInfo[] Paths, ModeInfo[] Modes) Query()
    {
        for (int attempt = 0; ; attempt++)
        {
            Check(GetDisplayConfigBufferSizes(QdcOnlyActivePaths, out uint pathCount, out uint modeCount));
            var paths = new PathInfo[pathCount];
            var modes = new ModeInfo[modeCount];
            int error = QueryDisplayConfig(QdcOnlyActivePaths, ref pathCount, paths, ref modeCount, modes, IntPtr.Zero);
            if (error == ErrorInsufficientBuffer && attempt < 3) continue;  // a display came or went in between
            Check(error);
            return (paths[..(int)pathCount], modes[..(int)modeCount]);
        }
    }

    private static string SourceId(PathSourceInfo source) => $"{source.AdapterId.High:X8}{source.AdapterId.Low:X8}:{source.Id}";

    // The name in the monitor's EDID; "" when it has none (often a laptop's own screen).
    private static string MonitorName(PathTargetInfo target)
    {
        var name = new TargetDeviceName();
        name.Header = new DeviceInfoHeader
        {
            Type = DeviceInfoGetTargetName, Size = (uint)Marshal.SizeOf<TargetDeviceName>(), AdapterId = target.AdapterId, Id = target.Id,
        };
        if (DisplayConfigGetDeviceInfo(ref name) != 0) return "";
        if (name.MonitorFriendlyDeviceName is { Length: > 0 } friendly) return friendly;
        return target.OutputTechnology is OutputInternal or OutputDisplayPortEmbedded or OutputUdiEmbedded ? "Built-in display" : "";
    }

    // The number Windows gives the display ("\\.\DISPLAY2" is 2).
    private static string DisplayNumber(PathSourceInfo source)
    {
        var name = new SourceDeviceName();
        name.Header = new DeviceInfoHeader
        {
            Type = DeviceInfoGetSourceName, Size = (uint)Marshal.SizeOf<SourceDeviceName>(), AdapterId = source.AdapterId, Id = source.Id,
        };
        if (DisplayConfigGetDeviceInfo(ref name) != 0 || name.ViewGdiDeviceName == null) return (source.Id + 1).ToString();
        return new string(name.ViewGdiDeviceName.Reverse().TakeWhile(char.IsAsciiDigit).Reverse().ToArray());
    }

    private static void Check(int error)
    {
        if (error != 0) throw new Win32Exception(error);
    }

    // --- wingdi.h / winuser.h ---

    private const uint QdcOnlyActivePaths = 0x2;
    private const uint SdcApply = 0x80, SdcUseSuppliedDisplayConfig = 0x20, SdcSaveToDatabase = 0x200, SdcAllowChanges = 0x400;
    private const uint ModeInfoTypeSource = 1;
    private const uint DeviceInfoGetSourceName = 1, DeviceInfoGetTargetName = 2;
    private const uint OutputInternal = 0x80000000, OutputDisplayPortEmbedded = 11, OutputUdiEmbedded = 13;
    private const int ErrorInsufficientBuffer = 122;

    [StructLayout(LayoutKind.Sequential)]
    private struct Luid { public uint Low; public int High; }

    [StructLayout(LayoutKind.Sequential)]
    private struct Rational { public uint Numerator, Denominator; }

    [StructLayout(LayoutKind.Sequential)]
    private struct PathSourceInfo { public Luid AdapterId; public uint Id; public uint ModeInfoIdx; public uint StatusFlags; }

    [StructLayout(LayoutKind.Sequential)]
    private struct PathTargetInfo
    {
        public Luid AdapterId;
        public uint Id, ModeInfoIdx, OutputTechnology, Rotation, Scaling;
        public Rational RefreshRate;
        public uint ScanLineOrdering;
        public int TargetAvailable;
        public uint StatusFlags;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct PathInfo { public PathSourceInfo SourceInfo; public PathTargetInfo TargetInfo; public uint Flags; }

    // DISPLAYCONFIG_MODE_INFO: its union holds a target, source or desktop image mode; only a source mode's fields are
    // named here.
    [StructLayout(LayoutKind.Explicit, Size = 64)]
    private struct ModeInfo
    {
        [FieldOffset(0)] public uint InfoType;
        [FieldOffset(4)] public uint Id;
        [FieldOffset(8)] public Luid AdapterId;
        [FieldOffset(16)] public uint Width;
        [FieldOffset(20)] public uint Height;
        [FieldOffset(24)] public uint PixelFormat;
        [FieldOffset(28)] public int X;
        [FieldOffset(32)] public int Y;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct DeviceInfoHeader { public uint Type, Size; public Luid AdapterId; public uint Id; }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct TargetDeviceName
    {
        public DeviceInfoHeader Header;
        public uint Flags, OutputTechnology;
        public ushort EdidManufactureId, EdidProductCodeId;
        public uint ConnectorInstance;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string MonitorFriendlyDeviceName;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string MonitorDevicePath;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct SourceDeviceName
    {
        public DeviceInfoHeader Header;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string ViewGdiDeviceName;
    }

    [DllImport("user32.dll")]
    private static extern int GetDisplayConfigBufferSizes(uint flags, out uint pathCount, out uint modeCount);

    [DllImport("user32.dll")]
    private static extern int QueryDisplayConfig(uint flags, ref uint pathCount, [Out] PathInfo[] paths, ref uint modeCount,
                                                 [Out] ModeInfo[] modes, IntPtr currentTopologyId);

    [DllImport("user32.dll")]
    private static extern int SetDisplayConfig(uint pathCount, PathInfo[] paths, uint modeCount, ModeInfo[] modes, uint flags);

    [DllImport("user32.dll")]
    private static extern int DisplayConfigGetDeviceInfo(ref TargetDeviceName request);

    [DllImport("user32.dll")]
    private static extern int DisplayConfigGetDeviceInfo(ref SourceDeviceName request);
}
