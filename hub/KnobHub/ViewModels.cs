using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using KnobHub.Apps;
using KnobHub.Core;

namespace KnobHub;

internal abstract class Observable : INotifyPropertyChanged
{
    public event PropertyChangedEventHandler? PropertyChanged;

    protected void OnPropertyChanged([CallerMemberName] string? name = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));

    /// <summary>Raises PropertyChanged for every property (after the model changed underneath).</summary>
    protected void OnAllChanged() => OnPropertyChanged(string.Empty);
}

/// <summary>The window: the Knob page (live preview), Apps and Settings.</summary>
internal sealed class MainViewModel : Observable
{
    private readonly KnobSession _session;
    private readonly DispatcherTimer _sendSettings;

    public MainViewModel(KnobSession session)
    {
        _session = session;
        // Sliders change many times a second: send the knob the latest value at most every 80 ms.
        _sendSettings = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(80) };
        _sendSettings.Tick += (_, _) =>
        {
            _sendSettings.Stop();
            _session.SendSettings();
        };
        session.StateChanged += Refresh;
        session.SettingsChanged += Refresh;
        session.Preview.BrightnessChanged += () => OnPropertyChanged(nameof(DimOpacity));
        session.Apps.Changed += RefreshApps;
        session.Apps.Entries.CollectionChanged += (_, _) => RefreshApps();
    }

    public void Refresh()
    {
        OnAllChanged();
        RefreshApps();
    }

    public string HubVersion => "Knob Hub " + Assembly.GetExecutingAssembly().GetName().Version?.ToString(3);

    // --- Knob page ---

    public WriteableBitmap Screen => _session.Preview.Screen;
    public string KnobName => _session.KnobSettings.Name;
    public bool IsConnected => _session.KnobConnected;
    public string Status => IsConnected ? $"Connected to {_session.ConnectionKind}" : "No knob connected";
    public string Details => string.IsNullOrEmpty(_session.OsVersion) ? "" : $"Knob OS {_session.OsVersion}";
    public string PreviewMode => IsConnected ? "Mirroring the knob" : "Virtual knob: works like the real one";
    public string? PreviewError => _session.Preview.Error;
    /// <summary>The preview dims like the knob's screen (Knob OS's 5% is about 80% dark).</summary>
    public double DimOpacity => (100 - _session.Preview.Brightness) * 0.82 / 100;

    /// <summary>0 turn right, 1 turn left, 2 press, 3 back.</summary>
    public void PreviewInput(int input) => _session.PreviewInput(input);

    /// <summary>A touch on the preview, in screen pixels (0-239).</summary>
    public void PreviewTouch(int x, int y, bool down) => _session.PreviewTouch(x, y, down);

    // --- Apps page ---

    public ObservableCollection<AppItem> Apps { get; } = [];

    private void RefreshApps()
    {
        Apps.Clear();
        foreach (var entry in _session.Apps.Entries) Apps.Add(new AppItem(entry, _session.Apps));
        foreach (var item in Catalog) item.Refresh();
    }

    // --- Add apps page ---

    public ObservableCollection<CatalogItem> Catalog { get; } = [];

    /// <summary>While loading, or why there's nothing to list; "" when the list is there.</summary>
    public string CatalogStatus
    {
        get => _catalogStatus;
        private set
        {
            _catalogStatus = value;
            OnPropertyChanged();
            OnPropertyChanged(nameof(HasCatalogStatus));
        }
    }

    public bool HasCatalogStatus => CatalogStatus.Length > 0;

    /// <summary>The GitHub repository ("owner/name") the apps come from; changing it loads its list.</summary>
    public string AppsRepo
    {
        get => _session.HubSettings.AppsRepo;
        set
        {
            string repo = value.Trim();
            if (repo.Length == 0 || repo == AppsRepo) return;
            _session.HubSettings.AppsRepo = repo;
            _session.HubSettings.Save();
            OnPropertyChanged();
            _ = LoadCatalogAsync();
        }
    }

    private string _catalogStatus = "";
    private int _catalogLoad;  // a newer load makes an older one's result stale
    private bool _catalogLoaded;

    /// <summary>Lists the apps released in <see cref="AppsRepo"/>; with <paramref name="once"/>, only if that hasn't been
    /// done yet.</summary>
    public async Task LoadCatalogAsync(bool once = false)
    {
        if (once && _catalogLoaded) return;
        _catalogLoaded = true;
        int load = ++_catalogLoad;
        string repo = AppsRepo;
        Catalog.Clear();
        CatalogStatus = $"Looking for apps in {repo}…";
        string status;
        List<CatalogApp> apps = [];
        try
        {
            apps = await AppCatalog.LoadAsync(repo);
            status = apps.Count == 0 ? $"No apps have been released in {repo} yet." : "";
        }
        catch (System.Net.Http.HttpRequestException ex) when (ex.StatusCode == System.Net.HttpStatusCode.NotFound)
        {
            status = $"Couldn't find {repo} on GitHub. Check the name (owner/repository), and that it's public.";
        }
        catch (Exception ex)
        {
            status = $"Couldn't load the list: {ex.Message}";
        }
        if (load != _catalogLoad) return;
        foreach (var app in apps) Catalog.Add(new CatalogItem(app, _session.Apps));
        CatalogStatus = status;
    }

    // --- Settings page ---

    private KnobSettings Knob => _session.KnobSettings;

    public int Brightness { get => Knob.Brightness; set { Knob.Brightness = value; Changed(); } }
    public int LedBrightness { get => Knob.LedBrightness; set { Knob.LedBrightness = value; Changed(); } }
    public int LedMode { get => Knob.LedMode; set { Knob.LedMode = value; Changed(); } }
    public int Timeout { get => Knob.Timeout; set { Knob.Timeout = value; Changed(); } }
    public bool DimIdle { get => Knob.DimIdle; set { Knob.DimIdle = value; Changed(); } }
    public bool LedsIdleOff { get => Knob.LedsIdleOff; set { Knob.LedsIdleOff = value; Changed(); } }

    public string Name
    {
        get => Knob.Name;
        set
        {
            if (string.IsNullOrWhiteSpace(value)) return;
            Knob.Name = value.Trim();
            Changed();
            OnPropertyChanged(nameof(KnobName));
        }
    }

    /// <summary>The Home clock (on the Apps page's Clock row): 0 = 24-hour, 1 = 12-hour.</summary>
    public int ClockFormat
    {
        get => Knob.Clock24h ? 0 : 1;
        set { Knob.Clock24h = value == 0; Changed(); }
    }

    public string[] ClockFormats { get; } = ["24-hour", "12-hour"];

    public string[] LedModes => KnobSettings.LedModes;
    public string[] Timeouts => KnobSettings.Timeouts;

    public string ConnectionText => IsConnected
        ? $"Connected to {_session.ConnectionKind}. The preview mirrors it."
        : "No knob connected: the preview is a virtual knob. Start the Knob OS simulator to connect one.";

    public string FirmwareText => string.IsNullOrEmpty(_session.OsVersion)
        ? "Knob OS isn't running"
        : $"Knob OS {_session.OsVersion} on {KnobName}" + (IsConnected ? "" : " (the preview)");

    public bool StartWithWindows
    {
        get { try { return StartupSetting.Enabled; } catch { return false; } }
        set { try { StartupSetting.Enabled = value; } catch { } OnPropertyChanged(); }
    }

    private void Changed([CallerMemberName] string? name = null)
    {
        OnPropertyChanged(name);
        _sendSettings.Stop();
        _sendSettings.Start();
    }
}

