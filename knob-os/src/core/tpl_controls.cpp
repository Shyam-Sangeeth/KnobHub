// "controls": a value with a toggle and a picker under it, e.g. an audio channel: its volume, a feature on/off and the
// output device.
//   label, icon, color, value (0-100, the ring and the number at the bottom), step (per click, default 2), unit (after
//   the number, default "%"), muted (the icon is crossed out and muted_text shows instead of the number), muted_text
//   (default "Muted"),
//   toggle {id, label, icon, on, on_color / off_color (the icon's, when on / off; default the colour / grey), hidden}
//   (optional: a pill),
//   picker {selected (option id), options [{id, name, detail}], empty_text (default "None")} (optional: a card)
// The knob's press moves between the controls: the value, the toggle, the picker. Turning changes the selected one:
//   value: step a click, shown right away (reports rotate +1/-1);
//   toggle: on / off (reports tap, item = the toggle's id, delta 1 on, 0 off);
//   picker: steps through the options on the card and picks once the knob rests (reports select, item = the option's
//   id), so turning past options doesn't pick each one.
// Tapping the icon or the number, or pressing the knob twice quickly, reports press (e.g. mute); tapping the toggle or
// the card selects it (tapping it again changes it). Long press: back.
#include <algorithm>
#include <string>
#include <vector>

#include "templates.h"
#include "theme.h"
#include "ui.h"

