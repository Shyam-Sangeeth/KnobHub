using Forms = System.Windows.Forms;

namespace KnobHub;

/// <summary>Tray icon: left-click opens the window; the menu opens it or exits.</summary>
internal sealed class TrayIcon : IDisposable
{
    private readonly Forms.NotifyIcon _icon;

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
    }

    public void Dispose()
    {
        _icon.Visible = false;
        _icon.Dispose();
    }
}
