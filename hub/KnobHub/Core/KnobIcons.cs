using System.Globalization;
using System.IO;
using System.Text;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using KnobHub.Sdk;

namespace KnobHub.Core;

/// <summary>
/// Icons for the knob (see knob-os/src/core/icons.h): the knob asks for an icon by name at the size it shows it, and
/// Knob Hub draws it, from an app's own icons or its built-in ones, as alpha-only pixels the knob colours.
/// </summary>
internal static class KnobIcons
{
    /// <summary>Common icons any app can use by name (Segoe Fluent Icons). Also used on the Apps page.</summary>
    public static readonly IReadOnlyDictionary<string, KnobIcon> BuiltIn = new Dictionary<string, KnobIcon>
    {
        ["music"] = KnobIcon.Glyph("\uEC4F"),
        ["media"] = KnobIcon.Glyph("\uEC4F"),
        ["play"] = KnobIcon.Glyph("\uE768"),
        ["pause"] = KnobIcon.Glyph("\uE769"),
        ["previous"] = KnobIcon.Glyph("\uE892"),
        ["next"] = KnobIcon.Glyph("\uE893"),
        ["game"] = KnobIcon.Glyph("\uE7FC"),
        ["headphones"] = KnobIcon.Glyph("\uE7F6"),
        ["speaker"] = KnobIcon.Glyph("\uE7F5"),
        ["volume"] = KnobIcon.Glyph("\uE767"),
        ["mute"] = KnobIcon.Glyph("\uE74F"),
        ["mic"] = KnobIcon.Glyph("\uE720"),
        ["equalizer"] = KnobIcon.Glyph("\uE9E9"),
        ["tv"] = KnobIcon.Glyph("\uE7F4"),
        ["monitor"] = KnobIcon.Glyph("\uE7F4"),
        ["video"] = KnobIcon.Glyph("\uE714"),
        ["camera"] = KnobIcon.Glyph("\uE722"),
        ["phone"] = KnobIcon.Glyph("\uE717"),
        ["mail"] = KnobIcon.Glyph("\uE715"),
        ["calendar"] = KnobIcon.Glyph("\uE787"),
        ["clock"] = KnobIcon.Glyph("\uE823"),
        ["timer"] = KnobIcon.Glyph("\uE916"),
        ["home"] = KnobIcon.Glyph("\uE80F"),
        ["settings"] = KnobIcon.Glyph("\uE713"),
        ["lights"] = KnobIcon.Glyph("\uE945"),
        ["sun"] = KnobIcon.Glyph("\uE706"),
        ["power"] = KnobIcon.Glyph("\uE7E8"),
        ["wifi"] = KnobIcon.Glyph("\uE701"),
        ["bluetooth"] = KnobIcon.Glyph("\uE702"),
        ["usb"] = KnobIcon.Glyph("\uE88E"),
        ["globe"] = KnobIcon.Glyph("\uE774"),
        ["folder"] = KnobIcon.Glyph("\uE8B7"),
        ["search"] = KnobIcon.Glyph("\uE721"),
        ["star"] = KnobIcon.Glyph("\uE734"),
        ["heart"] = KnobIcon.Glyph("\uEB51"),
        ["lock"] = KnobIcon.Glyph("\uE72E"),
        ["info"] = KnobIcon.Glyph("\uE946"),
        ["about"] = KnobIcon.Glyph("\uE946"),
        ["add"] = KnobIcon.Glyph("\uE710"),
        ["check"] = KnobIcon.Glyph("\uE73E"),
        ["trash"] = KnobIcon.Glyph("\uE74D"),
        ["restart"] = KnobIcon.Glyph("\uE72C"),
        ["refresh"] = KnobIcon.Glyph("\uE72C"),
    };

    // Changes when Draw changes how icons come out, so knobs fetch their kept icons again.
    private const uint DrawVersion = 1;

    /// <summary>A fingerprint of how the icon is drawn: the knob keeps icons with it and fetches one again when it
    /// changes (e.g. an app update brings a new picture). Never 0.</summary>
    public static uint Hash(KnobIcon icon)
    {
        var (kind, data) = icon switch
        {
            GlyphIcon g => ("glyph", Encoding.UTF8.GetBytes(g.Font + "|" + g.Text)),
            PathIcon p => ("path", Encoding.UTF8.GetBytes(p.Data)),
            ImageIcon i => ("image", i.Data),
            _ => ("", Array.Empty<byte>()),
        };
        uint hash = 2166136261 ^ DrawVersion;  // FNV-1a
        foreach (byte b in Encoding.ASCII.GetBytes(kind).Concat(data))
        {
            hash ^= b;
            hash *= 16777619;
        }
        return hash == 0 ? 1 : hash;
    }

    /// <summary>The icon's shape, size x size, as alpha bytes row by row: fitted to the square (keeping its
    /// proportions) and centred.</summary>
    public static byte[] Draw(KnobIcon icon, int size)
    {
        var visual = new DrawingVisual();
        RenderOptions.SetBitmapScalingMode(visual, BitmapScalingMode.HighQuality);
        using (var dc = visual.RenderOpen())
        {
            switch (icon)
            {
                case GlyphIcon glyph:
                    var text = new FormattedText(glyph.Text, CultureInfo.InvariantCulture, FlowDirection.LeftToRight,
                                                 new Typeface(glyph.Font), 100, Brushes.White, 1.0);
                    DrawFitted(dc, text.BuildGeometry(new Point(0, 0)), size);
                    break;
                case PathIcon path:
                    DrawFitted(dc, Geometry.Parse(path.Data), size);
                    break;
                case ImageIcon image:
                    var bitmap = new BitmapImage();
                    bitmap.BeginInit();
                    bitmap.CacheOption = BitmapCacheOption.OnLoad;
                    bitmap.StreamSource = new MemoryStream(image.Data);
                    bitmap.EndInit();
                    double scale = (double)size / Math.Max(bitmap.PixelWidth, bitmap.PixelHeight);
                    double w = bitmap.PixelWidth * scale, h = bitmap.PixelHeight * scale;
                    dc.DrawImage(bitmap, new Rect((size - w) / 2, (size - h) / 2, w, h));
                    break;
            }
        }
        var target = new RenderTargetBitmap(size, size, 96, 96, PixelFormats.Pbgra32);
        target.Render(visual);
        var pixels = new byte[size * size * 4];
        target.CopyPixels(pixels, size * 4, 0);
        var alpha = new byte[size * size];
        for (int i = 0; i < alpha.Length; i++) alpha[i] = pixels[i * 4 + 3];
        return alpha;
    }

    private static void DrawFitted(DrawingContext dc, Geometry shape, int size)
    {
        var bounds = shape.Bounds;
        if (bounds.IsEmpty || bounds.Width <= 0 || bounds.Height <= 0) return;
        double scale = size / Math.Max(bounds.Width, bounds.Height);
        var place = new Matrix();
        place.Translate(-bounds.X - bounds.Width / 2, -bounds.Y - bounds.Height / 2);
        place.Scale(scale, scale);
        place.Translate(size / 2.0, size / 2.0);
        dc.PushTransform(new MatrixTransform(place));
        dc.DrawGeometry(Brushes.White, null, shape);
        dc.Pop();
    }
}