/// <summary>An app on the Add apps page: from the GitHub releases, with whether it's installed or can be updated.</summary>
internal sealed class CatalogItem(CatalogApp app, AppManager manager) : Observable
{
    private static readonly Color Fallback = Color.FromRgb(0x8B, 0x7C, 0xF6);
    private bool _busy;

    public CatalogApp App => app;
    public string Name => app.Manifest.Name;
    public string Description => string.IsNullOrEmpty(app.Manifest.Description) ? app.Manifest.Subtitle : app.Manifest.Description;
    public string Details => string.Join(" · ", new[] { string.IsNullOrEmpty(app.Version) ? "" : "v" + app.Version,
                                                           string.IsNullOrEmpty(app.Manifest.Author) ? "" : "by " + app.Manifest.Author }
                                                    .Where(s => s.Length > 0));

    // An app's own icons are in its DLL: they can be drawn once it's installed; before that only a built-in one.
    public ImageSource? IconImage
    {
        get
        {
            KnobHub.Sdk.KnobIcon? icon = null;
            try { Installed?.App?.Icons.TryGetValue(app.Manifest.Icon, out icon); }
            catch (Exception) { }
            icon ??= KnobIcons.BuiltIn.GetValueOrDefault(app.Manifest.Icon);
            return icon != null ? IconHelper.Render(icon, IconHelper.ParseColor(app.Manifest.Color, Fallback)) : null;
        }
    }
    public bool ShowGlyph => IconImage == null;
    public string Glyph => IconHelper.Glyph(app.Manifest.Icon);
    public Brush Accent => Frozen(IconHelper.ParseColor(app.Manifest.Color, Fallback));
    public Brush AccentFill
    {
        get
        {
            var c = IconHelper.ParseColor(app.Manifest.Color, Fallback);
            return Frozen(Color.FromArgb(0x33, c.R, c.G, c.B));
        }
    }

    private AppEntry? Installed => manager.Entries.FirstOrDefault(e => e.Source != AppSource.System && e.Id == app.Id);
    private bool IsUpdate => Installed is { Source: AppSource.Plugin } e && AppCatalog.IsNewer(app.Version, e.Manifest.Version);

    /// <summary>Downloading and installing.</summary>
    public bool Busy
    {
        get => _busy;
        set
        {
            _busy = value;
            OnAllChanged();
        }
    }

