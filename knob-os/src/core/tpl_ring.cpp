// "ring": a number (or a short text) in the middle of a full ring, e.g. a counter or a timer.
//   text (as big as fits inside the ring), value (0-100: how far round the ring is filled, clockwise from the top),
//   color (the fill), track_color (the ring under the fill; "" or not sent: a dark track), label (small, above the
//   text; optional)
// Turning reports rotate (+1/-1) and pressing reports press; the app changes the screen. Long press: back.
#include <string>

#include "templates.h"
#include "theme.h"
#include "ui.h"

namespace {

constexpr uint32_t DefaultTrack = 0x22252B;
constexpr int RingSize = 220, RingWidth = 12;
constexpr int TextWidth = 168;  // what fits across inside the ring, with a margin

// Biggest first. The digit fonts only have digits and a few signs: only for text they can draw.
struct Size {
    const lv_font_t* font;
    bool digits_only;
};
const Size Sizes[] = {
    {&knob_font_digits_96, true}, {&knob_font_digits_72, true}, {&knob_font_digits_60, true}, {&knob_font_48, false},
    {&knob_font_digits_36, true}, {&knob_font_28, false},       {&knob_font_24, false},       {&knob_font_20, false},
    {&knob_font_16, false},       {&knob_font_12, false},
};

bool digits_only(const std::string& text) { return text.find_first_not_of(" %+,-./0123456789:") == std::string::npos; }

class RingScreen : public TemplateScreen {
public:
    RingScreen(JsonObjectConst data, TemplateSink sink) : TemplateScreen(ui::screen_root(screens::parent()), std::move(sink)) {
        lv_obj_t* r = root();
        arc_ = ui::arc(r, RingSize, RingWidth, 270, 360, DefaultTrack, theme::Accent);
        lv_obj_set_style_arc_rounded(arc_, true, LV_PART_INDICATOR);
        lv_obj_center(arc_);
        text_ = ui::label(r, "", &knob_font_48, theme::Text);
        label_ = ui::label(r, "", &knob_font_12, theme::TextSecondary);
        lv_obj_set_style_text_letter_space(label_, 1, 0);
        update(data);
    }

    void input(KnobInput in) override {
        switch (in) {
            case KnobInput::TurnRight: emit("rotate", "", 1); break;
            case KnobInput::TurnLeft: emit("rotate", "", -1); break;
            case KnobInput::Press: emit("press"); break;
            case KnobInput::Back: emit("back"); break;
        }
    }

    void update(JsonObjectConst data) override {
        if (data["value"].is<int>()) lv_arc_set_value(arc_, data["value"].as<int>());
        if (data["color"].is<const char*>())
            lv_obj_set_style_arc_color(arc_, theme::color(ui::parse_color(data["color"], theme::Accent)), LV_PART_INDICATOR);
        if (data["track_color"].is<const char*>())
            lv_obj_set_style_arc_color(arc_, theme::color(ui::parse_color(data["track_color"], DefaultTrack)), LV_PART_MAIN);
        if (data["label"].is<const char*>()) lv_label_set_text(label_, data["label"]);
        if (data["text"].is<const char*>()) {
            text_value_ = data["text"].as<const char*>();
            lv_label_set_text(text_, text_value_.c_str());
        }
        lay_out();
    }

private:
    // The text at the biggest size that fits across the ring, its digits' ink at the exact centre; the label above.
    void lay_out() {
        const lv_font_t* font = Sizes[sizeof Sizes / sizeof Sizes[0] - 1].font;
        bool digits = digits_only(text_value_);
        for (const Size& s : Sizes) {
            if (s.digits_only && !digits) continue;
            if (lv_text_get_width(text_value_.c_str(), static_cast<uint32_t>(text_value_.size()), s.font, 0) <= TextWidth) {
                font = s.font;
                break;
            }
        }
        lv_obj_set_style_text_font(text_, font, 0);
        const int line = lv_font_get_line_height(font);
        const int ink_middle = ui::ink_top(font, '0') + ui::ink_height(font, '0') / 2;
        lv_obj_align(text_, LV_ALIGN_CENTER, 0, line / 2 - ink_middle);

        const int above = ui::ink_height(font, '0') / 2 + 14;  // over the digits' tops
        lv_obj_align(label_, LV_ALIGN_CENTER, 0, -above - lv_font_get_line_height(&knob_font_12) / 2);
        if (lv_label_get_text(label_)[0]) lv_obj_remove_flag(label_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(label_, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t* arc_;
    lv_obj_t* text_;
    lv_obj_t* label_;
    std::string text_value_;
};

}  // namespace

TemplateScreen* make_ring(JsonObjectConst data, TemplateSink sink) { return new RingScreen(data, std::move(sink)); }
