using Forms = System.Windows.Forms;

namespace KnobHub;

/// <summary>Tray icon: left-click opens the window; the menu opens it or exits.</summary>
internal sealed class TrayIcon : IDisposable
{
    private readonly Forms.NotifyIcon _icon;
    private Action? _hintClicked;

    public TrayIcon(System.Drawing.Icon icon, Action open, Action exit)
    {
        var menu = new Forms.ContextMenuStrip();
        var openItem = new Forms.ToolStripMenuItem("Open Knob Hub", null, (_, _) => open());
        openItem.Font = new System.Drawing.Font(openItem.Font, System.Drawing.FontStyle.Bold);
        menu.Items.Add(openItem);
        menu.Items.Add(new Forms.ToolStripMenuItem("Exit", null, (_, _) => exit()));

        _icon = new Forms.NotifyIcon { Icon = icon, Text = "Knob Hub", ContextMenuStrip = menu, Visible = true };
        _icon.MouseClick += (_, e) =>
        {
            if (e.Button == Forms.MouseButtons.Left) open();
        };
        _icon.BalloonTipClicked += (_, _) => _hintClicked?.Invoke();
    }

    /// <summary>A Windows notification from the tray icon; clicking it calls <paramref name="onClick"/>.</summary>
    public void ShowHint(string title, string text, Action? onClick = null)
    {
        _hintClicked = onClick;
        _icon.ShowBalloonTip(5000, title, text, Forms.ToolTipIcon.None);
    }

    public void Dispose()
    {
        _icon.Visible = false;
        _icon.Dispose();
    }
}
