// Plays Knob Hub over Wi-Fi with Knob Hub's own SealedStream (loaded from KnobHub.dll by reflection): connects to the
// paired knob, does the handshake, sends the "hub" heartbeat and prints what the knob says for a few seconds. With
// --wrong-key it uses a random key, which the knob must refuse. Reads the pairing from Knob Hub's settings; close Knob
// Hub first (the knob takes one Wi-Fi link at a time). Build Knob Hub (Debug) first, then from the repository root:
//     dotnet run --project tools\wifi-link-test [-- --wrong-key]
using System.Net.Sockets;
using System.Reflection;
using System.Text;
using System.Text.Json.Nodes;

string hubDll = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..", "..", "hub", "KnobHub", "bin",
                                              "Debug", "net10.0-windows10.0.19041.0", "KnobHub.dll"));
var settings = JsonNode.Parse(File.ReadAllText(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "KnobHub", "settings.json")))!;
string host = settings["paired"]!["host"]!.GetValue<string>();
byte[] key = args.Contains("--wrong-key") ? System.Security.Cryptography.RandomNumberGenerator.GetBytes(32)
    : Convert.FromBase64String(settings["paired"]!["key"]!.GetValue<string>());
Console.WriteLine($"Connecting to {host}.local:5719");

var sealedType = Assembly.LoadFrom(hubDll).GetType("KnobHub.Core.SealedStream")!;
var open = sealedType.GetMethod("OpenAsync", BindingFlags.Public | BindingFlags.Static)!;
using var client = new TcpClient { NoDelay = true };
await client.ConnectAsync(host + ".local", 5719);
using var cancel = new CancellationTokenSource(TimeSpan.FromSeconds(5));
var task = (Task)open.Invoke(null, [client.GetStream(), key, cancel.Token])!;
await task;
var link = (Stream?)task.GetType().GetProperty("Result")!.GetValue(task);
if (link == null) { Console.WriteLine("Handshake refused"); return; }
Console.WriteLine("Handshake done");

var now = DateTimeOffset.Now;
string beat = new JsonObject { ["t"] = "hub", ["session"] = "test", ["time"] = now.ToUnixTimeSeconds(), ["offset"] = (int)now.Offset.TotalMinutes }.ToJsonString();
link.Write(Encoding.UTF8.GetBytes(beat + "\n"));
link.Flush();
var reader = new StreamReader(link);
using var listen = new CancellationTokenSource(TimeSpan.FromSeconds(6));
try
{
    while (await reader.ReadLineAsync(listen.Token) is { } line)
        Console.WriteLine("knob: " + (line.Length > 200 ? line[..200] + "..." : line));
}
catch (OperationCanceledException) { }
Console.WriteLine("Done");
