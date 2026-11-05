using System.Reflection;

// Draws Knob Hub's built-in icons as the knob gets them: one line per request on stdin ("name size"), answered with
// the A8 bytes in base64 (or an empty line for an unknown icon).
var icons = typeof(KnobHub.App).Assembly.GetType("KnobHub.Core.KnobIcons")!;
var builtIn = (System.Collections.IDictionary)icons.GetField("BuiltIn", BindingFlags.Public | BindingFlags.Static)!.GetValue(null)!;
var draw = icons.GetMethod("Draw", BindingFlags.Public | BindingFlags.Static)!;
string? line;
while ((line = Console.ReadLine()) != null)
{
    var parts = line.Split(' ');
    object? icon = builtIn.Contains(parts[0]) ? builtIn[parts[0]] : null;
    Console.WriteLine(icon == null ? "" : Convert.ToBase64String((byte[])draw.Invoke(null, [icon, int.Parse(parts[1])])!));
}
