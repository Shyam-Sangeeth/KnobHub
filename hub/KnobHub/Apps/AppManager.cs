using System.Collections.ObjectModel;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Runtime.Loader;
using System.Text.Json;
using KnobHub.Core;
using KnobHub.Sdk;

namespace KnobHub.Apps;

internal enum AppSource { System, BuiltIn, Plugin }

/// <summary>What an app is, from its app.json (plugins) or its code (built-in apps).</summary>
internal sealed class AppManifest
{
    public string Id { get; set; } = "";
    public string Name { get; set; } = "";
    public string Subtitle { get; set; } = "";
    public string Description { get; set; } = "";
    public string Icon { get; set; } = "";
    public string Color { get; set; } = "#8B7CF6";
    public string Version { get; set; } = "";
    public string Author { get; set; } = "";
    /// <summary>Plugins: the assembly file in the app's folder and the KnobApp subclass in it.</summary>
    public string Assembly { get; set; } = "";
    public string Type { get; set; } = "";
}

/// <summary>An app on the Apps page: Clock and Settings (Knob OS's own, always there) or a Knob Hub app.</summary>
internal sealed class AppEntry
{
    public required AppManifest Manifest { get; init; }
    public required AppSource Source { get; init; }
    public string? Folder { get; init; }
    public bool Enabled { get; set; } = true;
    /// <summary>The running app, while it's turned on.</summary>
    public KnobApp? App { get; set; }
    public AppHost? Host { get; set; }
    /// <summary>Why it couldn't start, or null.</summary>
    public string? Error { get; set; }
    /// <summary>The app's settings that were changed from their defaults: id to option index. Lock it to use it (the
    /// app reads it from any thread).</summary>
    public Dictionary<string, int> Settings { get; set; } = [];
    public AssemblyLoadContext? LoadContext { get; set; }

    public string Id => Manifest.Id;
}

/// <summary>Finds, installs, orders and runs the apps. Plugins live in %LOCALAPPDATA%\KnobHub\Apps\&lt;id&gt;.</summary>
internal sealed class AppManager
{
    public static readonly string PluginsFolder = Path.Combine(HubSettings.Folder, "Apps");

    private readonly HubSettings _settings;
    private readonly Func<AppEntry, AppHost> _makeHost;
    private readonly Dictionary<string, Func<KnobApp>> _builtInFactories = [];

    public AppManager(HubSettings settings, Func<AppEntry, AppHost> makeHost)
    {
        _settings = settings;
        _makeHost = makeHost;
    }

    /// <summary>The apps in Home ring order, Clock and Settings among them.</summary>
    public ObservableCollection<AppEntry> Entries { get; } = [];

    public event Action? Changed;

    /// <summary>The Knob Hub apps that are on the knob, in order.</summary>
    public IEnumerable<AppEntry> OnKnob => Entries.Where(e => e.Source != AppSource.System && e.Enabled && e.App != null);

    public AppEntry? Find(string id) => Entries.FirstOrDefault(e => e.Id == id && e.Source != AppSource.System);

    public void Load(IEnumerable<(AppManifest Manifest, Func<KnobApp> Create)> builtIn)
    {
        var apps = new List<AppEntry>();
        foreach (var (manifest, create) in builtIn)
        {
            _builtInFactories[manifest.Id] = create;
            apps.Add(new AppEntry { Manifest = manifest, Source = AppSource.BuiltIn });
        }
        if (Directory.Exists(PluginsFolder))
        {
            foreach (string folder in Directory.GetDirectories(PluginsFolder))
            {
                var manifest = ReadManifest(folder);
                if (manifest != null && apps.All(a => a.Id != manifest.Id))
                    apps.Add(new AppEntry { Manifest = manifest, Source = AppSource.Plugin, Folder = folder });
            }
        }

        // The saved order (Clock and Settings in it too); until there is one, Clock first and Settings last. Apps not
        // in it yet go just before Settings.
        var clock = SystemEntry("clock", "Clock", "Time and date", "clock", "#F2F3F5");
        var settings = SystemEntry("settings", "Settings", "Brightness, lights and connection", "settings", "#9AA0AA");
        var all = apps.Prepend(clock).Append(settings).ToList();
        var ordered = _settings.Apps.Select(a => all.FirstOrDefault(e => e.Id == a.Id)).OfType<AppEntry>().Distinct().ToList();
        if (!ordered.Contains(clock)) ordered.Insert(0, clock);
        if (!ordered.Contains(settings)) ordered.Add(settings);
        foreach (var entry in apps.Where(a => !ordered.Contains(a))) ordered.Insert(ordered.IndexOf(settings), entry);

        Entries.Clear();
        foreach (var entry in ordered)
        {
            Entries.Add(entry);
            if (entry.Source == AppSource.System) continue;
            var saved = _settings.Apps.FirstOrDefault(a => a.Id == entry.Id);
            entry.Enabled = saved?.Enabled ?? true;
            entry.Settings = new(saved?.Settings ?? []);
            if (entry.Enabled) StartApp(entry);
        }
        SaveOrder();
    }

