using System.Diagnostics;
using System.IO;
using System.Net.Http;
using System.Security.Cryptography;
using System.Text.Json;

namespace KnobHub.Core;

/// <summary>A newer Knob Hub release on GitHub.</summary>
internal sealed record HubUpdate(Version Version, string PageUrl, string InstallerUrl, string InstallerName, long InstallerSize,
                                 string? Sha256);

/// <summary>
/// Knob Hub's own updates: checks the latest release of Shyam-Sangeeth/KnobHub on GitHub, downloads its installer (the
/// .msi asset, checked against GitHub's SHA-256 digest) and runs it. The installer closes Knob Hub, upgrades it in place
/// (settings and apps are kept) and starts it again. Apps have their own releases (see <see cref="AppCatalog"/>).
/// </summary>
internal static class HubUpdates
{
    private const string LatestReleaseUrl = "https://api.github.com/repos/Shyam-Sangeeth/KnobHub/releases/latest";
    public const string ReleasesPage = "https://github.com/Shyam-Sangeeth/KnobHub/releases";

    private static readonly HttpClient Http = CreateClient();

    private static HttpClient CreateClient()
    {
        var client = new HttpClient { Timeout = TimeSpan.FromMinutes(10) };
        client.DefaultRequestHeaders.UserAgent.ParseAdd($"KnobHub/{CurrentVersion}");
        client.DefaultRequestHeaders.Accept.ParseAdd("application/vnd.github+json");
        return client;
    }

    /// <summary>This copy's version (major.minor.build).</summary>
    public static Version CurrentVersion
    {
        get
        {
            var v = typeof(HubUpdates).Assembly.GetName().Version ?? new Version(0, 0, 0);
            return new Version(v.Major, v.Minor, Math.Max(v.Build, 0));
        }
    }

    /// <summary>The latest release if it's newer than this copy and has an installer; otherwise null.</summary>
    public static async Task<HubUpdate?> CheckAsync(CancellationToken cancel = default)
    {
        using var response = await Http.GetAsync(LatestReleaseUrl, cancel);
        if (response.StatusCode == System.Net.HttpStatusCode.NotFound) return null;  // no releases yet
        response.EnsureSuccessStatusCode();

        using var json = JsonDocument.Parse(await response.Content.ReadAsStringAsync(cancel));
        var root = json.RootElement;
        if (root.TryGetProperty("draft", out var draft) && draft.GetBoolean()) return null;
        if (root.TryGetProperty("prerelease", out var pre) && pre.GetBoolean()) return null;

        string tag = root.GetProperty("tag_name").GetString() ?? "";
        if (!Version.TryParse(tag.TrimStart('v', 'V'), out var latest)) return null;
        latest = new Version(latest.Major, latest.Minor, Math.Max(latest.Build, 0));
        if (latest <= CurrentVersion) return null;

        foreach (var asset in root.GetProperty("assets").EnumerateArray())
        {
            string name = asset.GetProperty("name").GetString() ?? "";
            if (!name.EndsWith(".msi", StringComparison.OrdinalIgnoreCase)) continue;
            // GitHub publishes a "sha256:<hex>" digest for each asset: the download is checked against it.
            string? sha = asset.TryGetProperty("digest", out var digest) && digest.GetString() is { } d &&
                          d.StartsWith("sha256:", StringComparison.OrdinalIgnoreCase)
                ? d["sha256:".Length..]
                : null;
            return new HubUpdate(latest, root.GetProperty("html_url").GetString() ?? ReleasesPage,
                                 asset.GetProperty("browser_download_url").GetString()!, name, asset.GetProperty("size").GetInt64(), sha);
        }
        return null;  // a newer release without an installer can't be installed from here
    }

    /// <summary>Downloads the installer to a temp folder, reporting progress (0-1), and checks it.</summary>
    public static async Task<string> DownloadAsync(HubUpdate update, IProgress<double> progress, CancellationToken cancel = default)
    {
        string folder = Path.Combine(Path.GetTempPath(), "KnobHub", "update");
        Directory.CreateDirectory(folder);
        string path = Path.Combine(folder, Path.GetFileName(update.InstallerName));

        using (var response = await Http.GetAsync(update.InstallerUrl, HttpCompletionOption.ResponseHeadersRead, cancel))
        {
            response.EnsureSuccessStatusCode();
            await using var source = await response.Content.ReadAsStreamAsync(cancel);
            await using var target = File.Create(path);
            var buffer = new byte[81920];
            long done = 0;
            int read;
            while ((read = await source.ReadAsync(buffer, cancel)) > 0)
            {
                await target.WriteAsync(buffer.AsMemory(0, read), cancel);
                done += read;
                progress.Report(update.InstallerSize > 0 ? (double)done / update.InstallerSize : 0);
            }
        }

        if (new FileInfo(path).Length != update.InstallerSize)
            throw new InvalidDataException("The download is incomplete. Please try again.");
        if (update.Sha256 != null)
        {
            await using var stream = File.OpenRead(path);
            string actual = Convert.ToHexString(await SHA256.HashDataAsync(stream, cancel));
            if (!actual.Equals(update.Sha256, StringComparison.OrdinalIgnoreCase))
            {
                File.Delete(path);
                throw new InvalidDataException("The downloaded installer didn't pass its check and was deleted.");
            }
        }
        return path;
    }

    /// <summary>Starts the installer with only a progress bar. It closes Knob Hub itself before upgrading.</summary>
    public static void RunInstaller(string msiPath) =>
        Process.Start(new ProcessStartInfo("msiexec.exe", $"/i \"{msiPath}\" /passive") { UseShellExecute = true });
}
