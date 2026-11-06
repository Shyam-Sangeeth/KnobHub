// Tests Knob Hub's and the apps' code directly, without a knob or a window: the Timer app, the AudioDesk app (with
// made-up channels; it never connects to AudioDesk), the screen timeout setting, app ids, the Add apps catalog (reads
// GitHub) and the updater's checksum (downloads a Knob OS 1.0.2 image from GitHub). Build Knob Hub (Debug) first, then
// from the repository root:
//     dotnet run --project tools\hub-tests
// Exits with the number of failures.
using System.IO;
using System.Net.Http;
using System.Reflection;
using System.Text.Json;
using KnobHub.Sdk;

const BindingFlags Any = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static;
int failed = 0;
void Expect(string what, object? got, object? wanted)
{
    bool ok = Equals(got, wanted);
    if (!ok) failed++;
    Console.WriteLine($"{(ok ? "pass" : "FAIL")}  {what}: got {got ?? "null"}{(ok ? "" : $", wanted {wanted ?? "null"}")}");
}
object? Get(object o, string field) => o.GetType().GetField(field, Any)!.GetValue(o);
void Set(object o, string field, object? v) => o.GetType().GetField(field, Any)!.SetValue(o, v);

// ---- Timer: a press in the instant it runs out ----
{
    var host = new FakeHost();
    var timer = new Timer.KnobApp.TimerApp();
    timer.Attach(host);
    timer.Open();
    timer.Input(new KnobAppInput("timer", InputKind.Press, "", 0));  // start
    Set(timer, "_endsAt", DateTime.UtcNow.AddMilliseconds(-1));      // it has run out; the tick hasn't seen it yet
    timer.Input(new KnobAppInput("timer", InputKind.Press, "", 0));  // the press in that instant
    Expect("Timer: not paused at a negative time", Get(timer, "_pausedLeft"), null);
    Thread.Sleep(600);  // the next tick
    Expect("Timer: the tick says Time's up", host.Toasts.Contains("Time's up"), true);
    Expect("Timer: stopped", Get(timer, "_endsAt"), null);
    timer.Input(new KnobAppInput("timer", InputKind.Press, "", 0));  // a press now starts it again from the set time
    var left = (DateTime?)Get(timer, "_endsAt") - DateTime.UtcNow;
    Expect("Timer: starts again with the set time (about 5 min)", left > TimeSpan.FromMinutes(4.9), true);
    timer.Stop();
    // An ordinary pause still works
    var t2 = new Timer.KnobApp.TimerApp();
    t2.Attach(new FakeHost());
    t2.Open();
    t2.Input(new KnobAppInput("timer", InputKind.Press, "", 0));
    t2.Input(new KnobAppInput("timer", InputKind.Press, "", 0));
    var paused = (TimeSpan?)Get(t2, "_pausedLeft");
    Expect("Timer: an ordinary pause keeps the time left", paused > TimeSpan.FromMinutes(4.9), true);
    t2.Stop();
}

