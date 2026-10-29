using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using KnobHub.Core;
using KnobHub.Sdk;
using Windows.Foundation;
using Windows.Media.Control;

namespace KnobHub.Apps;

/// <summary>Built-in Media app: what's playing in Windows (Spotify, a browser, any app with media controls), with
/// its album art, play/pause, previous/next, and the knob seeking through the track.</summary>
internal sealed class MediaApp : KnobApp
{
    public static readonly AppManifest Manifest = new()
    {
        Id = "media",
        Name = "Media",
        Subtitle = "Now playing",
        Description = "Now playing and playback controls",
        Icon = "play",
        Color = "#4ADE80",
        Version = "0.1.0",
        Author = "Knob Hub",
    };

    private const string Screen = "now";
    private const int ArtSize = 80;  // on a 240 px screen (Knob OS's design size)
    private const double SeekStep = 5;  // seconds per click of the knob, as the knob shows it
    private static readonly TimeSpan SeekWindow = TimeSpan.FromSeconds(2);

    private GlobalSystemMediaTransportControlsSessionManager? _manager;
    private GlobalSystemMediaTransportControlsSession? _session;
    private TypedEventHandler<GlobalSystemMediaTransportControlsSessionManager, CurrentSessionChangedEventArgs>? _sessionChanged;

    // The album art on show, sent to the knob only when the picture changes. Kept by the thumbnail's own bytes, not by
    // the track: apps can change it for the same track (Chrome first gives its own logo, then the video's thumbnail).
    private string _artKey = "";
    private KnobImage _art = KnobImage.None;

    // Turning the knob quickly sends several seeks before the app reports its new position: each one builds on the
    // last target, not on the old position.
    private double _seekTarget;
    private DateTime _seekTime;

    public override async void Start()
    {
        try
        {
            _manager = await GlobalSystemMediaTransportControlsSessionManager.RequestAsync();
            _sessionChanged = OnSessionChanged;
            _manager.CurrentSessionChanged += _sessionChanged;
            GlobalSystemMediaTransportControlsSession? session = null;
            try { session = _manager.GetCurrentSession(); } catch (COMException) { }
            Watch(session);
        }
        catch (Exception ex)
        {
            Host.Log("Media controls aren't available: " + ex.Message);
        }
    }

    public override void Stop()
    {
        if (_manager != null && _sessionChanged != null) _manager.CurrentSessionChanged -= _sessionChanged;
        Watch(null);
        _manager = null;
    }

    private void OnSessionChanged(GlobalSystemMediaTransportControlsSessionManager sender, CurrentSessionChangedEventArgs args)
    {
        var manager = _manager;
        if (manager == null) return;
        try { Watch(manager.GetCurrentSession()); }
        catch (COMException) { Watch(null); }
    }

    public override async void Open()
    {
        try { Host.Show(Screen, await Describe(newScreen: true)); }
        catch (COMException) { Watch(null); }
        catch (Exception ex) { Host.Log(ex.Message); }
    }

    public override async void Input(KnobAppInput input)
    {
        var session = _session;
        try
        {
            switch (input.Kind, input.Item)
            {
                case (InputKind.Back, _):
                    Host.Close();
                    break;
                case (InputKind.Rotate, _):
                    if (session != null) await Seek(session, input.Delta * SeekStep);
                    break;
                case (InputKind.Tap, "play"):
                    if (session != null) await session.TryTogglePlayPauseAsync();
                    break;
                case (InputKind.Tap, "previous"):
                    if (session != null && !await session.TrySkipPreviousAsync()) Host.Log($"{session.SourceAppUserModelId} didn't skip back");
                    break;
                case (InputKind.Tap, "next"):
                    if (session != null && !await session.TrySkipNextAsync()) Host.Log($"{session.SourceAppUserModelId} didn't skip");
                    break;
            }
        }
        catch (COMException) { Watch(null); }
        catch (Exception ex) { Host.Log(ex.Message); }
    }

    private async Task Seek(GlobalSystemMediaTransportControlsSession session, double seconds)
    {
        var (position, duration) = Timeline(session);
        double from = DateTime.UtcNow - _seekTime < SeekWindow ? _seekTarget : position;
        _seekTarget = Math.Clamp(from + seconds, 0, duration > 0 ? duration : double.MaxValue);
        _seekTime = DateTime.UtcNow;
        await session.TryChangePlaybackPositionAsync((long)(_seekTarget * TimeSpan.TicksPerSecond));
    }

