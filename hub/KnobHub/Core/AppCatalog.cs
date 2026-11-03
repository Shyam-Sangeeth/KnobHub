using System.IO;
using System.Net.Http;
using System.Text.Json;
using System.Text.Json.Nodes;
using KnobHub.Apps;

namespace KnobHub.Core;

/// <summary>An app offered on the Add apps page: the newest release of it.</summary>
internal sealed record CatalogApp(AppManifest Manifest, string DownloadUrl, string ReleaseUrl)
{
    public string Id => Manifest.Id;
    public string Version => Manifest.Version;
}

/// <summary>
/// Knob Hub apps published as the GitHub releases of a repository of their own (Shyam-Sangeeth/KnobHub-Apps, apart
/// from Knob Hub's own releases). Each release is one app, tagged with its id, holding every version of it: the app
/// ("&lt;id&gt;-&lt;version&gt;.knobapp") and its app.json ("&lt;id&gt;-&lt;version&gt;.app.json"), so the list shows names,
/// icons and versions without downloading every app. The older layout, one release per version tagged
/// "&lt;id&gt;-v&lt;version&gt;" with "&lt;id&gt;.knobapp" and "&lt;id&gt;.app.json", is read too. Drafts and pre-releases are
/// left out.
/// </summary>
internal static class AppCatalog
{
    private static readonly HttpClient Http = MakeClient();

    private static HttpClient MakeClient()
    {
        var http = new HttpClient { Timeout = TimeSpan.FromSeconds(30) };
        http.DefaultRequestHeaders.UserAgent.ParseAdd("KnobHub");  // GitHub's API needs one
        http.DefaultRequestHeaders.Accept.ParseAdd("application/vnd.github+json");
        return http;
    }

    /// <summary>The newest release of each app in <paramref name="repo"/> ("owner/name"), by name.</summary>
    public static async Task<List<CatalogApp>> LoadAsync(string repo, CancellationToken cancel = default)
    {
        string json = await Http.GetStringAsync($"https://api.github.com/repos/{repo}/releases?per_page=100", cancel);
        // The newest version of each app, by the files' names; only its app.json is downloaded.
        var newest = new Dictionary<string, (string Version, JsonObject Package, JsonObject? Manifest, JsonObject Release)>(
            StringComparer.OrdinalIgnoreCase);
        foreach (var release in (JsonNode.Parse(json) as JsonArray ?? []).OfType<JsonObject>())
        {
            if (Flag(release, "draft") || Flag(release, "prerelease")) continue;
            var assets = (release["assets"] as JsonArray ?? []).OfType<JsonObject>().ToList();
            foreach (var package in assets.Where(a => a.Str("name").EndsWith(".knobapp", StringComparison.OrdinalIgnoreCase)))
            {
                string name = Path.GetFileNameWithoutExtension(package.Str("name"));
                var (id, version) = SplitName(name, release.Str("tag_name"));
                var manifestAsset = assets.FirstOrDefault(a => a.Str("name").Equals(name + ".app.json", StringComparison.OrdinalIgnoreCase));
                if (!newest.TryGetValue(id, out var known) || IsNewer(version, known.Version))
                    newest[id] = (version, package, manifestAsset, release);
            }
        }

        var apps = new List<CatalogApp>();
        foreach (var (id, (version, package, manifestAsset, release)) in newest)
        {
            var manifest = manifestAsset != null ? await ReadManifest(manifestAsset.Str("browser_download_url"), cancel) : null;
            manifest ??= new AppManifest { Id = id, Name = string.IsNullOrEmpty(release.Str("name")) ? id : release.Str("name") };
            if (string.IsNullOrEmpty(manifest.Version)) manifest.Version = version;
            apps.Add(new CatalogApp(manifest, package.Str("browser_download_url"), release.Str("html_url")));
        }
        return apps.OrderBy(a => a.Manifest.Name, StringComparer.CurrentCultureIgnoreCase).ToList();
    }

    /// <summary>"audiodesk-1.2.0" -> ("audiodesk", "1.2.0"). A name without a version ("audiodesk", the older layout)
    /// takes it from the release's tag.</summary>
    private static (string Id, string Version) SplitName(string name, string tag)
    {
        int dash = name.LastIndexOf('-');
        if (dash > 0 && System.Version.TryParse(name[(dash + 1)..], out _)) return (name[..dash], name[(dash + 1)..]);
        return (name, VersionFromTag(tag));
    }

    /// <summary>Downloads the app to a temporary file (the caller installs it and deletes the file).</summary>
    public static async Task<string> DownloadAsync(CatalogApp app, CancellationToken cancel = default)
    {
        if (!Uri.TryCreate(app.DownloadUrl, UriKind.Absolute, out var url) || url.Scheme != Uri.UriSchemeHttps)
            throw new InvalidOperationException("The app's download address isn't a secure web address.");
        string path = Path.Combine(Path.GetTempPath(), $"knobhub-{app.Id}-{Guid.NewGuid():N}.knobapp");
        await using var source = await Http.GetStreamAsync(url, cancel);
        await using var file = File.Create(path);
        await source.CopyToAsync(file, cancel);
        return path;
    }

    /// <summary>Whether version <paramref name="a"/> is newer than <paramref name="b"/> ("1.2.0" style; otherwise compared as
    /// text).</summary>
    public static bool IsNewer(string a, string b) =>
        System.Version.TryParse(a, out var va) && System.Version.TryParse(b, out var vb) ? va > vb : string.CompareOrdinal(a, b) > 0;

    private static async Task<AppManifest?> ReadManifest(string url, CancellationToken cancel)
    {
        try { return JsonSerializer.Deserialize<AppManifest>(await Http.GetStringAsync(url, cancel), Json.Files); }
        catch (Exception) { return null; }  // listed with what the release itself says
    }

    // "audiodesk-v1.2.0" -> "1.2.0"
    private static string VersionFromTag(string tag)
    {
        int v = tag.LastIndexOf("-v", StringComparison.OrdinalIgnoreCase);
        return v >= 0 ? tag[(v + 2)..] : tag.TrimStart('v', 'V');
    }

    private static bool Flag(JsonObject o, string name) => o[name] is JsonValue v && v.TryGetValue(out bool b) && b;
}
