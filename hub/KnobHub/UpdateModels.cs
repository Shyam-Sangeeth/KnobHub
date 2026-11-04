using System.Windows.Threading;
using KnobHub.Core;

namespace KnobHub;

/// <summary>
/// Settings > Knob OS: the knob's Knob OS against the one that came with Knob Hub, and installing it over USB: an
/// update on a knob running Knob OS, or a first install on a knob that isn't (its board is chosen here, since it can't
/// say what it is).
/// </summary>
internal sealed class KnobOsModel : Observable
{
    private readonly KnobSession _session;
    private readonly DispatcherTimer _watch;
    private string _message = "";
    private string _step = "";
    private double _progress;
    private KnobBoard? _board;

    public KnobOsModel(KnobSession session)
    {
        _session = session;
        session.StateChanged += () =>
        {
            if (session.KnobConnected) _message = "";  // the knob is back: what it runs says the rest
            OnAllChanged();
        };
        // Knobs that aren't running Knob OS don't connect, so look for them while the Settings page is on show.
        _watch = new DispatcherTimer { Interval = TimeSpan.FromSeconds(2) };
        _watch.Tick += (_, _) => OnAllChanged();
    }

    /// <summary>Whether the Settings page is on show (the only time it watches for knobs without Knob OS).</summary>
    public void Watch(bool shown)
    {
        if (shown) { OnAllChanged(); _watch.Start(); }
        else _watch.Stop();
    }

    private bool Connected => _session.KnobConnected;
    private bool OverUsb => Connected && _session.ConnectionKind == "USB";
    /// <summary>A knob that's plugged in but not running Knob OS (only looked for while none is connected over USB).</summary>
    private string? SilentPort => OverUsb ? null : _session.PortsWithoutKnobOs.FirstOrDefault();
    private string KnobName => _session.KnobSettings.Name;

    public bool Installing => _session.Installing;

    /// <summary>What the knob runs and what Knob Hub can install, in words.</summary>
    public string Status
    {
        get
        {
            if (Installing) return _step;
            if (_message.Length > 0) return _message;
            var images = KnobFirmware.Images;
            if (images.Count == 0)
                return "This copy of Knob Hub has no Knob OS to install (build Knob OS for the real knobs, then Knob Hub).";
            if (Connected)
            {
                string has = $"{KnobName} has Knob OS {_session.KnobOsVersion}";
                if (_session.ConnectionKind == "the simulator") return $"The simulator runs Knob OS {_session.KnobOsVersion}.";
                if (KnobFirmware.For(_session.Board.Id) is not { } image) return $"{has}. Knob Hub has no Knob OS for its board.";
                if (_session.KnobOsUpdate != null)
                    return OverUsb
                        ? $"{has}. Knob OS {image.Version} is ready to install: the knob restarts with it and keeps its settings, pairing and Wi-Fi."
                        : $"{has}. Knob OS {image.Version} is ready: plug the knob in over USB to install it.";
                return $"{has}, the latest.";
            }
            if (SilentPort is { } port)
                return $"The knob on {port} isn't running Knob OS. Choose its board, then install Knob OS on it.";
            return "No knob connected. Plug a knob in over USB to install Knob OS on it or update it.";
        }
    }

    /// <summary>The board picker, for a knob without Knob OS.</summary>
    public bool ChoosesBoard => !Installing && !Connected && SilentPort != null;
    public IReadOnlyList<KnobBoard> Boards { get; } = KnobBoard.Known.Where(b => KnobFirmware.For(b.Id) != null).ToList();

    public KnobBoard? Board
    {
        get => _board ??= Boards.FirstOrDefault(b => b.Id == _session.HubSettings.PreviewBoard) ?? Boards.FirstOrDefault();
        set { _board = value; OnAllChanged(); }
    }

    private (string Port, FirmwareImage Image)? Target =>
        OverUsb && _session.UsbPort is { } usb && KnobFirmware.For(_session.Board.Id) is { } image ? (usb, image)
        : !Connected && SilentPort is { } port && Board != null && KnobFirmware.For(Board.Id) is { } chosen ? (port, chosen)
        : null;

    public bool ShowsInstall => !Installing && Target != null;
    public string InstallText => Target is not { } target ? ""
        : Connected && _session.KnobOsUpdate == null ? "Install it again" : $"Install Knob OS {target.Image.Version}";

