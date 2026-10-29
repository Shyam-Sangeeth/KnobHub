#include "ui.h"

#include <array>
#include <cstdlib>
#include <map>
#include <cstring>

#include "hal.h"
#include "icons.h"
#include "theme.h"

namespace ui {

namespace {

// This screen's fonts: the set made for its size, or the nearest one.
const fonts::Set& font_set() {
    static const fonts::Set* set = nullptr;
    if (!set) {
        int count = 0;
        const fonts::Set* sets = fonts::sets(count);
        set = &sets[0];
        for (int i = 1; i < count; i++) {
            if (std::abs(sets[i].diameter - size()) < std::abs(set->diameter - size())) set = &sets[i];
        }
    }
    return *set;
}

const lv_font_t* find(const fonts::Entry* entries, int count, int design) {
    for (int i = 0; i < count; i++) {
        if (entries[i].design >= design) return entries[i].font;
    }
    return entries[count - 1].font;
}

// The font for an icon of size pixels: the smallest from 12 (design) up that's at least as big.
const lv_font_t* font_for(int size) {
    const fonts::Set& set = font_set();
    for (int i = 0; i < set.text_count; i++) {
        if (set.text[i].design >= 12 && set.text[i].px >= size) return set.text[i].font;
    }
    return set.text[set.text_count - 1].font;
}

}  // namespace

int size() { return hal_board().size; }

int px(int design) {
    return (design * size() + (design >= 0 ? DesignSize / 2 : -DesignSize / 2)) / DesignSize;
}

int picture_px(int length, int drawn_for) {
    if (drawn_for <= 0 || drawn_for == size()) return length;
    int scaled = (length * size() + drawn_for / 2) / drawn_for;
    return scaled > 0 ? scaled : 1;
}

const lv_font_t* font(int design_size) { return find(font_set().text, font_set().text_count, design_size); }

const lv_font_t* digits(int design_size) { return find(font_set().digits, font_set().digit_count, design_size); }

lv_obj_t* plain(lv_obj_t* parent) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

lv_obj_t* screen_root(lv_obj_t* parent) {
    lv_obj_t* root = plain(parent);
    lv_obj_set_size(root, size(), size());
    lv_obj_set_style_bg_color(root, theme::color(theme::Screen), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    // Touches anywhere in it land here when nothing else takes them, and swipes on anything in it stop here (LVGL
    // passes them up to the parent by default): for the screen's own gestures.
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_GESTURE_BUBBLE);
    return root;
}

lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* font, uint32_t color) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, theme::color(color), 0);
    return l;
}

