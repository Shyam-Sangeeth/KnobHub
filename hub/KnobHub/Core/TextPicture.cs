using System.Globalization;
using System.Windows;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using KnobHub.Sdk;

namespace KnobHub.Core;

/// <summary>
/// Draws text the knob's fonts can't (Malayalam, Hindi, Japanese, emoji...) with Windows' fonts, which have every
/// script and shape it properly, as an alpha-only <see cref="KnobImage"/> the knob shows in place of the label.
/// </summary>
internal static class TextPicture
{
    /// <summary>The widest picture the knob takes (knob-os/src/core/text_picture.cpp); longer text ends in "…".</summary>
    public const int MaxWidth = 1000;

    // Segoe UI with Windows' fonts for other scripts after it.
    private static readonly FontFamily Fonts =
        new("Segoe UI, Nirmala UI, Leelawadee UI, Yu Gothic UI, Microsoft YaHei UI, Malgun Gothic, Segoe UI Emoji, Segoe UI Symbol");

    /// <summary>One line, ending in "…" past <paramref name="maxWidth"/>. <paramref name="size"/> is the knob font's
    /// size in pixels (16 for a title, 12 for small text).</summary>
    public static KnobImage Draw(string text, double size, int maxWidth, bool semibold = false)
    {
        // WPF objects belong to the UI thread; apps may be on any thread.
        var dispatcher = Application.Current?.Dispatcher;
        if (dispatcher != null && !dispatcher.CheckAccess()) return dispatcher.Invoke(() => Draw(text, size, maxWidth, semibold));

        var typeface = new Typeface(Fonts, FontStyles.Normal, semibold ? FontWeights.SemiBold : FontWeights.Normal, FontStretches.Normal);
        var formatted = new FormattedText(text, CultureInfo.CurrentUICulture, FlowDirection.LeftToRight, typeface, size,
                                          Brushes.White, 1.0)
        {
            MaxTextWidth = maxWidth,
            MaxLineCount = 1,
            Trimming = TextTrimming.CharacterEllipsis,
        };
        int width = Math.Clamp((int)Math.Ceiling(formatted.WidthIncludingTrailingWhitespace), 1, maxWidth);
        int height = Math.Max(1, (int)Math.Ceiling(formatted.Height));

        var visual = new DrawingVisual();
        using (var dc = visual.RenderOpen()) dc.DrawText(formatted, new Point(0, 0));
        var bitmap = new RenderTargetBitmap(width, height, 96, 96, PixelFormats.Pbgra32);
        bitmap.Render(visual);

        var bgra = new byte[width * height * 4];
        bitmap.CopyPixels(bgra, width * 4, 0);
        var alpha = new byte[width * height];
        for (int i = 0; i < alpha.Length; i++) alpha[i] = bgra[i * 4 + 3];
        return KnobImage.FromAlpha(alpha, width, height);
    }
}
