using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;
using Microsoft.Win32;
using Drawing = System.Drawing;
using Forms = System.Windows.Forms;

namespace KnobHub;

internal static class IconHelper
{
    /// <summary>The app icon (Assets\KnobHub.ico) at the tray size for the current DPI.</summary>
    public static Drawing.Icon LoadTrayIcon()
    {
        var resource = Application.GetResourceStream(new Uri("pack://application:,,,/Assets/KnobHub.ico"))
            ?? throw new InvalidOperationException("App icon resource is missing.");
        using var stream = resource.Stream;
        return new Drawing.Icon(stream, System.Windows.Forms.SystemInformation.SmallIconSize);
    }

    /// <summary>An icon name (as in app.json) as a Segoe Fluent Icons glyph, for the Apps page: the app's own glyph
    /// icon or a built-in one; a generic app glyph for anything else.</summary>
    public static string Glyph(string icon, IReadOnlyDictionary<string, Sdk.KnobIcon>? appIcons = null) =>
        (appIcons != null && appIcons.TryGetValue(icon, out var own) ? own : Core.KnobIcons.BuiltIn.GetValueOrDefault(icon)) is Sdk.GlyphIcon glyph
            ? glyph.Text
            : AppGlyph;

    private static readonly string AppGlyph = char.ConvertFromUtf32(0xECAA);  // Segoe Fluent Icons' "app"

    /// <summary>A knob icon drawn the way the knob gets it, in <paramref name="color"/>, 18 px on screen (sharp up to 200%
    /// scaling); null if it can't be drawn.</summary>
    public static ImageSource? Render(Sdk.KnobIcon icon, Color color)
    {
        const int Pixels = 36;
        byte[] alpha;
        try { alpha = Core.KnobIcons.Draw(icon, Pixels); }
        catch (Exception) { return null; }
        var bgra = new byte[Pixels * Pixels * 4];
        for (int i = 0; i < alpha.Length; i++)
        {
            bgra[i * 4] = color.B;
            bgra[i * 4 + 1] = color.G;
            bgra[i * 4 + 2] = color.R;
            bgra[i * 4 + 3] = alpha[i];
        }
        var image = System.Windows.Media.Imaging.BitmapSource.Create(Pixels, Pixels, 192, 192, PixelFormats.Bgra32, null, bgra, Pixels * 4);
        image.Freeze();
        return image;
    }

    public static Color ParseColor(string hex, Color fallback)
    {
        try { return (Color)ColorConverter.ConvertFromString(hex); }
        catch (FormatException) { return fallback; }
    }
}

/// <summary>
/// Where the window opens: where it was left (if that's still on a monitor), otherwise centred on a monitor other than
/// the main one, which is often busy with a game.
/// </summary>
internal static class WindowPlace
{
    private const int SwHide = 0;

    [StructLayout(LayoutKind.Sequential)]
    private struct Rect { public int Left, Top, Right, Bottom; }

    [StructLayout(LayoutKind.Sequential)]
    private struct Placement
    {
        public int Length, Flags, ShowCmd;
        public Drawing.Point MinPosition, MaxPosition;
        public Rect NormalPosition;
    }

    [DllImport("user32.dll")] private static extern bool GetWindowPlacement(IntPtr hwnd, ref Placement placement);
    [DllImport("user32.dll")] private static extern bool SetWindowPlacement(IntPtr hwnd, ref Placement placement);

    /// <summary>Call from SourceInitialized (the window exists but isn't shown yet).</summary>
    public static void Restore(Window window, int[]? saved)
    {
        var hwnd = new WindowInteropHelper(window).Handle;
        var placement = new Placement { Length = Marshal.SizeOf<Placement>() };
        if (!GetWindowPlacement(hwnd, ref placement)) return;
        var current = placement.NormalPosition;

        Drawing.Rectangle bounds;
        if (saved is [var left, var top, var right, var bottom, _] && right > left && bottom > top &&
            Forms.Screen.AllScreens.Any(s => s.WorkingArea.IntersectsWith(Drawing.Rectangle.FromLTRB(left, top, right, bottom))))
        {
            bounds = Drawing.Rectangle.FromLTRB(left, top, right, bottom);
        }
        else
        {
            var screen = Forms.Screen.AllScreens.FirstOrDefault(s => !s.Primary) ?? Forms.Screen.PrimaryScreen;
            if (screen == null) return;
            var area = screen.WorkingArea;
            int width = Math.Min(current.Right - current.Left, area.Width), height = Math.Min(current.Bottom - current.Top, area.Height);
            bounds = new Drawing.Rectangle(area.Left + (area.Width - width) / 2, area.Top + (area.Height - height) / 2, width, height);
        }
        placement.NormalPosition = new Rect { Left = bounds.Left, Top = bounds.Top, Right = bounds.Right, Bottom = bounds.Bottom };
        placement.ShowCmd = SwHide;  // just move it; WPF shows it
        SetWindowPlacement(hwnd, ref placement);
        if (saved is [.., 1]) window.WindowState = WindowState.Maximized;
    }

    public static int[]? Save(Window window)
    {
        var hwnd = new WindowInteropHelper(window).Handle;
        var placement = new Placement { Length = Marshal.SizeOf<Placement>() };
        if (hwnd == IntPtr.Zero || !GetWindowPlacement(hwnd, ref placement)) return null;
        var r = placement.NormalPosition;
        return [r.Left, r.Top, r.Right, r.Bottom, window.WindowState == WindowState.Maximized ? 1 : 0];
    }
}

/// <summary>"Start with Windows": a per-user Run entry that starts Knob Hub in the tray at sign-in.</summary>
internal static class StartupSetting
{
    private const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string RunValue = "KnobHub";

    public static bool Enabled
    {
        get
        {
            using var key = Registry.CurrentUser.OpenSubKey(RunKey);
            return key?.GetValue(RunValue) != null;
        }
        set
        {
            using var key = Registry.CurrentUser.CreateSubKey(RunKey);
            if (value) key.SetValue(RunValue, $"\"{Environment.ProcessPath}\" --minimized");
            else key.DeleteValue(RunValue, throwOnMissingValue: false);
        }
    }
}