// ---- AudioDesk: per-channel turn target, no Muted while turning (never started: no pipe to the real AudioDesk) ----
{
    var host = new FakeHost();
    var app = new AudioDesk.KnobApp.AudioDeskApp();
    app.Attach(host);
    var client = Get(app, "_client")!;
    var asm = client.GetType().Assembly;
    const string Json = """
    {"sonar_available":false,"sonar":false,"profile":"headset","channels":[
      {"id":"game","volume":0.5,"muted":true,"sonar_available":false,"through_sonar":false,"switching":false,"device":"a","devices":[{"id":"a","name":"Speakers","format":""}]},
      {"id":"media","volume":0.2,"muted":false,"sonar_available":false,"through_sonar":false,"switching":false,"device":"a","devices":[{"id":"a","name":"Speakers","format":""}]}]}
    """;
    var stateType = asm.GetType("AudioDesk.KnobApp.AudioDeskState")!;
    var state = JsonSerializer.Deserialize(Json, stateType,
        new JsonSerializerOptions { PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower, PropertyNameCaseInsensitive = true });
    client.GetType().GetProperty("State")!.SetValue(client, state);
    app.Open();
    app.Input(new KnobAppInput("channels", InputKind.Press, "game", 0));
    string game = host.LastShown;
    app.Input(new KnobAppInput(game, InputKind.Rotate, "", 1));  // game: 50% -> 52% (2% step), unmutes
    Expect("AudioDesk: game's target", Math.Round((double)Get(app, "_volumeTarget")!, 3), 0.52);
    host.Updates.Clear();
    app.GetType().GetMethod("OnChanged", Any)!.Invoke(app, null);  // AudioDesk still says muted for a moment
    string update = host.Updates.LastOrDefault() ?? "";
    Expect("AudioDesk: an update while turning has no muted (no flash)", update.Contains("muted"), false);
    Expect("AudioDesk: ...nor value", update.Contains("\"value\""), false);
    app.Input(new KnobAppInput(game, InputKind.Back, "", 0));
    app.Input(new KnobAppInput("channels", InputKind.Press, "media", 0));
    string media = host.LastShown;
    app.Input(new KnobAppInput(media, InputKind.Rotate, "", 1));  // within 0.8 s: media 20% -> 22%, not 52% -> 54%
    Expect("AudioDesk: media starts from its own volume", Math.Round((double)Get(app, "_volumeTarget")!, 3), 0.22);
    Thread.Sleep(900);
    host.Updates.Clear();
    app.GetType().GetMethod("OnChanged", Any)!.Invoke(app, null);
    update = host.Updates.LastOrDefault() ?? "";
    Expect("AudioDesk: after turning, updates carry muted and value again", update.Contains("muted") && update.Contains("\"value\""), true);
}

// ---- Screen timeout: seconds now; the older position still loads and goes out ----
{
    var hubAsm = Assembly.Load("KnobHub");
    var settingsType = hubAsm.GetType("KnobHub.Core.KnobSettings")!;
    var files = (JsonSerializerOptions)hubAsm.GetType("KnobHub.Core.Json")!.GetField("Files", Any)!.GetValue(null)!;
    int Seconds(object k) => (int)settingsType.GetProperty("TimeoutSeconds")!.GetValue(k)!;
    foreach (var (json, wanted) in new[] { ("{\"timeout\":0}", 60), ("{\"timeout\":3}", 300), ("{\"timeout\":4}", 0), ("{}", 60),
                                           ("{\"timeout\":3,\"timeoutSeconds\":1200}", 1200) })
        Expect($"Timeout: a settings file {json}", Seconds(JsonSerializer.Deserialize(json, settingsType, files)!), wanted);
    var knob = Activator.CreateInstance(settingsType)!;
    settingsType.GetProperty("TimeoutSeconds")!.SetValue(knob, 600);
    string saved = JsonSerializer.Serialize(knob, settingsType, files);
    Expect("Timeout: saved and loaded again", Seconds(JsonSerializer.Deserialize(saved, settingsType, files)!), 600);
    var message = (System.Text.Json.Nodes.JsonObject)settingsType.GetMethod("ToMessage")!.Invoke(knob, null)!;
    Expect("Timeout: sent as seconds and as the older position", $"{message["timeout_seconds"]} {message["timeout"]}", "600 3");
    var apply = settingsType.GetMethod("Apply")!;
    apply.Invoke(knob, [new System.Text.Json.Nodes.JsonObject { ["timeout_seconds"] = 3600, ["timeout"] = 3 }]);
    Expect("Timeout: a knob's seconds win over its older position", Seconds(knob), 3600);
    apply.Invoke(knob, [new System.Text.Json.Nodes.JsonObject { ["timeout"] = 1 }]);
    Expect("Timeout: a knob before 1.0.6 (30 s) becomes 1 minute", Seconds(knob), 60);
}