    public bool CanInstall => !_busy && (Installed == null || IsUpdate);
    public string ActionText => _busy ? "Installing…" : Installed switch
    {
        null => "Install",
        { Source: AppSource.BuiltIn } => "Built in",
        _ when IsUpdate => "Update",
        _ => "Installed",
    };
    /// <summary>"v1.0.0 installed" while an update is offered.</summary>
    public string? InstalledText => IsUpdate ? $"v{Installed!.Manifest.Version} installed" : null;
    public bool HasInstalledText => InstalledText != null;

    /// <summary>The installed apps changed.</summary>
    public void Refresh() => OnAllChanged();

    private static SolidColorBrush Frozen(Color c)
    {
        var brush = new SolidColorBrush(c);
        brush.Freeze();
        return brush;
    }
}

/// <summary>One of an app's settings on its Apps page row.</summary>
internal sealed class AppSettingItem(AppEntry entry, KnobHub.Sdk.AppSetting setting, AppManager manager) : Observable
{
    public string Label => setting.Label;
    public IReadOnlyList<string> Options => setting.Options;

    public int Selected
    {
        get => AppManager.GetSetting(entry, setting.Id);
        set
        {
            if (value < 0 || value == Selected) return;
            manager.SetSetting(entry, setting.Id, value);
            OnPropertyChanged();
        }
    }
}

/// <summary>A row on the Apps page.</summary>
internal sealed class AppItem(AppEntry entry, AppManager manager) : Observable
{
    private static readonly Color Fallback = Color.FromRgb(0x8B, 0x7C, 0xF6);

    public AppEntry Entry => entry;
    public string Name => entry.Manifest.Name;
    public string Description => string.IsNullOrEmpty(entry.Manifest.Description) ? entry.Manifest.Subtitle : entry.Manifest.Description;
    public string Glyph => IconHelper.Glyph(entry.Manifest.Icon, entry.App?.Icons);

    /// <summary>The app's icon drawn the way the knob gets it (a glyph in any font, a picture or a path), in the app's
    /// colour; null for a name Knob Hub doesn't know (<see cref="Glyph"/> shows a generic one then).</summary>
    public ImageSource? IconImage => (_iconImage ??= new(DrawIcon)).Value;

    /// <summary>The generic glyph shows when the app's icon couldn't be drawn.</summary>
    public bool ShowGlyph => IconImage == null;

    private Lazy<ImageSource?>? _iconImage;

    private ImageSource? DrawIcon()
    {
        KnobHub.Sdk.KnobIcon? icon = null;
        try { entry.App?.Icons.TryGetValue(entry.Manifest.Icon, out icon); }
        catch (Exception) { }  // a broken plugin: the built-in icon or the generic glyph
        icon ??= KnobIcons.BuiltIn.GetValueOrDefault(entry.Manifest.Icon);
        return icon != null ? IconHelper.Render(icon, IconHelper.ParseColor(entry.Manifest.Color, Fallback)) : null;
    }
    public bool IsSystem => entry.Source == AppSource.System;
    public bool IsClock => entry.Source == AppSource.System && entry.Id == "clock";
    /// <summary>Clock and Settings are always on the knob.</summary>
    public bool CanTurnOff => !IsSystem;
    public bool CanUninstall => entry.Source == AppSource.Plugin;
    public string? Error => entry.Error;
    public bool HasError => entry.Error != null;
    public string ErrorText => $"Couldn't start: {entry.Error}";

    public string SourceLabel => entry.Source switch
    {
        AppSource.System => "System",
        AppSource.BuiltIn => "Built in",
        _ => "Plugin",
    };

    public string Details
    {
        get
        {
            var m = entry.Manifest;
            if (IsSystem) return "";
            string details = m.Version;
            if (!string.IsNullOrEmpty(m.Author)) details += (details.Length > 0 ? " · " : "") + "by " + m.Author;
            return details;
        }
    }

    public Brush Accent => Frozen(IconHelper.ParseColor(entry.Manifest.Color, Fallback));
    public Brush AccentFill
    {
        get
        {
            var c = IconHelper.ParseColor(entry.Manifest.Color, Fallback);
            return Frozen(Color.FromArgb(0x33, c.R, c.G, c.B));
        }
    }

    public bool Enabled
    {
        get => entry.Enabled;
        set
        {
            manager.SetEnabled(entry, value);
            OnPropertyChanged();
        }
    }

    /// <summary>This row is being dragged.</summary>
    public bool IsDragged
    {
        get => _isDragged;
        set { _isDragged = value; OnPropertyChanged(); }
    }

    /// <summary>Where a row dragged over this one would go: 0 nowhere, 1 above it, 2 below it.</summary>
    public int DropMark
    {
        get => _dropMark;
        set
        {
            if (value == _dropMark) return;
            _dropMark = value;
            OnPropertyChanged();
        }
    }

    private bool _isDragged;
    private int _dropMark;

    /// <summary>The app's own settings (while it's running), each a drop-down on its row.</summary>
    public List<AppSettingItem> Settings =>
        entry.App?.Settings.Select(s => new AppSettingItem(entry, s, manager)).ToList() ?? [];

    private static SolidColorBrush Frozen(Color c)
    {
        var brush = new SolidColorBrush(c);
        brush.Freeze();
        return brush;
    }
}