    private void Watch(GlobalSystemMediaTransportControlsSession? session)
    {
        if (_session != null)
        {
            try
            {
                _session.MediaPropertiesChanged -= OnTrackChanged;
                _session.PlaybackInfoChanged -= OnPlaybackChanged;
                _session.TimelinePropertiesChanged -= OnTimelineChanged;
            }
            catch (COMException) { }
        }
        _session = session;
        if (_session != null)
        {
            try
            {
                _session.MediaPropertiesChanged += OnTrackChanged;
                _session.PlaybackInfoChanged += OnPlaybackChanged;
                _session.TimelinePropertiesChanged += OnTimelineChanged;
            }
            catch (COMException)
            {
                _session = null;
            }
        }
        OnTrackChanged(null, null);
    }

    private async void OnTrackChanged(GlobalSystemMediaTransportControlsSession? sender, object? args)
    {
        if (!Host.IsOpen) return;
        try { Host.Update(Screen, await Describe(newScreen: false)); }
        catch (COMException) { Watch(null); }
        catch (Exception ex) { Host.Log(ex.Message); }
    }

    private void OnPlaybackChanged(GlobalSystemMediaTransportControlsSession sender, object args)
    {
        if (!Host.IsOpen) return;
        try
        {
            var screen = new MediaScreen();
            Playback(sender, screen);
            Host.Update(Screen, new { screen.Playing, screen.Position, screen.Duration, screen.CanSeek, screen.CanPrevious, screen.CanNext });
        }
        catch (COMException) { Watch(null); }
        catch (Exception ex) { Host.Log(ex.Message); }
    }

    private void OnTimelineChanged(GlobalSystemMediaTransportControlsSession sender, object args)
    {
        if (!Host.IsOpen) return;
        try
        {
            var (position, duration) = Timeline(sender);
            Host.Update(Screen, new { position, duration });
        }
        catch (COMException) { Watch(null); }
        catch (Exception ex) { Host.Log(ex.Message); }
    }

    /// <summary>Everything on the Media screen. The art goes with a new screen, or an update when the track changed
    /// (otherwise the knob keeps what it has).</summary>
    private async Task<MediaScreen> Describe(bool newScreen)
    {
        var screen = new MediaScreen { Color = Manifest.Color };
        var session = _session;
        if (session == null)
        {
            screen.Title = "Nothing playing";
            screen.Artist = "Play something on your PC";
            screen.CanPrevious = screen.CanNext = screen.CanSeek = false;
            _artKey = "";
            _art = KnobImage.None;
            screen.Art = _art;
            return screen;
        }

        GlobalSystemMediaTransportControlsSessionMediaProperties? media = null;
        try { media = await session.TryGetMediaPropertiesAsync(); }
        catch (Exception) { }
        screen.Title = string.IsNullOrWhiteSpace(media?.Title) ? "Unknown title" : media.Title;
        screen.Artist = media?.Artist ?? "";
        // Titles in other scripts (Malayalam, Hindi, Japanese...) go as pictures drawn with Windows' fonts.
        // At full length: the knob scrolls text that doesn't fit, pictures too.
        screen.TitleImage = Picture(screen.Title, 16, semibold: true);
        screen.ArtistImage = Picture(screen.Artist, 12);
        try
        {
            screen.Source = SourceName(session.SourceAppUserModelId);
            Playback(session, screen);
        }
        catch (COMException)
        {
            // The player went away: show nothing playing.
            Watch(null);
            return await Describe(newScreen);
        }

        // Read every time: apps (Spotify, Chrome) often add or replace the thumbnail a moment after the track starts.
        byte[]? thumbnail = await Thumbnail(media);
        // The screen's size is part of it: a knob of another size needs the art drawn again.
        string key = thumbnail == null ? "" : Convert.ToHexString(SHA256.HashData(thumbnail)) + "@" + Scaled(ArtSize);
        bool changed = key != _artKey;
        if (changed)
        {
            _artKey = key;
            _art = (thumbnail != null ? Art(thumbnail, media!.Title) : null) ?? KnobImage.None;
        }
        if (newScreen || changed) screen.Art = _art;
        return screen;
    }

    private KnobImage Picture(string text, double size, bool semibold = false) =>
        KnobText.CanDraw(text) ? KnobImage.None : TextPicture.Draw(text, size * Host.ScreenSize / 240.0, TextPicture.MaxWidth, semibold);

    /// <summary>A length on a 240 px screen, on this knob's.</summary>
    private int Scaled(int length) => (int)Math.Round(length * Host.ScreenSize / 240.0);