    /// <summary>The Home ring's order for the knob: Clock, Settings and the apps that are on it.</summary>
    public IEnumerable<string> RingOrder =>
        Entries.Where(e => e.Source == AppSource.System || (e.Enabled && e.App != null)).Select(e => e.Id);

    public void SetEnabled(AppEntry entry, bool enabled)
    {
        if (entry.Source == AppSource.System || entry.Enabled == enabled) return;
        entry.Enabled = enabled;
        if (enabled) StartApp(entry);
        else StopApp(entry);
        SaveOrder();
        Changed?.Invoke();
    }

    /// <summary>One of the app's settings: the chosen option, or the setting's default until it's changed.</summary>
    public static int GetSetting(AppEntry entry, string id)
    {
        var setting = entry.App?.Settings.FirstOrDefault(s => s.Id == id);
        int value;
        lock (entry.Settings)
            if (!entry.Settings.TryGetValue(id, out value)) value = setting?.Default ?? 0;
        return setting != null ? Math.Clamp(value, 0, setting.Options.Count - 1) : value;
    }

    public void SetSetting(AppEntry entry, string id, int value)
    {
        lock (entry.Settings) entry.Settings[id] = value;
        SaveOrder();
        try { entry.App?.SettingChanged(id); }
        catch (Exception ex) { entry.Host?.Log($"Couldn't change {id}: {ex.Message}"); }
    }

    /// <summary>Moves an app (Clock and Settings too) to another place on the Home ring.</summary>
    public void MoveTo(AppEntry entry, int index)
    {
        int from = Entries.IndexOf(entry), to = Math.Clamp(index, 0, Entries.Count - 1);
        if (from < 0 || from == to) return;
        Entries.Move(from, to);
        SaveOrder();
        Changed?.Invoke();
    }

    /// <summary>Reads a .knobapp (a zip with app.json and the app's files) without installing it.</summary>
    public static AppManifest ReadPackage(string path)
    {
        using var zip = ZipFile.OpenRead(path);
        var entry = zip.GetEntry("app.json") ?? throw new InvalidDataException("This isn't a Knob Hub app: app.json is missing.");
        using var stream = entry.Open();
        var manifest = JsonSerializer.Deserialize<AppManifest>(stream, Json.Files) ?? throw new InvalidDataException("app.json is empty.");
        Validate(manifest);
        return manifest;
    }

    /// <summary>Installs (or updates) a .knobapp and turns it on.</summary>
    public AppEntry Install(string path)
    {
        var manifest = ReadPackage(path);
        if (Entries.Any(e => e.Id == manifest.Id && e.Source != AppSource.Plugin))
            throw new InvalidOperationException($"\"{manifest.Id}\" is already used by an app that comes with Knob Hub.");

        // An update stays where it was; a new app goes just before Settings.
        var old = Entries.FirstOrDefault(e => e.Id == manifest.Id);
        int settingsAt = Entries.ToList().FindIndex(e => e.Source == AppSource.System && e.Id == "settings");
        int index = old != null ? Entries.IndexOf(old) : settingsAt >= 0 ? settingsAt : Entries.Count;
        if (old != null) RemoveEntry(old);

        string folder = Path.Combine(PluginsFolder, manifest.Id);
        if (Directory.Exists(folder)) Directory.Delete(folder, recursive: true);
        Directory.CreateDirectory(folder);
        ZipFile.ExtractToDirectory(path, folder);

        // An update keeps the app's settings.
        var entry = new AppEntry { Manifest = manifest, Source = AppSource.Plugin, Folder = folder, Settings = old?.Settings ?? [] };
        Entries.Insert(index, entry);
        StartApp(entry);
        SaveOrder();
        Changed?.Invoke();
        return entry;
    }

    public void Uninstall(AppEntry entry)
    {
        if (entry.Source != AppSource.Plugin) return;
        RemoveEntry(entry);
        try { if (entry.Folder != null) Directory.Delete(entry.Folder, recursive: true); }
        catch (IOException) { }  // still loaded until Knob Hub restarts: the folder goes then
        catch (UnauthorizedAccessException) { }
        SaveOrder();
        Changed?.Invoke();
    }

