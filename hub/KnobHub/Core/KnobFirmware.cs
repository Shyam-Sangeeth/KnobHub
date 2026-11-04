using System.IO;
using System.Text.Json;

namespace KnobHub.Core;

/// <summary>A Knob OS image for one board that came with Knob Hub.</summary>
internal sealed record FirmwareImage(string Board, string Version, string Path);

/// <summary>
/// The Knob OS images that come with Knob Hub, in the firmware folder next to KnobHub.exe: knob-os-&lt;board&gt;.bin
/// for each board, and firmware.json with each board's file and Knob OS version. They're made by Knob OS's real-knob
/// builds (knob-os/scripts/merged.py) and copied in by the build (see the KnobFirmware target in KnobHub.csproj).
/// </summary>
internal static class KnobFirmware
{
    public static string Folder => Path.Combine(AppContext.BaseDirectory, "firmware");

    private static IReadOnlyDictionary<string, FirmwareImage>? _images;

    /// <summary>Every board's image, by board id; empty when Knob Hub was built without them.</summary>
    public static IReadOnlyDictionary<string, FirmwareImage> Images => _images ??= Load();

    public static FirmwareImage? For(string board) => Images.GetValueOrDefault(board);

    private static Dictionary<string, FirmwareImage> Load()
    {
        var images = new Dictionary<string, FirmwareImage>();
        try
        {
            using var json = JsonDocument.Parse(File.ReadAllText(Path.Combine(Folder, "firmware.json")));
            foreach (var board in json.RootElement.EnumerateObject())
            {
                string file = Path.Combine(Folder, board.Value.GetProperty("file").GetString() ?? "");
                string version = board.Value.GetProperty("version").GetString() ?? "";
                if (File.Exists(file) && version.Length > 0) images[board.Name] = new FirmwareImage(board.Name, version, file);
            }
        }
        catch (Exception ex) when (ex is IOException or JsonException or KeyNotFoundException or InvalidOperationException) { }
        return images;
    }

    /// <summary>Whether Knob OS <paramref name="a"/> is newer than <paramref name="b"/> ("1.2.0" style); an empty or
    /// unreadable <paramref name="b"/> counts as older.</summary>
    public static bool IsNewer(string a, string b) =>
        Version.TryParse(a, out var va) && (!Version.TryParse(b, out var vb) || va > vb);
}