namespace {

constexpr uint32_t ArcTrack = 0x22252B;
constexpr uint32_t DetailText = 0xB8BDC6;  // lighter than secondary text: it sits on the card
constexpr uint32_t Unset = 0xFFFFFFFF;     // a colour the app didn't send
constexpr uint32_t PickerRestMs = 600;     // the picker picks once the knob has rested this long
constexpr uint32_t DoublePressMs = 350;    // a second press within this reports press
constexpr int IconSize = 16, IconBox = IconSize + 12, CardWidth = 156, CardHeight = 46, TextWidth = 134;  // design px
constexpr int ToggleHeight = 24;  // design px
constexpr int RowGap = 12;      // between the icon, the name, the toggle, the card and the number (design px)
constexpr int ContentTop = 26;  // just inside the ring (design px)
constexpr int ValueTop = 173;   // the number sits in the gap at the bottom of the ring (design px)

struct Option {
    std::string id, name, detail;
};

enum Control { Value, Toggle, Picker };

class ControlsScreen : public TemplateScreen {
public:
    ControlsScreen(JsonObjectConst data, TemplateSink sink) : TemplateScreen(ui::screen_root(screens::parent()), std::move(sink)) {
        lv_obj_t* r = root();
        arc_ = ui::arc(r, ui::px(214), ui::px(9), 135, 270, ArcTrack, color_);
        lv_obj_center(arc_);

        // The icon (tap: press) and the name.
        icon_box_ = ui::plain(r);
        lv_obj_set_size(icon_box_, ui::px(IconBox), ui::px(IconBox));  // a bigger target than the icon
        lv_obj_add_flag(icon_box_, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(icon_box_, on_icon, LV_EVENT_CLICKED, this);
        ui::press_feedback(icon_box_, 88);
        name_ = ui::label(r, "", ui::font(12), theme::TextSecondary);
        lv_obj_set_style_text_letter_space(name_, ui::px(1), 0);

        // The toggle: a pill with an optional icon.
        toggle_ = ui::plain(r);
        lv_obj_set_size(toggle_, LV_SIZE_CONTENT, ui::px(ToggleHeight));
        lv_obj_set_style_radius(toggle_, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(toggle_, ui::px(1), 0);
        lv_obj_set_style_bg_opa(toggle_, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_hor(toggle_, ui::px(11), 0);
        lv_obj_set_style_pad_column(toggle_, ui::px(5), 0);
        lv_obj_set_flex_flow(toggle_, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(toggle_, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        toggle_text_ = ui::label(toggle_, "", ui::font(12), theme::TextSecondary);
        touchable(toggle_, Toggle);

        // The picker: the option's name and detail on a card, both scrolling when long.
        card_ = ui::plain(r);
        lv_obj_set_size(card_, ui::px(CardWidth), ui::px(CardHeight));
        lv_obj_set_style_radius(card_, ui::px(12), 0);
        lv_obj_set_style_bg_color(card_, theme::color(theme::Surface), 0);
        lv_obj_set_style_bg_opa(card_, LV_OPA_COVER, 0);
        touchable(card_, Picker);
        option_name_ = ui::label(card_, "", ui::font(14), theme::Text);
        option_detail_ = ui::label(card_, "", ui::font(12), DetailText);
        for (int side = 0; side < 2; side++) {  // turning picks: shown while the card is selected
            arrows_[side] = ui::label(card_, side == 0 ? LV_SYMBOL_LEFT : LV_SYMBOL_RIGHT, ui::font(12), theme::TextMuted);
            lv_obj_align(arrows_[side], side == 0 ? LV_ALIGN_LEFT_MID : LV_ALIGN_RIGHT_MID, side == 0 ? ui::px(4) : -ui::px(4), 0);
        }

        // The number (or the muted text) in the space at the bottom of the ring.
        value_box_ = ui::plain(r);
        lv_obj_set_size(value_box_, ui::px(96), ui::px(34));
        lv_obj_align(value_box_, LV_ALIGN_TOP_MID, 0, ui::px(ValueTop));
        lv_obj_set_style_radius(value_box_, ui::px(10), 0);
        touchable(value_box_, Value);
        value_ = ui::label(value_box_, "", ui::font(28), theme::Text);
        lv_obj_center(value_);

        update(data);
    }

    ~ControlsScreen() override {
        if (rest_timer_) lv_timer_delete(rest_timer_);
    }

    void input(KnobInput in) override {
        switch (in) {
            case KnobInput::TurnRight: turn(1); break;
            case KnobInput::TurnLeft: turn(-1); break;
            case KnobInput::Press: press(); break;
            case KnobInput::Back: emit("back"); break;
        }
    }

    // The value as the level meter (none while muted), along the value's arc, whichever control is in focus.
    lights::Glow glow() const override { return {color_, muted_ ? 0 : value_now_, 135, 270}; }

    // The focused control's feel: fine steps for the value, firm clicks for the toggle and the picker.
    haptics::Feel feel() const override {
        if (!available(focus_)) return {};
        haptics::Feel f;
        if (focus_ == Value) {
            f.positions = 100 / step_ + 1;
            f.position = value_now_ / step_;
            f.detent = haptics::Detent::Fine;
        } else if (focus_ == Toggle) {
            f.positions = 2;
            f.position = toggle_on_ ? 1 : 0;
            f.wrap = true;
            f.detent = haptics::Detent::Firm;
        } else if (focus_ == Picker) {
            if (options_.empty()) return {};
            f.positions = static_cast<int>(options_.size());
            f.position = std::max(0, shown_);
            f.wrap = true;
            f.detent = haptics::Detent::Firm;
        }
        return f;
    }

    void update(JsonObjectConst data) override {
        bool redraw_icon = false;
        if (data["color"].is<const char*>()) {
            color_ = ui::parse_color(data["color"], color_);
            redraw_icon = true;
        }
        if (data["icon"].is<const char*>()) {
            icon_name_ = data["icon"].as<const char*>();
            redraw_icon = true;
        }
        if (data["label"].is<const char*>()) lv_label_set_text(name_, data["label"]);
        if (data["value"].is<int>()) value_now_ = data["value"];
        if (data["step"].is<int>()) step_ = std::max(1, data["step"].as<int>());
        if (data["unit"].is<const char*>()) unit_ = data["unit"].as<const char*>();
        if (data["muted_text"].is<const char*>()) muted_text_ = data["muted_text"].as<const char*>();
        if (data["muted"].is<bool>() && data["muted"].as<bool>() != muted_) {
            muted_ = data["muted"];
            redraw_icon = true;
        }
        if (data["toggle"].is<JsonObjectConst>()) read_toggle(data["toggle"]);
        // While the knob is picking, the card shows that option; the app catches up once it's picked.
        if (data["picker"].is<JsonObjectConst>()) {
            JsonObjectConst picker = data["picker"];
            has_picker_ = true;
            if (picker["empty_text"].is<const char*>()) empty_text_ = picker["empty_text"].as<const char*>();
            if (picker["options"].is<JsonArrayConst>()) {
                options_.clear();
                for (JsonObjectConst o : picker["options"].as<JsonArrayConst>())
                    options_.push_back({o["id"] | "", o["name"] | "", o["detail"] | ""});
                if (options_.empty()) shown_ = -1;
                else if (shown_ >= static_cast<int>(options_.size())) shown_ = static_cast<int>(options_.size()) - 1;
            }
            if (picker["selected"].is<const char*>() && !rest_timer_) shown_ = index_of(picker["selected"].as<const char*>());
        }
        if (redraw_icon) draw_icon();
        if (!available(focus_)) focus_ = Value;
        show();
    }

private:
    void read_toggle(JsonObjectConst t) {
        has_toggle_ = !(t["hidden"] | false);
        if (t["id"].is<const char*>()) toggle_id_ = t["id"].as<const char*>();
        if (t["label"].is<const char*>()) lv_label_set_text(toggle_text_, t["label"]);
        if (t["on"].is<bool>()) toggle_on_ = t["on"];
        if (t["on_color"].is<const char*>()) toggle_on_color_ = ui::parse_color(t["on_color"], Unset);
        if (t["off_color"].is<const char*>()) toggle_off_color_ = ui::parse_color(t["off_color"], Unset);
        if (t["icon"].is<const char*>() && toggle_icon_name_ != t["icon"].as<const char*>()) {
            toggle_icon_name_ = t["icon"].as<const char*>();
            if (toggle_icon_) lv_obj_delete(toggle_icon_);
            toggle_icon_ = nullptr;
            if (!toggle_icon_name_.empty()) {
                toggle_icon_ = ui::icon(toggle_, toggle_icon_name_, ui::px(12), theme::TextMuted);
                lv_obj_move_to_index(toggle_icon_, 0);  // before the label
            }
        }
    }

    // --- Controls ---

    void touchable(lv_obj_t* obj, Control control) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(obj, reinterpret_cast<void*>(static_cast<intptr_t>(control)));
        lv_obj_add_event_cb(obj, on_control, LV_EVENT_CLICKED, this);
        ui::press_feedback(obj, 96);
    }

    static void on_control(lv_event_t* e) {
        auto* self = static_cast<ControlsScreen*>(lv_event_get_user_data(e));
        auto control = static_cast<Control>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(lv_event_get_current_target_obj(e))));
        if (control == Value) return self->toggle_muted();
        if (control == Toggle && self->focus_ == Toggle) self->flip_toggle();
        else if (control == Picker && self->focus_ == Picker) self->step_picker(1);
        self->select(control);
    }

    static void on_icon(lv_event_t* e) { static_cast<ControlsScreen*>(lv_event_get_user_data(e))->toggle_muted(); }

    void toggle_muted() {
        muted_ = !muted_;  // right away; the app confirms
        draw_icon();
        show();
        emit("press");
    }

    // A press moves to the next control right away; a second one soon after goes back to where it was and reports
    // press.
    void press() {
        uint32_t now = lv_tick_get();
        if (pressed_at_ && lv_tick_elaps(pressed_at_) < DoublePressMs) {
            pressed_at_ = 0;
            focus_ = focus_before_;
            toggle_muted();
            return;
        }
        pressed_at_ = now ? now : 1;
        focus_before_ = focus_;
        select(next_control());
    }

    bool available(Control c) const { return c == Value || (c == Toggle && has_toggle_) || (c == Picker && has_picker_); }

    Control next_control() const {
        Control c = focus_;
        do c = static_cast<Control>((c + 1) % 3);
        while (!available(c));
        return c;
    }

    void select(Control control) {
        focus_ = control;
        show();
    }

    void turn(int delta) {
        if (focus_ == Value) {
            int value = std::min(100, std::max(0, value_now_ + delta * step_));
            if (value == value_now_ && !muted_) return;  // already at 100 or 0: nothing to change
            value_now_ = value;
            muted_ = false;  // like turning a volume knob up from mute
            draw_icon();
            show();
            emit("rotate", "", delta);
        } else if (focus_ == Toggle) {
            flip_toggle();
        } else {
            step_picker(delta);
        }
    }

    void flip_toggle() {
        if (!has_toggle_) return;
        toggle_on_ = !toggle_on_;
        show();
        emit("tap", toggle_id_, toggle_on_ ? 1 : 0);
    }

    void step_picker(int delta) {
        if (options_.empty()) return;
        int n = static_cast<int>(options_.size());
        shown_ = ((shown_ < 0 ? 0 : shown_) + delta + n) % n;
        show();
        if (rest_timer_) {
            lv_timer_reset(rest_timer_);
        } else {
            rest_timer_ = lv_timer_create(on_rest, PickerRestMs, this);
            lv_timer_set_repeat_count(rest_timer_, 1);
        }
    }

    static void on_rest(lv_timer_t* t) {
        auto* self = static_cast<ControlsScreen*>(lv_timer_get_user_data(t));
        self->rest_timer_ = nullptr;  // a one-shot timer deletes itself
        if (self->shown_ >= 0 && self->shown_ < static_cast<int>(self->options_.size()))
            self->emit("select", self->options_[self->shown_].id);
    }

    int index_of(const std::string& id) const {
        for (size_t i = 0; i < options_.size(); i++)
            if (options_[i].id == id) return static_cast<int>(i);
        return -1;
    }

    // --- Drawing ---

    void draw_icon() {
        const int icon_size = ui::px(IconSize);
        lv_obj_clean(icon_box_);
        uint32_t color = muted_ ? theme::TextMuted : color_;
        lv_obj_t* icon = ui::icon(icon_box_, icon_name_, icon_size, color);
        lv_obj_center(icon);
        if (muted_) {
            lv_obj_t* slash = ui::plain(icon_box_);  // the slash's box sits over the icon
            lv_obj_set_size(slash, icon_size, icon_size);
            lv_obj_center(slash);
            lv_obj_add_flag(slash, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
            ui::cross_out(slash, icon_size, color, theme::Screen);
        }
    }

    static void set_hidden(lv_obj_t* obj, bool hidden) {
        if (hidden) lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }

    void show() {
        lv_arc_set_value(arc_, value_now_);
        lv_obj_set_style_arc_color(arc_, theme::color(muted_ ? theme::TextMuted : color_), LV_PART_INDICATOR);

        if (muted_) {
            lv_obj_set_style_text_font(value_, ui::font(20), 0);
            lv_label_set_text(value_, muted_text_.c_str());
        } else {
            lv_obj_set_style_text_font(value_, ui::font(28), 0);
            lv_label_set_text_fmt(value_, "%d%s", value_now_, unit_.c_str());
        }

        set_hidden(toggle_, !has_toggle_);
        set_hidden(card_, !has_picker_);
        lay_out();
        lv_obj_set_style_bg_color(toggle_, theme::color(toggle_on_ ? ui::tint(color_) : theme::Surface), 0);
        lv_obj_set_style_border_color(toggle_, theme::color(toggle_on_ ? color_ : theme::Border), 0);
        lv_obj_set_style_text_color(toggle_text_, theme::color(toggle_on_ ? ui::light(color_) : theme::TextSecondary), 0);
        if (toggle_icon_) {
            uint32_t icon = toggle_on_ ? (toggle_on_color_ != Unset ? toggle_on_color_ : color_)
                                       : (toggle_off_color_ != Unset ? toggle_off_color_ : theme::TextMuted);
            ui::set_icon_color(toggle_icon_, icon);
        }

        const Option* o = shown_ >= 0 && shown_ < static_cast<int>(options_.size()) ? &options_[shown_] : nullptr;
        set_text(option_name_, o ? o->name.c_str() : empty_text_.c_str(), ui::px(6));
        set_text(option_detail_, o ? o->detail.c_str() : "", -ui::px(6));
        for (lv_obj_t* arrow : arrows_) set_hidden(arrow, !(focus_ == Picker && options_.size() > 1));

        // The selected control's outline; the value needs none (the ring and the number are the value).
        lv_obj_t* controls[] = {value_box_, toggle_, card_};
        for (int i = 0; i < 3; i++) {
            bool on = i == focus_ && i != Value;
            lv_obj_set_style_outline_width(controls[i], on ? ui::px(2) : 0, 0);
            lv_obj_set_style_outline_color(controls[i], theme::color(theme::Text), 0);
            lv_obj_set_style_outline_pad(controls[i], 0, 0);  // close round it
        }
    }

    // The icon, the name, the toggle and the card (those there are), the same gap apart, centred between the ring and
    // the number's digits (so the gap down to them comes out the same too). Spaced by what's drawn: a label's letters
    // sit inside its line, and the icon inside its bigger tap box.
    void lay_out() {
        const lv_font_t* font_12 = ui::font(12);
        const lv_font_t* font_28 = ui::font(28);
        const int icon_size = ui::px(IconSize), row_gap = ui::px(RowGap);
        const int toggle_height = ui::px(ToggleHeight);
        const int name_top = ui::ink_top(font_12), name_height = ui::ink_height(font_12);
        const int digits_top = ui::px(ValueTop) + (ui::px(34) - lv_font_get_line_height(font_28)) / 2 + ui::ink_top(font_28, '0');
        int height = icon_size + row_gap + name_height;
        if (has_toggle_) height += row_gap + toggle_height;
        if (has_picker_) height += row_gap + ui::px(CardHeight);
        int y = ui::px(ContentTop) + (digits_top - ui::px(ContentTop) - height) / 2;
        lv_obj_align(icon_box_, LV_ALIGN_TOP_MID, 0, y - (ui::px(IconBox) - icon_size) / 2);
        y += icon_size + row_gap;
        lv_obj_align(name_, LV_ALIGN_TOP_MID, 0, y - name_top);
        y += name_height + row_gap;
        if (has_toggle_) {
            lv_obj_align(toggle_, LV_ALIGN_TOP_MID, 0, y);
            y += toggle_height + row_gap;
        }
        if (has_picker_) lv_obj_align(card_, LV_ALIGN_TOP_MID, 0, y);
    }

    // A card line: changed only when the text does (so its marquee doesn't restart), at the top (y > 0) or bottom.
    static void set_text(lv_obj_t* label, const char* text, int y) {
        if (std::string(lv_label_get_text(label)) != text) {
            lv_label_set_text(label, text);
            ui::marquee(label, ui::px(TextWidth));
        }
        lv_obj_align(label, y > 0 ? LV_ALIGN_TOP_MID : LV_ALIGN_BOTTOM_MID, 0, y);
    }

    lv_obj_t* arc_;
    lv_obj_t* icon_box_;
    lv_obj_t* name_;
    lv_obj_t* toggle_;
    lv_obj_t* toggle_icon_ = nullptr;
    lv_obj_t* toggle_text_;
    lv_obj_t* card_;
    lv_obj_t* option_name_;
    lv_obj_t* option_detail_;
    lv_obj_t* arrows_[2] = {};
    lv_obj_t* value_box_;
    lv_obj_t* value_;
    lv_timer_t* rest_timer_ = nullptr;
    std::vector<Option> options_;
    std::string icon_name_, unit_ = "%", muted_text_ = "Muted", empty_text_ = "None";
    std::string toggle_id_, toggle_icon_name_;
    uint32_t color_ = theme::Accent;
    uint32_t toggle_on_color_ = Unset, toggle_off_color_ = Unset;
    int value_now_ = 0;
    int step_ = 2;
    int shown_ = -1;  // the option on the card
    Control focus_ = Value;
    Control focus_before_ = Value;  // before the last press, for a double press
    uint32_t pressed_at_ = 0;       // the last press's tick, 0 once used
    bool muted_ = false, toggle_on_ = false, has_toggle_ = false, has_picker_ = false;
};

}  // namespace

TemplateScreen* make_controls(JsonObjectConst data, TemplateSink sink) { return new ControlsScreen(data, std::move(sink)); }