    public void StopAll()
    {
        foreach (var entry in Entries) StopApp(entry);
    }

    private void RemoveEntry(AppEntry entry)
    {
        StopApp(entry);
        Entries.Remove(entry);
        entry.LoadContext?.Unload();
        entry.LoadContext = null;
    }

    private void StartApp(AppEntry entry)
    {
        if (entry.App != null || entry.Source == AppSource.System) return;
        try
        {
            var app = entry.Source == AppSource.BuiltIn ? _builtInFactories[entry.Id]() : LoadPlugin(entry);
            entry.Host = _makeHost(entry);
            app.Attach(entry.Host);
            entry.App = app;  // before Start, so the app can read its settings there
            app.Start();
            entry.Error = null;
        }
        catch (Exception ex)
        {
            entry.App = null;
            entry.Error = ex is TargetInvocationException { InnerException: { } inner } ? inner.Message : ex.Message;
        }
    }

    private static void StopApp(AppEntry entry)
    {
        if (entry.App == null) return;
        if (entry.Host is { IsOpen: true }) entry.Host.Close();
        try { entry.App.Stop(); } catch (Exception) { }
        entry.App = null;
        entry.Host = null;
    }

    private static KnobApp LoadPlugin(AppEntry entry)
    {
        string path = Path.Combine(entry.Folder!, entry.Manifest.Assembly);
        var context = entry.LoadContext ??= new PluginLoadContext(path);
        var assembly = context.LoadFromAssemblyPath(path);
        var type = assembly.GetType(entry.Manifest.Type, throwOnError: true)!;
        if (!typeof(KnobApp).IsAssignableFrom(type)) throw new InvalidDataException($"{type.Name} isn't a KnobApp.");
        return (KnobApp)Activator.CreateInstance(type)!;
    }

    private static AppManifest? ReadManifest(string folder)
    {
        try
        {
            var manifest = JsonSerializer.Deserialize<AppManifest>(File.ReadAllText(Path.Combine(folder, "app.json")), Json.Files);
            if (manifest == null) return null;
            Validate(manifest);
            return manifest;
        }
        catch (Exception) { return null; }
    }

    private static void Validate(AppManifest m)
    {
        if (string.IsNullOrWhiteSpace(m.Id) || m.Id.Any(c => !(char.IsAsciiLetterOrDigit(c) || c is '-' or '_' or '.')))
            throw new InvalidDataException("app.json needs an id made of letters, digits, '-', '_' or '.'.");
        if (string.IsNullOrWhiteSpace(m.Name)) throw new InvalidDataException("app.json needs a name.");
        if (string.IsNullOrWhiteSpace(m.Assembly) || string.IsNullOrWhiteSpace(m.Type))
            throw new InvalidDataException("app.json needs the app's assembly and type.");
        if (Path.GetFileName(m.Assembly) != m.Assembly) throw new InvalidDataException("app.json's assembly must be a file name.");
    }

    private static AppEntry SystemEntry(string id, string name, string subtitle, string icon, string color) => new()
    {
        Manifest = new AppManifest { Id = id, Name = name, Subtitle = subtitle, Description = subtitle, Icon = icon, Color = color },
        Source = AppSource.System,
    };

    private void SaveOrder()
    {
        _settings.Apps = Entries.Select(e => new AppState { Id = e.Id, Enabled = e.Enabled, Settings = SavedSettings(e) }).ToList();
        _settings.Save();
    }

    private static Dictionary<string, int>? SavedSettings(AppEntry entry)
    {
        lock (entry.Settings) return entry.Settings.Count > 0 ? new(entry.Settings) : null;
    }
}

/// <summary>Loads a plugin and its own dependencies from its folder; KnobHub.Sdk comes from Knob Hub so the
/// plugin's KnobApp is the same type.</summary>
internal sealed class PluginLoadContext(string mainAssembly) : AssemblyLoadContext(isCollectible: true)
{
    private readonly AssemblyDependencyResolver _resolver = new(mainAssembly);

    protected override Assembly? Load(AssemblyName name)
    {
        if (name.Name == typeof(KnobApp).Assembly.GetName().Name) return null;
        string? path = _resolver.ResolveAssemblyToPath(name);
        return path != null ? LoadFromAssemblyPath(path) : null;
    }

    protected override IntPtr LoadUnmanagedDll(string name)
    {
        string? path = _resolver.ResolveUnmanagedDllToPath(name);
        return path != null ? LoadUnmanagedDllFromPath(path) : IntPtr.Zero;
    }
}