// ---- Add apps: id check, and the catalog still loads ----
{
    var hubAsm = Assembly.Load("KnobHub");
    var manager = hubAsm.GetType("KnobHub.Apps.AppManager")!;
    var isValid = manager.GetMethod("IsValidId", Any)!;
    foreach (var (id, ok) in new[] { ("audiodesk", true), ("my.app-2_x", true), ("..\\..\\evil", false), ("a/b", false), ("..", false),
                                     (".hidden", false), ("a..b", false), ("", false) })
        Expect($"IsValidId(\"{id}\")", isValid.Invoke(null, [id]), ok);
    var catalog = hubAsm.GetType("KnobHub.Core.AppCatalog")!;
    var task = (Task)catalog.GetMethod("LoadAsync", Any)!.Invoke(null, ["Shyam-Sangeeth/KnobHub-Apps", CancellationToken.None])!;
    await task;
    var apps = ((System.Collections.IEnumerable)task.GetType().GetProperty("Result")!.GetValue(task)!).Cast<object>()
        .Select(a => (string)a.GetType().GetProperty("Id")!.GetValue(a)!).ToList();
    Console.WriteLine($"      catalog: {string.Join(", ", apps)}");
    Expect("Catalog lists the published apps (Timer among them)", apps.Contains("timer"), true);
}

// ---- Updater: the checksum gate, with a real release file and GitHub's digest ----
{
    var hubAsm = Assembly.Load("KnobHub");
    var updates = hubAsm.GetType("KnobHub.Core.HubUpdates")!;
    var updateType = hubAsm.GetType("KnobHub.Core.HubUpdate")!;
    const string Url = "https://github.com/Shyam-Sangeeth/KnobHub/releases/download/v1.0.2/knob-os-1.0.2-crowpanel-1.28.bin";
    const string Sha = "3a6a9eca01422f59bbe0e94367710ee3d387239a795d0265c03dc3ad50ff221d";
    long size = (await new HttpClient().GetByteArrayAsync(Url)).LongLength;
    string folder = Path.Combine(Path.GetTempPath(), "KnobHub", "update");
    async Task<string> Download(string? sha, string name)
    {
        var u = Activator.CreateInstance(updateType, [new Version(9, 9, 9), "https://example.invalid", Url, name, size, sha])!;
        try
        {
            string p = await (Task<string>)updates.GetMethod("DownloadAsync", Any)!.Invoke(null, [u, new Progress<double>(), CancellationToken.None])!;
            File.Delete(p);
            return "kept";
        }
        catch (InvalidDataException ex) { return (File.Exists(Path.Combine(folder, name)) ? "file left: " : "deleted: ") + ex.Message; }
    }
    Expect("Updater: right checksum is kept", await Download(Sha, "apptest-right.bin"), "kept");
    string wrong = await Download(new string('0', 64), "apptest-wrong.bin");
    Expect("Updater: wrong checksum is deleted", wrong.StartsWith("deleted: The downloaded installer didn't pass"), true);
    string none = await Download(null, "apptest-none.bin");
    Expect("Updater: no checksum is deleted, not run", none.StartsWith("deleted: GitHub gave no checksum"), true);
    Console.WriteLine($"      message: {none}");
}

Console.WriteLine(failed == 0 ? "all passed" : $"{failed} failed");
return failed;

sealed class FakeHost : IKnobHost
{
    public bool IsOpen { get; set; } = true;
    public List<string> Toasts = new(), Updates = new();
    public string LastShown = "";
    public void Show(string screen, KnobScreen content) => LastShown = screen;
    public void Update(string screen, object changes) =>
        Updates.Add(JsonSerializer.Serialize(changes, new JsonSerializerOptions { PropertyNamingPolicy = JsonNamingPolicy.SnakeCaseLower }));
    public void Close() { }
    public void Toast(string text) { lock (Toasts) Toasts.Add(text); }
    public void KeepAwake(bool on) { }
    public void Log(string message) => Console.WriteLine("      log: " + message);
    public int Setting(string id) => 1;  // 2% steps
    public int ScreenSize => 240;
}
