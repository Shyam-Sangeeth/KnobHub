using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Shapes;
using KnobHub.Apps;
using KnobHub.Core;
using Microsoft.Win32;

namespace KnobHub;

public partial class MainWindow : Window
{
    private readonly MainViewModel _model;
    private int _wheel;  // wheel movement not yet turned into knob steps (touchpads send small deltas)
    private System.Drawing.Point _touch;  // where the finger (left button) last was on the knob's screen
    private readonly System.Windows.Threading.DispatcherTimer _knobButtonTimer = new() { Interval = TimeSpan.FromMilliseconds(500) };

    internal MainWindow(MainViewModel model, int[]? placement, Action<int[]> placed)
    {
        InitializeComponent();
        SourceInitialized += (_, _) => WindowPlace.Restore(this, placement);
        // Remember where it's put as soon as it's moved or resized (Knob Hub may not get to close normally).
        var settle = new System.Windows.Threading.DispatcherTimer { Interval = TimeSpan.FromMilliseconds(500) };
        settle.Tick += (_, _) =>
        {
            settle.Stop();
            if (IsVisible && WindowState != WindowState.Minimized && WindowPlace.Save(this) is { } bounds) placed(bounds);
        };
        EventHandler moved = (_, _) => { settle.Stop(); settle.Start(); };
        LocationChanged += moved;
        SizeChanged += (_, _) => moved(this, EventArgs.Empty);
        StateChanged += moved;
        _knobButtonTimer.Tick += (_, _) => KnobButtonHeld();
        Loaded += (_, _) => SizePreview();
        DpiChanged += (_, _) => SizePreview();
        _model = model;
        DataContext = model;
        model.Vibrated += FlashRing;
        model.LedsChanged += ShowLeds;
        // In the tray, minimized or on another page, the preview only keeps up with the knob, without drawing.
        IsVisibleChanged += (_, _) => ShowPreview();
        StateChanged += (_, _) => ShowPreview();
        StateChanged += (_, _) => ShowWindowState();
        Loaded += (_, _) => ShowWindowState();
    }

    // --- The title bar ---

    private void Minimize_Click(object sender, RoutedEventArgs e) => WindowState = WindowState.Minimized;

    private void Maximize_Click(object sender, RoutedEventArgs e) =>
        WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;

    private void Close_Click(object sender, RoutedEventArgs e) => Close();

    // Maximized, the window reaches past the screen's edges by its frame: the content stays inside. The middle button
    // shows restore or maximize.
    private void ShowWindowState()
    {
        bool maximized = WindowState == WindowState.Maximized;
        var frame = SystemParameters.WindowNonClientFrameThickness;
        Root.Margin = maximized ? new Thickness(frame.Left, frame.Bottom, frame.Right, frame.Bottom) : new Thickness(0);
        MaximizeButton.Content = maximized ? "\uE923" : "\uE922";
        MaximizeButton.ToolTip = maximized ? "Restore" : "Maximize";
        AutomationProperties.SetName(MaximizeButton, maximized ? "Restore" : "Maximize");
    }

