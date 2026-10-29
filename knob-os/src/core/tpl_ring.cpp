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
constexpr int RingSize = 220, RingWidth = 12;  // design px
constexpr int TextWidth = 168;  // what fits across inside the ring, with a margin (design px)

// Design sizes, biggest first. The digit fonts only have digits and a few signs: only for text they can draw.
struct Size {
    int design;
    bool digits_only;
    const lv_font_t* font() const { return digits_only ? ui::digits(design) : ui::font(design); }
};
const Size Sizes[] = {
    {96, true}, {72, true}, {60, true}, {48, false}, {36, true}, {28, false}, {24, false}, {20, false}, {16, false}, {12, false},
};

bool digits_only(const std::string& text) { return text.find_first_not_of(" %+,-./0123456789:") == std::string::npos; }

class RingScreen : public TemplateScreen {
public:
    RingScreen(JsonObjectConst data, TemplateSink sink) : TemplateScreen(ui::screen_root(screens::parent()), std::move(sink)) {
        lv_obj_t* r = root();
        arc_ = ui::arc(r, ui::px(RingSize), ui::px(RingWidth), 270, 360, DefaultTrack, theme::Accent);
        lv_obj_set_style_arc_rounded(arc_, true, LV_PART_INDICATOR);
        lv_obj_center(arc_);
        text_ = ui::label(r, "", ui::font(48), theme::Text);
        label_ = ui::label(r, "", ui::font(12), theme::TextSecondary);
        lv_obj_set_style_text_letter_space(label_, ui::px(1), 0);
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
        if (data["color"].is<const char*>()) {
            color_ = ui::parse_color(data["color"], theme::Accent);
            lv_obj_set_style_arc_color(arc_, theme::color(color_), LV_PART_INDICATOR);
        }
        if (data["track_color"].is<const char*>())
            lv_obj_set_style_arc_color(arc_, theme::color(ui::parse_color(data["track_color"], DefaultTrack)), LV_PART_MAIN);
        if (data["label"].is<const char*>()) lv_label_set_text(label_, data["label"]);
        if (data["text"].is<const char*>()) {
            text_value_ = data["text"].as<const char*>();
            lv_label_set_text(text_, text_value_.c_str());
        }
        lay_out();
    }

    // The ring's fill as the level meter (the ring goes all the way round from the top, like the default).
    lights::Glow glow() const override { return {color_, lv_arc_get_value(arc_)}; }

private:
    // The text at the biggest size that fits across the ring, its digits' ink at the exact centre; the label above.
    void lay_out() {
        const lv_font_t* font = Sizes[sizeof Sizes / sizeof Sizes[0] - 1].font();
        bool digits = digits_only(text_value_);
        for (const Size& s : Sizes) {
            if (s.digits_only && !digits) continue;
            if (lv_text_get_width(text_value_.c_str(), static_cast<uint32_t>(text_value_.size()), s.font(), 0) <= ui::px(TextWidth)) {
                font = s.font();
                break;
            }
        }
        lv_obj_set_style_text_font(text_, font, 0);
        const int line = lv_font_get_line_height(font);
        const int ink_middle = ui::ink_top(font, '0') + ui::ink_height(font, '0') / 2;
        lv_obj_align(text_, LV_ALIGN_CENTER, 0, line / 2 - ink_middle);

        const int above = ui::ink_height(font, '0') / 2 + ui::px(14);  // over the digits' tops
        lv_obj_align(label_, LV_ALIGN_CENTER, 0, -above - lv_font_get_line_height(ui::font(12)) / 2);
        if (lv_label_get_text(label_)[0]) lv_obj_remove_flag(label_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(label_, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t* arc_;
    uint32_t color_ = theme::Accent;
    lv_obj_t* text_;
    lv_obj_t* label_;
    std::string text_value_;
};

}  // namespace

TemplateScreen* make_ring(JsonObjectConst data, TemplateSink sink) { return new RingScreen(data, std::move(sink)); }