    /// <summary>For the confirmation: what's about to happen.</summary>
    public string InstallQuestion => Target is not { } target ? ""
        : Connected
            ? $"Install Knob OS {target.Image.Version} on {KnobName}? It restarts with it and keeps its settings, pairing and Wi-Fi. Keep it plugged in until it's done (about a minute)."
            : $"Install Knob OS {target.Image.Version} for the {KnobBoard.Find(target.Image.Board).Name} on the device on {target.Port}? What it runs now is replaced. Keep it plugged in until it's done (about a minute).";

    public double Progress
    {
        get => _progress;
        private set { _progress = value; OnPropertyChanged(); }
    }

    public async Task InstallAsync()
    {
        if (Target is not { } target || Installing) return;
        _message = "";
        Progress = 0;
        var progress = new Progress<KnobFlasher.Progress>(p =>
        {
            _step = p.Fraction > 0 && p.Fraction < 1 ? $"{p.Step}: {p.Fraction:P0}" : p.Step + "…";
            Progress = p.Fraction * 100;
            OnPropertyChanged(nameof(Status));
        });
        try
        {
            await _session.InstallKnobOsAsync(target.Port, target.Image, progress);
            _message = $"Knob OS {target.Image.Version} is installed. The knob is starting…";
        }
        catch (Exception ex)
        {
            _message = $"Knob OS wasn't installed: {ex.Message}";
        }
        OnAllChanged();
    }
}

/// <summary>Settings > Knob Hub: checks GitHub for a newer Knob Hub, downloads it and runs its installer.</summary>
internal sealed class HubUpdateModel(HubSettings settings, Action exitForUpdate) : Observable
{
    private HubUpdate? _update;
    private string _status = $"You have Knob Hub {HubUpdates.CurrentVersion}.";
    private bool _busy, _downloading;
    private double _progress;

    /// <summary>A check found a newer release.</summary>
    public event Action<HubUpdate>? UpdateFound;

    public string Status
    {
        get => _status;
        private set { _status = value; OnPropertyChanged(); }
    }

    public bool IsUpdateAvailable => _update != null;
    public bool CanCheck => !_busy;
    public bool CanInstall => _update != null && !_busy;
    public bool IsDownloading => _downloading;
    public string InstallText => _update == null ? "" : $"Download and install {_update.Version}";
    public string ReleaseUrl => _update?.PageUrl ?? HubUpdates.ReleasesPage;

    public double Progress
    {
        get => _progress;
        private set { _progress = value; OnPropertyChanged(); }
    }

    public bool AutoCheck
    {
        get => settings.CheckForUpdates;
        set
        {
            if (value == settings.CheckForUpdates) return;
            settings.CheckForUpdates = value;
            settings.Save();
            OnPropertyChanged();
        }
    }

    private void SetBusy(bool busy, bool downloading = false)
    {
        _busy = busy;
        _downloading = downloading;
        OnAllChanged();
    }

    /// <summary>Checks GitHub. <paramref name="quiet"/> keeps the message short (the check at startup).</summary>
    public async Task CheckAsync(bool quiet)
    {
        if (_busy) return;
        SetBusy(true);
        Status = "Checking for updates…";
        try
        {
            _update = await HubUpdates.CheckAsync();
            Status = _update == null
                ? $"You're up to date (Knob Hub {HubUpdates.CurrentVersion})."
                : $"Knob Hub {_update.Version} is available (you have {HubUpdates.CurrentVersion}).";
            if (_update != null) UpdateFound?.Invoke(_update);
        }
        catch (Exception ex)
        {
            Status = quiet ? "Couldn't check for updates." : $"Couldn't check for updates: {ex.Message}";
        }
        finally
        {
            SetBusy(false);
        }
    }

    /// <summary>Downloads and checks the installer, starts it, then closes Knob Hub so it can be replaced.</summary>
    public async Task InstallAsync()
    {
        if (_update == null || _busy) return;
        SetBusy(true, downloading: true);
        Progress = 0;
        Status = $"Downloading Knob Hub {_update.Version}…";
        try
        {
            string installer = await HubUpdates.DownloadAsync(_update, new Progress<double>(p => Progress = p * 100));
            Status = "Installing… Knob Hub starts again when it's done.";
            HubUpdates.RunInstaller(installer);
            await Task.Delay(1500);
            exitForUpdate();
        }
        catch (Exception ex)
        {
            Status = $"Update failed: {ex.Message}";
            SetBusy(false);
        }
    }
}