// LVGL only dots a label with a fixed size, so the text is measured first.
void one_line(lv_obj_t* label, int max_width) {
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_size(label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_update_layout(label);
    if (lv_obj_get_width(label) <= max_width) return;
    lv_obj_set_size(label, max_width, lv_font_get_line_height(lv_obj_get_style_text_font(label, 0)));
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
}

int ink_top(const lv_font_t* font, uint32_t letter) {
    lv_font_glyph_dsc_t g;
    if (!lv_font_get_glyph_dsc(font, &g, letter, 0)) return 0;
    return font->line_height - font->base_line - (g.ofs_y + g.box_h);
}

int ink_height(const lv_font_t* font, uint32_t letter) {
    lv_font_glyph_dsc_t g;
    return lv_font_get_glyph_dsc(font, &g, letter, 0) ? g.box_h : lv_font_get_line_height(font);
}

lv_obj_t* badge(lv_obj_t* parent, const std::string& icon_name, int center_x, int center_y, uint32_t color) {
    const int diameter = px(15);
    lv_obj_t* badge = circle(parent, diameter, theme::Screen, color, px(1));
    lv_obj_set_pos(badge, center_x - diameter / 2, center_y - diameter / 2);
    lv_obj_center(icon(badge, icon_name, px(11), color));
    return badge;
}

void cross_out(lv_obj_t* box, int size, uint32_t color, uint32_t background) {
    // A line keeps a pointer to its points, so each size's slash stays in this table.
    static std::map<int, std::array<lv_point_precise_t, 2>> slashes;
    auto& slash = slashes[size];
    slash = {lv_point_precise_t{-px(1), -px(1)}, lv_point_precise_t{size + px(1), size + px(1)}};
    for (int i = 0; i < 2; i++) {
        lv_obj_t* line = lv_line_create(box);
        lv_line_set_points(line, slash.data(), 2);
        lv_obj_set_style_line_width(line, px(i == 0 ? 5 : 2), 0);
        lv_obj_set_style_line_color(line, theme::color(i == 0 ? background : color), 0);
        lv_obj_set_style_line_rounded(line, true, 0);
    }
}

void set_icon_color(lv_obj_t* icon, uint32_t color) {
    if (lv_obj_check_type(icon, &lv_image_class)) lv_obj_set_style_image_recolor(icon, theme::color(color), 0);
    else lv_obj_set_style_text_color(icon, theme::color(color), 0);
}

lv_obj_t* icon(lv_obj_t* parent, const std::string& name, int size, uint32_t color) {
    if (const char* glyph = symbol(name)) return label(parent, glyph, font_for(size), color);
    return icons::create(parent, name, size, color);
}

lv_obj_t* circle(lv_obj_t* parent, int size, uint32_t fill, uint32_t border, int border_width) {
    lv_obj_t* c = plain(parent);
    lv_obj_set_size(c, size, size);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(c, theme::color(fill), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    if (border_width > 0) {
        lv_obj_set_style_border_width(c, border_width, 0);
        lv_obj_set_style_border_color(c, theme::color(border), 0);
    }
    return c;
}

lv_obj_t* arc(lv_obj_t* parent, int size, int width, int rotation, int bg_degrees, uint32_t track, uint32_t color) {
    lv_obj_t* a = lv_arc_create(parent);
    lv_obj_set_size(a, size, size);
    lv_arc_set_rotation(a, rotation);
    lv_arc_set_bg_angles(a, 0, bg_degrees);
    lv_arc_set_range(a, 0, 100);
    lv_arc_set_value(a, 0);  // until a value is set LVGL draws part of the indicator
    lv_obj_remove_style(a, nullptr, LV_PART_KNOB);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(a, 0, 0);
    lv_obj_set_style_arc_width(a, width, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, theme::color(track), LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, theme::color(color), LV_PART_INDICATOR);
    return a;
}

void marquee(lv_obj_t* label, int max_width) {
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_size(label, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_update_layout(label);
    int text_width = lv_obj_get_width(label);
    if (text_width <= max_width) return;  // it fits

    // Waits before the first round and between rounds.
    static lv_anim_t pause;
    static bool ready = false;
    if (!ready) {
        lv_anim_init(&pause);
        pause.act_time = -MarqueePauseMs;
        lv_anim_set_repeat_count(&pause, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_repeat_delay(&pause, MarqueePauseMs);
        ready = true;
    }
    const lv_font_t* font = lv_obj_get_style_text_font(label, 0);
    int round = text_width + 3 * lv_font_get_glyph_width(font, ' ', ' ');  // LVGL's gap: three spaces
    lv_obj_set_style_anim(label, &pause, 0);
    lv_obj_set_style_anim_duration(label, static_cast<uint32_t>(round * 1000 / px(MarqueeSpeed)), 0);
    lv_obj_set_size(label, max_width, lv_font_get_line_height(font));
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
}

void press_feedback(lv_obj_t* obj, int scale_percent) {
    static const lv_style_prop_t props[] = {LV_STYLE_TRANSFORM_SCALE_X, LV_STYLE_TRANSFORM_SCALE_Y,
                                            LV_STYLE_COLOR_FILTER_OPA, LV_STYLE_PROP_INV};
    static lv_style_transition_dsc_t press, release;
    static lv_color_filter_dsc_t darken;  // LVGL's own "shade" lightens below 50%
    static bool ready = false;
    if (!ready) {
        lv_color_filter_dsc_init(&darken, [](const lv_color_filter_dsc_t*, lv_color_t c, lv_opa_t opa) {
            return lv_color_darken(c, opa);
        });
        lv_style_transition_dsc_init(&press, props, lv_anim_path_ease_out, 70, 0, nullptr);
        lv_style_transition_dsc_init(&release, props, lv_anim_path_ease_out, 160, 0, nullptr);
        ready = true;
    }
    lv_obj_set_style_transform_pivot_x(obj, LV_PCT(50), 0);
    lv_obj_set_style_transform_pivot_y(obj, LV_PCT(50), 0);
    lv_obj_set_style_transition(obj, &release, 0);
    lv_obj_set_style_transition(obj, &press, LV_STATE_PRESSED);
    lv_obj_set_style_transform_scale(obj, 256 * scale_percent / 100, LV_STATE_PRESSED);
    lv_obj_set_style_color_filter_dsc(obj, &darken, LV_STATE_PRESSED);
    lv_obj_set_style_color_filter_opa(obj, LV_OPA_30, LV_STATE_PRESSED);
}

std::vector<uint8_t> base64_decode(const char* text) {
    std::vector<uint8_t> out;
    if (!text) return out;
    out.reserve(std::strlen(text) * 3 / 4);
    uint32_t bits = 0;
    int count = 0;
    for (const char* p = text; *p && *p != '='; p++) {
        char c = *p;
        int v = c >= 'A' && c <= 'Z' ? c - 'A'
              : c >= 'a' && c <= 'z' ? c - 'a' + 26
              : c >= '0' && c <= '9' ? c - '0' + 52
              : c == '+' ? 62 : c == '/' ? 63 : -1;
        if (v < 0) break;
        bits = (bits << 6) | static_cast<uint32_t>(v);
        if (++count == 4) {
            out.push_back(static_cast<uint8_t>(bits >> 16));
            out.push_back(static_cast<uint8_t>(bits >> 8));
            out.push_back(static_cast<uint8_t>(bits));
            bits = 0;
            count = 0;
        }
    }
    if (count == 3) {
        out.push_back(static_cast<uint8_t>(bits >> 10));
        out.push_back(static_cast<uint8_t>(bits >> 2));
    } else if (count == 2) {
        out.push_back(static_cast<uint8_t>(bits >> 4));
    }
    return out;
}

uint32_t parse_color(const char* hex, uint32_t fallback) {
    if (!hex || hex[0] != '#' || std::strlen(hex) != 7) return fallback;
    char* end = nullptr;
    unsigned long value = std::strtoul(hex + 1, &end, 16);
    return end && *end == '\0' ? static_cast<uint32_t>(value) : fallback;
}

uint32_t light(uint32_t color) {
    // About 37% of the colour over white.
    auto mix = [&](int shift) { return (((color >> shift) & 0xFF) * 37 + 255 * 63) / 100; };
    return (mix(16) << 16) | (mix(8) << 8) | mix(0);
}

uint32_t tint(uint32_t color) {
    // About 18% of the colour over the screen's near-black.
    uint32_t r = ((color >> 16) & 0xFF) * 18 / 100 + 6;
    uint32_t g = ((color >> 8) & 0xFF) * 18 / 100 + 6;
    uint32_t b = (color & 0xFF) * 18 / 100 + 8;
    return (r << 16) | (g << 8) | b;
}

const char* symbol(const std::string& icon) {
    if (icon == "settings") return LV_SYMBOL_SETTINGS;
    if (icon == "clock" || icon == "home") return LV_SYMBOL_HOME;
    if (icon == "wifi") return LV_SYMBOL_WIFI;
    if (icon == "usb") return LV_SYMBOL_USB;
    if (icon == "sun" || icon == "brightness") return LV_SYMBOL_EYE_OPEN;
    if (icon == "lights" || icon == "bulb") return LV_SYMBOL_CHARGE;
    if (icon == "info" || icon == "about") return LV_SYMBOL_LIST;
    if (icon == "trash") return LV_SYMBOL_TRASH;
    if (icon == "refresh" || icon == "restart") return LV_SYMBOL_REFRESH;
    if (icon == "check") return LV_SYMBOL_OK;
    if (icon == "haptics") return LV_SYMBOL_BELL;
    return nullptr;
}

}  // namespace ui