    private void Nav_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (KnobPage == null) return;  // still loading
        KnobPage.Visibility = Nav.SelectedIndex == 0 ? Visibility.Visible : Visibility.Collapsed;
        AppsPage.Visibility = Nav.SelectedIndex == 1 ? Visibility.Visible : Visibility.Collapsed;
        BoardPage.Visibility = Nav.SelectedIndex == 2 ? Visibility.Visible : Visibility.Collapsed;
        SettingsPage.Visibility = Nav.SelectedIndex == 3 ? Visibility.Visible : Visibility.Collapsed;
        ShowPreview();
    }

    private void ShowPreview() =>
        _model.ShowPreview(IsVisible && WindowState != WindowState.Minimized && KnobPage.Visibility == Visibility.Visible);

    // --- Live preview: scroll = turn, middle-click = press (held: back), left button = touch ---

    /// <summary>The preview is 480 screen pixels across whatever the monitor's scaling: one for each of its 480 px,
    /// drawn with nearest neighbour so nothing smears.</summary>
    private void SizePreview()
    {
        const int PreviewPixels = 480;
        double dpi = VisualTreeHelper.GetDpi(this).DpiScaleX;
        double side = PreviewPixels / dpi;  // in WPF units
        PreviewScreen.Width = PreviewScreen.Height = PreviewDim.Width = PreviewDim.Height = side;
        PreviewScreen.Clip = new EllipseGeometry(new Point(side / 2, side / 2), side / 2, side / 2);
        PreviewRing.Width = PreviewRing.Height = side + 4;
        PreviewGlow.Width = PreviewGlow.Height = side;
        ShowLeds();
    }

    /// <summary>The knob's LEDs as a soft glow round the preview. One LED is a halo all round it. Several share the
    /// screen's arc (a dial's from bottom left over the top to bottom right, a song's all round from the top) in equal
    /// parts, in order, so a level fills the glow from where the arc on the screen starts to where it ends, as finely as
    /// the knob has LEDs. Brightness is the glow's opacity.</summary>
    private void ShowLeds()
    {
        const double Reach = 30;  // how far the glow spreads outside the screen, WPF units
        PreviewGlow.Children.Clear();
        var leds = _model.Leds;
        var (start, sweep) = _model.LedArc;
        double radius = PreviewGlow.Width / 2;
        if (leds.Length == 0 || double.IsNaN(radius)) return;
        double outer = radius + Reach;
        for (int i = 0; i < leds.Length; i++)
        {
            var (color, opacity) = Glow(leds[i]);
            if (opacity <= 0) continue;
            Color At(double alpha) => Color.FromArgb((byte)(alpha * 255), color.R, color.G, color.B);
            // Clear inside the screen, strongest at its edge, fading out softly.
            var brush = new RadialGradientBrush
            {
                MappingMode = BrushMappingMode.Absolute,
                Center = new Point(radius, radius),
                GradientOrigin = new Point(radius, radius),
                RadiusX = outer,
                RadiusY = outer,
                GradientStops =
                {
                    new GradientStop(At(0), (radius - 4) / outer),
                    new GradientStop(At(0.6), radius / outer),
                    new GradientStop(At(0.22), (radius + Reach * 0.4) / outer),
                    new GradientStop(At(0), 1),
                },
            };
            Geometry area = leds.Length == 1
                ? new EllipseGeometry(new Point(radius, radius), outer, outer)
                : Sector(radius, radius - 4, outer, start + (double)sweep * i / leds.Length, start + (double)sweep * (i + 1) / leds.Length);
            PreviewGlow.Children.Add(new Path { Data = area, Fill = brush, Opacity = opacity });
        }
    }

    /// <summary>The part of a ring round (center, center) between two radii and two angles (degrees clockwise from
    /// 3 o'clock).</summary>
    private static Geometry Sector(double center, double inner, double outer, double from, double to)
    {
        Point At(double r, double degrees) =>
            new(center + r * Math.Cos(degrees * Math.PI / 180), center + r * Math.Sin(degrees * Math.PI / 180));
        bool large = to - from > 180;
        var figure = new PathFigure { StartPoint = At(inner, from), IsClosed = true };
        figure.Segments.Add(new LineSegment(At(outer, from), true));
        figure.Segments.Add(new ArcSegment(At(outer, to), new Size(outer, outer), 0, large, SweepDirection.Clockwise, true));
        figure.Segments.Add(new LineSegment(At(inner, to), true));
        figure.Segments.Add(new ArcSegment(At(inner, from), new Size(inner, inner), 0, large, SweepDirection.Counterclockwise, true));
        return new PathGeometry([figure]);
    }

    /// <summary>An LED's colour at full brightness, and its brightness (0-1).</summary>
    private static (Color Color, double Opacity) Glow(Color led)
    {
        int max = Math.Max(led.R, Math.Max(led.G, led.B));
        if (max == 0) return (led, 0);
        return (Color.FromRgb((byte)(led.R * 255 / max), (byte)(led.G * 255 / max), (byte)(led.B * 255 / max)), max / 255.0);
    }

    private void Preview_MouseWheel(object sender, MouseWheelEventArgs e)
    {
        e.Handled = true;  // turning the knob, not scrolling the page
        _wheel += e.Delta;
        while (Math.Abs(_wheel) >= Mouse.MouseWheelDeltaForOneLine)
        {
            bool down = _wheel < 0;
            _model.PreviewInput(down ? 0 : 1);  // wheel down = turn right
            _wheel += down ? Mouse.MouseWheelDeltaForOneLine : -Mouse.MouseWheelDeltaForOneLine;
        }
    }

    // The left button is a finger on the knob's touch screen: tap, drag, swipe.
    private void Preview_TouchDown(object sender, MouseButtonEventArgs e)
    {
        if (sender is not UIElement surface || !surface.CaptureMouse()) return;
        _touch = ScreenPoint(e);
        _model.PreviewTouch(_touch.X, _touch.Y, true);
    }

    private void Preview_TouchMove(object sender, MouseEventArgs e)
    {
        if (sender is not UIElement { IsMouseCaptured: true }) return;
        var at = ScreenPoint(e);
        if (at == _touch) return;
        _touch = at;
        _model.PreviewTouch(at.X, at.Y, true);
    }

    private void Preview_TouchUp(object sender, MouseButtonEventArgs e)
    {
        if (sender is not UIElement { IsMouseCaptured: true } surface) return;
        _touch = ScreenPoint(e);
        _model.PreviewTouch(_touch.X, _touch.Y, false);
        surface.ReleaseMouseCapture();
    }

    // The middle button is the knob's own button: a press when let go quickly, back (the knob's long press) once held
    // for half a second, never both.
    private void Preview_MouseDown(object sender, MouseButtonEventArgs e)
    {
        if (e.ChangedButton != MouseButton.Middle) return;
        e.Handled = true;
        _knobButtonTimer.Stop();
        _knobButtonTimer.Start();
    }

    private void Preview_MouseUp(object sender, MouseButtonEventArgs e)
    {
        if (e.ChangedButton != MouseButton.Middle) return;
        e.Handled = true;
        if (!_knobButtonTimer.IsEnabled) return;  // held: the long press already went back
        _knobButtonTimer.Stop();
        _model.PreviewInput(2);
    }

    private void KnobButtonHeld()
    {
        _knobButtonTimer.Stop();
        _model.PreviewInput(3);
    }

    /// <summary>The pointer in knob screen pixels (0 to the screen's size - 1), kept on the screen while dragging
    /// outside it.</summary>
    private System.Drawing.Point ScreenPoint(MouseEventArgs e)
    {
        var at = e.GetPosition(PreviewScreen);
        int size = _model.ScreenSize;
        double scale = size / PreviewScreen.ActualWidth;
        return new(Math.Clamp((int)(at.X * scale), 0, size - 1), Math.Clamp((int)(at.Y * scale), 0, size - 1));
    }

    /// <summary>The preview knob's vibration motor played an effect (0 tick, 1 bump, 2 click, 3 alert): its ring
    /// flashes, a bump in red.</summary>
    private void FlashRing(int effect)
    {
        var color = effect switch
        {
            1 => Color.FromRgb(0xF8, 0x71, 0x71),
            3 => Color.FromRgb(0xF2, 0xC1, 0x4E),
            _ => Color.FromRgb(0x8B, 0x7C, 0xF6),
        };
        var brush = new SolidColorBrush(color);
        PreviewRing.Stroke = brush;
        brush.BeginAnimation(SolidColorBrush.ColorProperty,
            new ColorAnimation(Color.FromArgb(0xD9, 0xF2, 0xF3, 0xF5), TimeSpan.FromMilliseconds(effect == 0 ? 150 : 350)));
    }

    // --- Apps ---

    private static AppItem? ItemOf(object sender) => (sender as FrameworkElement)?.DataContext as AppItem;

    // Dragging a row moves the app on the Home ring: press on the row (not on one of its controls) and move past the
    // system's drag distance. While it's over another row, a line there shows where it will go.
    private AppItem? _pressedRow;
    private Point _pressedAt;

    private void AppRow_MouseDown(object sender, MouseButtonEventArgs e)
    {
        _pressedRow = IsOnControl(e.OriginalSource as DependencyObject, sender) ? null : ItemOf(sender);
        _pressedAt = e.GetPosition(this);
    }

    private void AppRow_MouseMove(object sender, MouseEventArgs e)
    {
        if (_pressedRow is not { } row || e.LeftButton != MouseButtonState.Pressed) return;
        var moved = e.GetPosition(this) - _pressedAt;
        if (Math.Abs(moved.X) < SystemParameters.MinimumHorizontalDragDistance &&
            Math.Abs(moved.Y) < SystemParameters.MinimumVerticalDragDistance) return;
        _pressedRow = null;
        row.IsDragged = true;
        try { DragDrop.DoDragDrop((DependencyObject)sender, new DataObject(typeof(AppItem), row), DragDropEffects.Move); }
        finally
        {
            row.IsDragged = false;
            foreach (var item in _model.Apps) item.DropMark = 0;
        }
    }

    private void AppRow_DragOver(object sender, DragEventArgs e)
    {
        e.Handled = true;
        if (DropTarget(sender, e) is not var (dragged, target, below))
        {
            e.Effects = DragDropEffects.None;
            return;
        }
        e.Effects = DragDropEffects.Move;
        foreach (var item in _model.Apps) item.DropMark = item == target && target != dragged ? (below ? 2 : 1) : 0;
    }

    private void AppRow_Drop(object sender, DragEventArgs e)
    {
        e.Handled = true;
        if (DropTarget(sender, e) is not var (dragged, target, below) || target == dragged) return;
        var entries = Manager.Entries;
        int from = entries.IndexOf(dragged.Entry), to = entries.IndexOf(target.Entry) + (below ? 1 : 0);
        if (from < to) to--;  // the dragged row leaves its place first
        Manager.MoveTo(dragged.Entry, to);
    }

    /// <summary>The row being dragged, the row it's over, and whether it's over that row's lower half.</summary>
    private static (AppItem Dragged, AppItem Target, bool Below)? DropTarget(object sender, DragEventArgs e)
    {
        if (e.Data.GetData(typeof(AppItem)) is not AppItem dragged || sender is not FrameworkElement row || ItemOf(sender) is not { } target)
            return null;
        return (dragged, target, e.GetPosition(row).Y > row.ActualHeight / 2);
    }

    /// <summary>Whether a press landed on one of the row's controls (a button, check box or drop-down), which keep
    /// their clicks.</summary>
    private static bool IsOnControl(DependencyObject? hit, object row)
    {
        for (var at = hit; at != null && at != row; at = at is System.Windows.Media.Visual ? System.Windows.Media.VisualTreeHelper.GetParent(at) : LogicalTreeHelper.GetParent(at))
            if (at is System.Windows.Controls.Primitives.ButtonBase or ComboBox) return true;
        return false;
    }

    private AppManager Manager => ((App)Application.Current).Session.Apps;

    // Add apps is a modal window over Knob Hub: Knob Hub is back once it's closed.
    private void AddApps_Click(object sender, RoutedEventArgs e) => new AddAppsWindow(_model, Manager, this).ShowDialog();

    private void Uninstall_Click(object sender, RoutedEventArgs e)
    {
        if (ItemOf(sender) is not { } item) return;
        var answer = MessageBox.Show(this, $"Uninstall {item.Name}?", "Uninstall app", MessageBoxButton.YesNo, MessageBoxImage.Question);
        if (answer == MessageBoxResult.Yes) Manager.Uninstall(item.Entry);
    }
}
