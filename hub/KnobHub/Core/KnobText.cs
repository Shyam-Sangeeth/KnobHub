using System.Text;
using System.Text.Json.Nodes;

namespace KnobHub.Core;

/// <summary>
/// Fits text to the characters the knob's fonts have (knob-os/fonts/make_fonts.py: ASCII, Latin-1 Supplement, Latin
/// Extended-A and common punctuation). Anything else becomes its nearest drawable form where there is one: "ǎ" -> "a",
/// "ﬁ" -> "fi", full-width letters -> ASCII. Characters with none (other scripts, emoji) are left as they are.
/// </summary>
internal static class KnobText
{
    public static bool Drawable(char c) =>
        c is >= ' ' and <= '~' or >= '\u00A0' and <= '\u017F' or >= '\u2010' and <= '\u2027'
            or '\u2030' or '\u2039' or '\u203A' or '\u20AC' or '\u2122';

    /// <summary>Whether the knob can draw the text (after <see cref="Fit"/>).</summary>
    public static bool CanDraw(string text) => Fit(text).All(Drawable);

    public static string Fit(string text)
    {
        if (text.All(Drawable)) return text;
        var fitted = new StringBuilder(text.Length);
        foreach (char c in text)
        {
            if (Drawable(c))
            {
                fitted.Append(c);
                continue;
            }
            // The compatibility decomposition, without accents the font can't put on that letter.
            string plain = c.ToString().Normalize(NormalizationForm.FormKD);
            var parts = plain.Where(p => Drawable(p)).ToArray();
            if (parts.Length > 0 && plain.All(p => Drawable(p) || char.GetUnicodeCategory(p) == System.Globalization.UnicodeCategory.NonSpacingMark))
                fitted.Append(parts);
            else
                fitted.Append(c);
        }
        return fitted.ToString();
    }

    /// <summary>Fits every string in a message (in place).</summary>
    public static void FitAll(JsonNode? node)
    {
        switch (node)
        {
            case JsonObject obj:
                foreach (var (key, value) in obj.ToList())
                {
                    if (value is JsonValue v && v.TryGetValue(out string? s)) obj[key] = Fit(s);
                    else FitAll(value);
                }
                break;
            case JsonArray array:
                for (int i = 0; i < array.Count; i++)
                {
                    if (array[i] is JsonValue v && v.TryGetValue(out string? s)) array[i] = Fit(s);
                    else FitAll(array[i]);
                }
                break;
        }
    }
}
