#include "message.h"

#include <algorithm>

#include "theme.h"
#include "ui.h"

MessageScreen::MessageScreen(const std::string& icon, const std::string& title, const std::string& text,
                             const std::string& hint, std::function<void()> on_back)
    : Screen(ui::screen_root(screens::parent())), on_back_(std::move(on_back)) {
    // The icon, the heading, the text (as many lines as it takes) and the hint, a fixed gap apart and centred together.
    // Text is spaced by its letters: the first line's capitals to the last line's baseline.
    constexpr int IconSize = 28, Gap = 12, TextWidth = 170;
    const lv_font_t* heading_font = &knob_font_20;
    const lv_font_t* body_font = &knob_font_14;
    const lv_font_t* hint_font = &knob_font_12;
    lv_obj_t* r = root();
    lv_obj_t* glyph = ui::icon(r, icon, IconSize, theme::TextMuted);
    lv_obj_t* heading = ui::label(r, title.c_str(), heading_font, theme::Text);
    ui::one_line(heading, 190);
    lv_obj_t* body = ui::label(r, text.c_str(), body_font, theme::TextSecondary);
    lv_obj_set_width(body, TextWidth);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_t* tip = ui::label(r, hint.c_str(), hint_font, theme::TextMuted);
    lv_obj_update_layout(r);

    // A wrapped text's ink: from the first line's capitals to the last line's baseline.
    auto ink = [](lv_obj_t* label, const lv_font_t* font) {
        int lines = std::max<int>(1, lv_obj_get_height(label) / lv_font_get_line_height(font));
        return (lines - 1) * lv_font_get_line_height(font) + ui::ink_height(font);
    };
    const bool has_text = !text.empty(), has_hint = !hint.empty();
    int height = IconSize + Gap + ui::ink_height(heading_font);
    if (has_text) height += Gap + ink(body, body_font);
    if (has_hint) height += Gap + 4 + ui::ink_height(hint_font);  // the hint stands a little apart
    int y = (ui::Size - height) / 2;
    lv_obj_align(glyph, LV_ALIGN_TOP_MID, 0, y + (IconSize - lv_obj_get_height(glyph)) / 2);
    y += IconSize + Gap;
    lv_obj_align(heading, LV_ALIGN_TOP_MID, 0, y - ui::ink_top(heading_font));
    y += ui::ink_height(heading_font);
    if (has_text) {
        lv_obj_align(body, LV_ALIGN_TOP_MID, 0, y + Gap - ui::ink_top(body_font));
        y += Gap + ink(body, body_font);
    }
    if (has_hint) lv_obj_align(tip, LV_ALIGN_TOP_MID, 0, y + Gap + 4 - ui::ink_top(hint_font));
}

void MessageScreen::input(KnobInput in) {
    if (in != KnobInput::Back && in != KnobInput::Press) return;
    if (on_back_) on_back_();
    else screens::pop();
}