    private static void Playback(GlobalSystemMediaTransportControlsSession session, MediaScreen screen)
    {
        var playback = session.GetPlaybackInfo();
        screen.Playing = playback?.PlaybackStatus == GlobalSystemMediaTransportControlsSessionPlaybackStatus.Playing;
        // Apps (Spotify) report skipping as unavailable for a moment while changing tracks, and may not report it back
        // for a while; the buttons stay on and an app that can't skip just doesn't.
        screen.CanPrevious = screen.CanNext = true;
        screen.CanSeek = playback?.Controls.IsPlaybackPositionEnabled ?? false;
        (screen.Position, screen.Duration) = Timeline(session);
    }

    /// <summary>Seconds into the track (estimated from the last timeline update while playing) and its length.</summary>
    private static (double Position, double Duration) Timeline(GlobalSystemMediaTransportControlsSession session)
    {
        try
        {
            var timeline = session.GetTimelineProperties();
            double duration = Math.Max(0, (timeline.EndTime - timeline.StartTime).TotalSeconds);
            double position = (timeline.Position - timeline.StartTime).TotalSeconds;
            if (session.GetPlaybackInfo()?.PlaybackStatus == GlobalSystemMediaTransportControlsSessionPlaybackStatus.Playing)
                position += (DateTimeOffset.Now - timeline.LastUpdatedTime).TotalSeconds;
            return (Math.Round(Math.Clamp(position, 0, duration > 0 ? duration : double.MaxValue), 1), Math.Round(duration, 1));
        }
        catch (COMException) { throw; }
        catch (Exception) { return (0, 0); }
    }

    /// <summary>The track's thumbnail as the app gives it (an image file's bytes), or null if it has none.</summary>
    private async Task<byte[]?> Thumbnail(GlobalSystemMediaTransportControlsSessionMediaProperties? media)
    {
        if (media?.Thumbnail == null) return null;
        try
        {
            using var source = await media.Thumbnail.OpenReadAsync();
            using var bytes = new MemoryStream();
            await source.AsStreamForRead().CopyToAsync(bytes);
            return bytes.ToArray();
        }
        catch (Exception ex)
        {
            Host.Log($"Couldn't read the album art for \"{media.Title}\": {ex.Message}");
            return null;
        }
    }

    /// <summary>The thumbnail cropped to a square and scaled to the knob's art size. Whatever the player sends is shown
    /// (some send their own logo when they have no art); with nothing, the knob shows its music icon.</summary>
    private KnobImage? Art(byte[] thumbnail, string title)
    {
        try
        {
            using var stream = new MemoryStream(thumbnail);
            var frame = BitmapFrame.Create(stream, BitmapCreateOptions.None, BitmapCacheOption.OnLoad);
            int side = Math.Min(frame.PixelWidth, frame.PixelHeight);
            BitmapSource square = new CroppedBitmap(frame, new System.Windows.Int32Rect(
                (frame.PixelWidth - side) / 2, (frame.PixelHeight - side) / 2, side, side));
            int artSize = Scaled(ArtSize);
            double scale = (double)artSize / side;
            var scaled = new TransformedBitmap(square, new ScaleTransform(scale, scale));
            var bgra = new FormatConvertedBitmap(scaled, PixelFormats.Bgra32, null, 0);

            int w = Math.Min(artSize, bgra.PixelWidth), h = Math.Min(artSize, bgra.PixelHeight);
            var pixels = new byte[w * h * 4];
            bgra.CopyPixels(new System.Windows.Int32Rect(0, 0, w, h), pixels, w * 4, 0);
            return KnobImage.FromBgra32(pixels, w, h, w * 4);
        }
        catch (Exception ex)
        {
            Host.Log($"Couldn't read the album art for \"{title}\": {ex.Message}");
            return null;
        }
    }

    /// <summary>"Spotify.exe", "SpotifyAB.SpotifyMusic_zpdnekdrzrea0!Spotify", "MSEdge" -> a readable name.</summary>
    private static string SourceName(string id)
    {
        if (string.IsNullOrEmpty(id)) return "";
        string name = id;
        int bang = name.LastIndexOf('!');
        if (bang >= 0) name = name[(bang + 1)..];
        if (name.EndsWith(".exe", StringComparison.OrdinalIgnoreCase)) name = name[..^4];
        return name.ToLowerInvariant() switch
        {
            "msedge" => "Edge",
            "chrome" => "Chrome",
            "firefox" => "Firefox",
            "app" => "",
            _ => name.Length > 0 ? char.ToUpperInvariant(name[0]) + name[1..] : "",
        };
    }
}
