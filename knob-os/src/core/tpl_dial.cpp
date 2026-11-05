// "dial": one big value on a 270-degree arc, e.g. a channel's volume or the screen brightness.
//   label, icon, color, value (0-100), min (the lowest value, default 0), unit ("%"), level (0-1, a live meter inside
//   the arc, optional), muted (bool), mutes (bool: pressing mutes, e.g. a volume), off_text (shown while muted or at 0;
//   default "Muted" or "Off"), button {id, label} (optional, tapped to change e.g. the device; an update with false hides
//   it), hint
// Turning changes the value right away on screen and reports rotate +1/-1. Pressing mutes or unmutes a dial that mutes,
// and sends any other between its lowest value and 100, right away; it reports press with the new state as the delta
// (1 muted, 0 not; or the new value).
#include <algorithm>
#include <cstring>

#include "templates.h"
#include "theme.h"
#include "ui.h"

namespace {

constexpr uint32_t ArcTrack = 0x22252B;
constexpr uint32_t ButtonText = 0xD5D8DE;
constexpr int Step = 2;  // percent per click of the knob, shown right away; the app confirms with an update
constexpr int IconSize = 16;  // design px
constexpr int Gap = 10;  // between the icon, the label, the value and the button (design px)
constexpr int WordWidth = 176;  // the widest the value can be inside the arc (design px)

class DialScreen : public TemplateScreen {
public:
    DialScreen(JsonObjectConst data, TemplateSink sink) : TemplateScreen(ui::screen_root(screens::parent()), std::move(sink)) {
        lv_obj_t* r = root();
        arc_ = ui::arc(r, ui::px(214), ui::px(9), 135, 270, ArcTrack, color_);
        lv_obj_center(arc_);
        level_ = ui::arc(r, ui::px(190), ui::px(3), 135, 270, ArcTrack, color_);
        lv_obj_center(level_);
        lv_obj_set_style_arc_opa(level_, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(level_, LV_OPA_40, LV_PART_INDICATOR);

        name_ = ui::label(r, "", ui::font(12), theme::TextSecondary);
        lv_obj_set_style_text_letter_space(name_, ui::px(1), 0);
        value_ = ui::label(r, "", ui::font(48), theme::Text);

        button_ = ui::plain(r);
        lv_obj_add_flag(button_, LV_OBJ_FLAG_CLICKABLE);
        ui::press_feedback(button_);
        lv_obj_set_size(button_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(button_, theme::color(theme::Surface), 0);
        lv_obj_set_style_bg_opa(button_, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(button_, ui::px(6), 0);
        lv_obj_set_style_pad_hor(button_, ui::px(8), 0);
        lv_obj_set_style_pad_ver(button_, ui::px(5), 0);
        lv_obj_set_flex_flow(button_, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(button_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(button_, ui::px(5), 0);
        button_label_ = ui::label(button_, "", ui::font(12), ButtonText);
        ui::label(button_, LV_SYMBOL_DOWN, ui::font(12), theme::TextMuted);
        lv_obj_add_event_cb(button_, on_button, LV_EVENT_CLICKED, this);
        lv_obj_add_flag(button_, LV_OBJ_FLAG_HIDDEN);  // until the app gives it a label

        // Just below the arc ends, where the screen is still about 146 px wide.
        hint_ = ui::label(r, "", ui::font(12), theme::TextMuted);
        lv_obj_align(hint_, LV_ALIGN_TOP_MID, 0, ui::px(201));

        update(data);
    }

    void input(KnobInput in) override {
        switch (in) {
            case KnobInput::TurnRight:
            case KnobInput::TurnLeft: {
                int delta = in == KnobInput::TurnRight ? 1 : -1;
                value_now_ = std::min(100, std::max(min_, value_now_ + delta * Step));
                muted_ = false;  // like turning a volume knob up from mute
                show_value();
                emit("rotate", "", delta);
                break;
            }
            case KnobInput::Press:
                if (mutes_) {
                    muted_ = !muted_;
                } else {
                    value_now_ = value_now_ > min_ ? min_ : 100;
                    muted_ = false;  // an app's "muted" on a dial that doesn't mute: the press shows the new value
                }
                show_value();
                emit("press", "", mutes_ ? muted_ : value_now_);
                break;
            case KnobInput::Back: emit("back"); break;
        }
    }

    // The value as the level meter (none while muted), along the dial's arc.
    lights::Glow glow() const override { return {color_, muted_ ? 0 : value_now_, 135, 270}; }

    // A fine click per step, with end stops at the lowest value and 100.
    haptics::Feel feel() const override {
        haptics::Feel f;
        f.positions = (100 - min_) / Step + 1;
        f.position = (value_now_ - min_) / Step;
        f.detent = haptics::Detent::Fine;
        return f;
    }

    void update(JsonObjectConst data) override {
        bool icon_changed = false;
        if (data["color"].is<const char*>()) {
            color_ = ui::parse_color(data["color"], color_);
            icon_changed = true;
            lv_obj_set_style_arc_color(level_, theme::color(color_), LV_PART_INDICATOR);
        }
        if (data["icon"].is<const char*>()) {
            icon_name_ = data["icon"].as<const char*>();
            icon_changed = true;
        }
        if (icon_changed) {
            // A picture or a font symbol, so it's made again rather than changed.
            if (icon_) lv_obj_delete(icon_);
            icon_ = ui::icon(root(), icon_name_, ui::px(IconSize), color_);
        }
        if (data["label"].is<const char*>()) lv_label_set_text(name_, data["label"]);
        if (data["unit"].is<const char*>()) unit_ = data["unit"].as<const char*>();
        if (data["min"].is<int>()) min_ = std::min(100, std::max(0, data["min"].as<int>()));
        if (data["value"].is<int>()) value_now_ = data["value"];
        value_now_ = std::min(100, std::max(min_, value_now_));
        if (data["muted"].is<bool>()) muted_ = data["muted"];
        if (data["mutes"].is<bool>()) mutes_ = data["mutes"];
        if (data["off_text"].is<const char*>()) off_text_ = data["off_text"].as<const char*>();
        if (data["level"].is<float>()) lv_arc_set_value(level_, static_cast<int>(data["level"].as<float>() * 100));
        if (data["hint"].is<const char*>()) {
            ui::marquee_text(hint_, data["hint"], ui::px(146));
        }
        if (data["button"].is<JsonObjectConst>()) {
            button_id_ = data["button"]["id"] | "";
            ui::marquee_text(button_label_, data["button"]["label"] | "", ui::px(120));  // the button stays inside the arc
            lv_obj_remove_flag(button_, LV_OBJ_FLAG_HIDDEN);
        } else if (data["button"].is<bool>()) {  // "button": false hides it
            lv_obj_add_flag(button_, LV_OBJ_FLAG_HIDDEN);
        }
        show_value();
        lay_out();
    }

private:
    static void on_button(lv_event_t* e) {
        auto* self = static_cast<DialScreen*>(lv_event_get_user_data(e));
        self->emit("tap", self->button_id_);
    }

    // The icon, the label, the value and the button (when shown), a fixed gap apart (by what's drawn: a label's letters
    // sit inside its line) and centred on the screen together; the hint stays below the arc's ends.
    void lay_out() {
        const lv_font_t* small = ui::font(12);
        const lv_font_t* big = ui::font(48);
        const int icon_size = ui::px(IconSize), gap = ui::px(Gap);
        bool button = !lv_obj_has_flag(button_, LV_OBJ_FLAG_HIDDEN);
        int button_height = 0;
        if (button) {
            lv_obj_update_layout(button_);
            button_height = lv_obj_get_height(button_);
        }
        int height = (icon_ ? icon_size + gap : 0) + ui::ink_height(small) + gap + ui::ink_height(big, '0') +
                     (button ? gap + button_height : 0);
        int y = (ui::size() - height) / 2;
        if (icon_) {
            lv_obj_update_layout(icon_);
            lv_obj_align(icon_, LV_ALIGN_TOP_MID, 0, y + (icon_size - lv_obj_get_height(icon_)) / 2);
            y += icon_size + gap;
        }
        lv_obj_align(name_, LV_ALIGN_TOP_MID, 0, y - ui::ink_top(small));
        y += ui::ink_height(small) + gap;
        // A word in the smaller font sits in the middle of the big value's place, so nothing else moves.
        const lv_font_t* value_font = lv_obj_get_style_text_font(value_, 0);
        lv_obj_align(value_, LV_ALIGN_TOP_MID, 0,
                     y + (ui::ink_height(big, '0') - ui::ink_height(value_font, '0')) / 2 - ui::ink_top(value_font, '0'));
        y += ui::ink_height(big, '0') + gap;
        if (button) lv_obj_align(button_, LV_ALIGN_TOP_MID, 0, y);
    }

    // The value, or while muted or at 0 a word: in the big font when it fits inside the arc (the big font has only
    // ASCII), otherwise smaller.
    void show_value() {
        lv_arc_set_value(arc_, value_now_);
        lv_obj_set_style_arc_color(arc_, theme::color(muted_ ? theme::TextMuted : color_), LV_PART_INDICATOR);
        const lv_font_t* font = ui::font(48);
        const int max_width = ui::px(WordWidth);
        if (muted_ || value_now_ == 0) {
            const char* word = !off_text_.empty() ? off_text_.c_str() : muted_ ? "Muted" : "Off";
            lv_label_set_text(value_, word);
            bool ascii = std::all_of(word, word + std::strlen(word), [](char c) { return (c & 0x80) == 0; });
            if (!ascii || lv_text_get_width(word, std::strlen(word), font, 0) > max_width) font = ui::font(28);
        } else {
            lv_label_set_text_fmt(value_, "%d%s", value_now_, unit_.c_str());
        }
        bool new_font = font != lv_obj_get_style_text_font(value_, 0);
        lv_obj_set_style_text_font(value_, font, 0);
        ui::one_line(value_, max_width);
        if (new_font) lay_out();
    }

    lv_obj_t* arc_;
    lv_obj_t* level_;
    lv_obj_t* icon_ = nullptr;
    std::string icon_name_;
    lv_obj_t* name_;
    lv_obj_t* value_;
    lv_obj_t* button_;
    lv_obj_t* button_label_;
    lv_obj_t* hint_;
    uint32_t color_ = theme::Accent;
    std::string unit_ = "%", off_text_;
    std::string button_id_;
    int value_now_ = 0, min_ = 0;
    bool muted_ = false, mutes_ = false;
};

}  // namespace

TemplateScreen* make_dial(JsonObjectConst data, TemplateSink sink) { return new DialScreen(data, std::move(sink)); }
