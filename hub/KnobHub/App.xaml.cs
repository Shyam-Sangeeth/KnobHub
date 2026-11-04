using System.Windows;
using KnobHub.Apps;
using KnobHub.Core;

namespace KnobHub;

public partial class App : Application
{
    private const string InstanceName = "KnobHub.SingleInstance";
    private const string ShowEventName = "KnobHub.ShowWindow";
    private const string ExitEventName = "KnobHub.Exit";

    private Mutex? _instanceMutex;
    private KnobSession? _session;
    private TrayIcon? _tray;
    private MainWindow? _window;
    private HubUpdateModel? _updates;
    private bool _exiting;

    internal KnobSession Session => _session ?? throw new InvalidOperationException("Knob Hub hasn't started.");

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        // As in AudioDesk: WPF's GPU path draws an empty white window on this machine; the software renderer is
        // plenty fast here.
        System.Windows.Media.RenderOptions.ProcessRenderMode = System.Windows.Interop.RenderMode.SoftwareOnly;

        // Used by the installer: --exit closes a running Knob Hub (before an upgrade or uninstall) and waits for it;
        // --add-startup and --remove-startup set Start with Windows (the installer's checkbox, uninstalling).
        if (e.Args.Contains("--exit") || e.Args.Contains("--add-startup") || e.Args.Contains("--remove-startup"))
        {
            if (e.Args.Contains("--add-startup") || e.Args.Contains("--remove-startup"))
            {
                try { StartupSetting.Enabled = e.Args.Contains("--add-startup"); } catch { }
            }
            if (e.Args.Contains("--exit")) CloseRunningCopy();
            Shutdown();
            return;
        }

        // Second launch: ask the running copy to show its window, then quit.
        _instanceMutex = new Mutex(true, InstanceName, out bool isFirst);
        if (!isFirst)
        {
            try { EventWaitHandle.OpenExisting(ShowEventName).Set(); } catch { }
            // The other copy may be on its way out (it just started an update that started this one): give it a moment.
            try { isFirst = _instanceMutex.WaitOne(TimeSpan.FromSeconds(3)); }
            catch (AbandonedMutexException) { isFirst = true; }
            if (!isFirst)
            {
                Shutdown();
                return;
            }
        }
        var showEvent = new EventWaitHandle(false, EventResetMode.AutoReset, ShowEventName);
        new Thread(() =>
        {
            while (showEvent.WaitOne())
                Dispatcher.BeginInvoke(ShowWindow);
        }) { IsBackground = true }.Start();
        var exitEvent = new EventWaitHandle(false, EventResetMode.AutoReset, ExitEventName);
        new Thread(() =>
        {
            exitEvent.WaitOne();
            Dispatcher.BeginInvoke(ExitApp);
        }) { IsBackground = true }.Start();

        var settings = HubSettings.Load();
        var usb = new UsbConnection();
        var links = new KnobLinks(usb, new WifiConnection(() => settings.Paired), new SimulatorConnection());
        _session = new KnobSession(settings, links, Dispatcher, usb);
        _session.Logged += Log;
        _updates = new HubUpdateModel(settings, ExitApp);
        var model = new MainViewModel(_session, _updates);
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
        CheckForUpdatesLater();
    }

    /// <summary>If it's turned on, checks GitHub for a newer Knob Hub once, a little after it starts (so it doesn't slow
    /// down signing in), and says so in a tray notification that opens Settings.</summary>
    private async void CheckForUpdatesLater()
    {
        if (_updates == null || _session == null || !_session.HubSettings.CheckForUpdates) return;
        await Task.Delay(TimeSpan.FromSeconds(20));
        _updates.UpdateFound += update =>
        {
            if (_window is { IsVisible: true }) return;  // the Settings page shows it
            _tray?.ShowHint($"Knob Hub {update.Version} is available", "Click to open Settings and update.", () =>
            {
                ShowWindow();
                _window?.ShowSettings();
            });
        };
        await _updates.CheckAsync(quiet: true);
    }

    /// <summary>Asks a running Knob Hub to exit and waits (up to 10 s) until it has.</summary>
    private static void CloseRunningCopy()
    {
        try
        {
            EventWaitHandle.OpenExisting(ExitEventName).Set();
            using var mutex = new Mutex(false, InstanceName);
            try
            {
                if (mutex.WaitOne(TimeSpan.FromSeconds(10))) mutex.ReleaseMutex();
            }
            catch (AbandonedMutexException) { }
        }
        catch (WaitHandleCannotBeOpenedException) { }  // not running
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
