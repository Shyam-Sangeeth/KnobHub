using System.Windows;
using KnobHub.Apps;
using KnobHub.Core;

namespace KnobHub;

public partial class App : Application
{
    private const string InstanceName = "KnobHub.SingleInstance";
    private const string ShowEventName = "KnobHub.ShowWindow";

    private Mutex? _instanceMutex;
    private KnobSession? _session;
    private TrayIcon? _tray;
    private MainWindow? _window;
    private bool _exiting;

    internal KnobSession Session => _session ?? throw new InvalidOperationException("Knob Hub hasn't started.");

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        // As in AudioDesk: WPF's GPU path draws an empty white window on this machine; the software renderer is
        // plenty fast here.
        System.Windows.Media.RenderOptions.ProcessRenderMode = System.Windows.Interop.RenderMode.SoftwareOnly;

        // Second launch: ask the running copy to show its window, then quit.
        _instanceMutex = new Mutex(true, InstanceName, out bool isFirst);
        if (!isFirst)
        {
            try { EventWaitHandle.OpenExisting(ShowEventName).Set(); } catch { }
            Shutdown();
            return;
        }
        var showEvent = new EventWaitHandle(false, EventResetMode.AutoReset, ShowEventName);
        new Thread(() =>
        {
            while (showEvent.WaitOne())
                Dispatcher.BeginInvoke(ShowWindow);
        }) { IsBackground = true }.Start();

        var settings = HubSettings.Load();
        _session = new KnobSession(settings, new SimulatorConnection(), Dispatcher);
        _session.Logged += Log;
        var model = new MainViewModel(_session);
        _session.Start([(MediaApp.Manifest, () => new MediaApp())]);
        model.Refresh();

        _tray = new TrayIcon(IconHelper.LoadTrayIcon(), ShowWindow, ExitApp);
        _window = new MainWindow(model, settings.Window, bounds =>
        {
            settings.Window = bounds;
            settings.Save();
        });
        _window.Closing += (_, args) =>
        {
            settings.Window = WindowPlace.Save(_window) ?? settings.Window;
            settings.Save();
            // Closing the window keeps Knob Hub (and the knob's apps) running in the tray.
            if (_exiting) return;
            args.Cancel = true;
            _window.Hide();
        };

        if (!e.Args.Contains("--minimized")) ShowWindow();
    }

    /// <summary>%LOCALAPPDATA%\KnobHub\knobhub.log: connections, apps opened, app problems. Started afresh when it
    /// passes 1 MB.</summary>
    private static void Log(string message)
    {
        try
        {
            string path = System.IO.Path.Combine(HubSettings.Folder, "knobhub.log");
            System.IO.Directory.CreateDirectory(HubSettings.Folder);
            if (System.IO.File.Exists(path) && new System.IO.FileInfo(path).Length > 1_000_000) System.IO.File.Delete(path);
            System.IO.File.AppendAllText(path, $"{DateTime.Now:yyyy-MM-dd HH:mm:ss} {message}{Environment.NewLine}");
        }
        catch (Exception) { }
    }

    private void ShowWindow()
    {
        if (_window == null) return;
        _window.Show();
        if (_window.WindowState == WindowState.Minimized) _window.WindowState = WindowState.Normal;
        _window.Activate();
    }

    private void ExitApp()
    {
        _exiting = true;
        _window?.Close();
        _tray?.Dispose();
        _session?.Dispose();
        Shutdown();
    }

    protected override void OnExit(ExitEventArgs e)
    {
        _instanceMutex?.Dispose();
        base.OnExit(e);
    }
}
