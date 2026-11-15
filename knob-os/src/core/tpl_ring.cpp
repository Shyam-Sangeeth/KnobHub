// "ring": a number (or a short text) in the middle of a full ring, e.g. a counter or a timer.
//   text (as big as fits inside the ring), value (0-100: how far round the ring is filled, clockwise from the top),
//   color (the fill), track_color (the ring under the fill; "" or not sent: a dark track), label (small, above the
//   text; optional), colors (a list of colours spread evenly round the ring clockwise from the top, each blending into
//   the next and the last back into the first: a colour wheel the fill shows instead of color; [] goes back to color),
//   spin (seconds for those colours to go once round: more than 0 clockwise, less than 0 anticlockwise, 0 still; they
//   stand still while the busy spinner shows, the screen is off or another screen is on top)
// Turning reports rotate (+1/-1) and pressing reports press; the app changes the screen. Long press: back.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "busy.h"
#include "idle.h"
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

constexpr uint32_t MinStepMs = 40;  // the wheel turns at most 25 times a second
constexpr int Bands = 10;           // a turn redraws the ring in this many rows of rectangles, not the middle

// A colour wheel as the ring's fill: an image the size of the ring. Only the pixels under the ring are kept, each with
// its angle; turning the wheel recolours them from a palette of 360 colours, one per degree, moved round. The image
// carries the ring's own shape as its transparency (anti-aliased edges, worked out once), so a full ring is drawn as a
// plain image: no masks to work out on every frame. A part fill is drawn by LVGL's arc from the colours alone (it
// masks them to the arc, with rounded ends).
class Wheel {
public:
    // stops: the colours, clockwise from the top. size and width: the ring's, in real pixels.
    void set(std::vector<uint32_t> stops, int size, int width) {
        stops_ = std::move(stops);
        if (size != size_) shape(size, width);
        phase_ = -1;  // paint again
    }

    // Colours the pixels with the wheel turned by phase degrees clockwise. Returns false when nothing changed.
    bool paint(int phase) {
        if (phase == phase_) return false;
        phase_ = phase;
        uint16_t palette[360];
        const int n = static_cast<int>(stops_.size());
        for (int a = 0; a < 360; a++) {
            int at = ((a - phase) % 360 + 360) % 360;  // where this degree is on the unturned wheel
            int scaled = at * n;                       // in stops, times 360
            uint32_t from = stops_[scaled / 360 % n], to = stops_[(scaled / 360 + 1) % n];
            int mix = scaled % 360 * 255 / 359;
            palette[a] = lv_color_to_u16(lv_color_mix(theme::color(to), theme::color(from), static_cast<uint8_t>(mix)));
        }
        auto* colours = reinterpret_cast<uint16_t*>(image_.data());
        for (const Pixel& p : pixels_) colours[p.offset] = palette[p.angle];
        return true;
    }

    // The ring with its shape (RGB565 colours, then 8-bit transparency), for a full ring.
    const lv_image_dsc_t* ring() const { return &ring_; }
    // The colours alone (the same pixels), for LVGL's arc to mask.
    const lv_image_dsc_t* colours() const { return &colours_; }

private:
    struct Pixel {
        uint32_t offset;
        uint16_t angle;  // degrees clockwise from the top
    };

    void shape(int size, int width) {
        size_ = size;
        const size_t count = static_cast<size_t>(size) * size;
        image_.assign(count * 3, 0);  // the colours (2 bytes a pixel), then the transparency (1 byte a pixel)
        uint8_t* alpha = image_.data() + count * 2;
        pixels_.clear();
        // The ring as LVGL's arc draws it: from the image's edge (radius size / 2) inwards by width, each edge pixel
        // as opaque as the part of it inside the ring.
        const float centre = size / 2.0f, outer = size / 2.0f, inner = outer - width;
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                float dx = x + 0.5f - centre, dy = y + 0.5f - centre, d = std::hypot(dx, dy);
                if (d > outer + 1 || d < inner - 1) continue;
                float cover = std::clamp(outer - d + 0.5f, 0.0f, 1.0f) * std::clamp(d - inner + 0.5f, 0.0f, 1.0f);
                alpha[y * size + x] = static_cast<uint8_t>(std::lround(cover * 255));
                int angle = static_cast<int>(std::lround(std::atan2(dx, -dy) * 180 / 3.14159265f));
                pixels_.push_back({static_cast<uint32_t>(y * size + x), static_cast<uint16_t>((angle + 360) % 360)});
            }
        }
        for (lv_image_dsc_t* dsc : {&ring_, &colours_}) {
            *dsc = {};
            dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
            dsc->header.w = static_cast<uint32_t>(size);
            dsc->header.h = static_cast<uint32_t>(size);
            dsc->header.stride = static_cast<uint32_t>(size * 2);
            dsc->data = image_.data();
        }
        ring_.header.cf = LV_COLOR_FORMAT_RGB565A8;
        ring_.data_size = static_cast<uint32_t>(count * 3);
        colours_.header.cf = LV_COLOR_FORMAT_RGB565;
        colours_.data_size = static_cast<uint32_t>(count * 2);
    }

    std::vector<uint32_t> stops_;
    std::vector<uint8_t> image_;
    std::vector<Pixel> pixels_;
    lv_image_dsc_t ring_{}, colours_{};
    int size_ = 0;
    int phase_ = -1;
};

class RingScreen : public TemplateScreen {
public:
    RingScreen(JsonObjectConst data, TemplateSink sink) : TemplateScreen(ui::screen_root(screens::parent()), std::move(sink)) {
        lv_obj_t* r = root();
        arc_ = ui::arc(r, ui::px(RingSize), ui::px(RingWidth), 270, 360, DefaultTrack, theme::Accent);
        lv_obj_set_style_arc_rounded(arc_, true, LV_PART_INDICATOR);
        lv_obj_center(arc_);
        lv_obj_add_event_cb(arc_, on_draw, LV_EVENT_DRAW_MAIN_END, this);
        text_ = ui::label(r, "", ui::font(48), theme::Text);
        label_ = ui::label(r, "", ui::font(12), theme::TextSecondary);
        lv_obj_set_style_text_letter_space(label_, ui::px(1), 0);
        update(data);
    }

    ~RingScreen() override {
        if (timer_) lv_timer_delete(timer_);
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
        if (data["label"].is<const char*>() && std::strcmp(lv_label_get_text(label_), data["label"]) != 0) {
            ui::marquee_text(label_, data["label"], ui::px(TextWidth));
            label_y_ = INT32_MIN;  // its size changed: place it again
        }
        if (data["colors"].is<JsonArrayConst>()) set_colors(data["colors"].as<JsonArrayConst>());
        if (data["spin"].is<float>()) set_spin(data["spin"].as<float>());
        if (data["text"].is<const char*>()) {
            text_value_ = data["text"].as<const char*>();
            lv_label_set_text(text_, text_value_.c_str());
        }
        hide_covered_track();
        lay_out();
    }

    // The ring's fill as the level meter (the ring goes all the way round from the top, like the default).
    lights::Glow glow() const override { return {color_, lv_arc_get_value(arc_)}; }

private:
    void set_colors(JsonArrayConst colors) {
        std::vector<uint32_t> stops;
        for (JsonVariantConst c : colors)
            if (c.is<const char*>()) stops.push_back(ui::parse_color(c, theme::Accent));
        if (stops.empty()) {
            if (wheel_) {
                lv_obj_set_style_arc_opa(arc_, LV_OPA_COVER, LV_PART_INDICATOR);
                wheel_.reset();
                lv_obj_invalidate(arc_);
            }
            set_spin(spin_seconds_);  // stops its timer
            return;
        }
        if (!wheel_) wheel_.reset(new Wheel());
        wheel_->set(std::move(stops), ui::px(RingSize), ui::px(RingWidth));
        wheel_->paint(phase());
        drop_cached();
        lv_obj_set_style_arc_opa(arc_, LV_OPA_TRANSP, LV_PART_INDICATOR);  // the wheel is drawn instead (on_draw)
        lv_obj_invalidate(arc_);
        set_spin(spin_seconds_);
    }

    void set_spin(float seconds) {
        if (seconds != spin_seconds_) {
            phase();  // where it is now, at the old speed
            spin_seconds_ = seconds;
            spin_from_ = lv_tick_get();
            spin_start_ = phase_now_;
        }
        const bool turning = wheel_ && std::fabs(seconds) >= 0.5f;
        if (!turning) {
            if (timer_) lv_timer_delete(timer_);
            timer_ = nullptr;
            return;
        }
        uint32_t step = std::max(MinStepMs, static_cast<uint32_t>(std::fabs(seconds) * 1000 / 360));
        if (!timer_) timer_ = lv_timer_create(on_step, step, this);
        else lv_timer_set_period(timer_, step);
    }

    // How far the wheel has turned (degrees clockwise from where its colours start).
    int phase() {
        if (wheel_ && std::fabs(spin_seconds_) >= 0.5f) {
            float turned = lv_tick_elaps(spin_from_) * 360.0f / (spin_seconds_ * 1000);
            phase_now_ = ((spin_start_ + static_cast<int>(std::floor(turned))) % 360 + 360) % 360;
        }
        return phase_now_;
    }

    // The wheel's pixels changed: LVGL mustn't draw from a copy it keeps.
    void drop_cached() {
        lv_image_cache_drop(wheel_->ring());
        lv_image_cache_drop(wheel_->colours());
    }

    // The dark track is hidden under a full wheel, so it isn't drawn on every frame for nothing.
    void hide_covered_track() {
        const bool covered = wheel_ && lv_arc_get_value(arc_) >= 100;
        if (covered == track_hidden_) return;
        track_hidden_ = covered;
        lv_obj_set_style_arc_opa(arc_, covered ? LV_OPA_TRANSP : LV_OPA_COVER, LV_PART_MAIN);
    }

    // Draws the wheel where the arc's fill would be: a full ring as the image with its own shape, a part fill through
    // LVGL's arc (which can't draw an image as a full circle: its start and end are the same angle then, and it draws
    // only the rounded ends).
    static void on_draw(lv_event_t* e) {
        auto* self = static_cast<RingScreen*>(lv_event_get_user_data(e));
        if (!self->wheel_) return;
        const int value = lv_arc_get_value(self->arc_);
        if (value <= 0) return;
        lv_area_t box;
        lv_obj_get_coords(self->arc_, &box);
        lv_layer_t* layer = lv_event_get_layer(e);
        if (value >= 100) {
            lv_draw_image_dsc_t image;
            lv_draw_image_dsc_init(&image);
            image.src = self->wheel_->ring();
            lv_draw_image(layer, &image, &box);
        } else {
            lv_draw_arc_dsc_t d;
            lv_draw_arc_dsc_init(&d);
            d.img_src = self->wheel_->colours();
            d.width = ui::px(RingWidth);
            d.radius = static_cast<uint16_t>(lv_area_get_width(&box) / 2);
            d.center = {box.x1 + d.radius, box.y1 + d.radius};
            d.opa = LV_OPA_COVER;
            d.rounded = 1;
            d.start_angle = 270;
            d.end_angle = 270 + value * 360 / 100;
            lv_draw_arc(layer, &d);
        }
    }

    static void on_step(lv_timer_t* t) { static_cast<RingScreen*>(lv_timer_get_user_data(t))->step(); }

    void step() {
        // Only while it's seen, and not under the busy spinner (which would have the whole screen drawn again).
        if (busy::active() || idle::state() == idle::State::Off || screens::top() != this) {
            spin_from_ = lv_tick_get();  // it carries on from here afterwards
            spin_start_ = phase_now_;
            return;
        }
        if (!wheel_->paint(phase())) return;
        drop_cached();
        invalidate_ring();
    }

    // Has the ring drawn again without the middle: rows of rectangles over the band of the ring.
    void invalidate_ring() {
        lv_area_t box;
        lv_obj_get_coords(arc_, &box);
        const int size = lv_area_get_width(&box), outer = size / 2 + 1, inner = size / 2 - ui::px(RingWidth) - 2;
        const int cx = box.x1 + size / 2, cy = box.y1 + size / 2, rows = (size + Bands - 1) / Bands;
        for (int y0 = box.y1; y0 <= box.y2; y0 += rows) {
            int y1 = std::min<int>(y0 + rows - 1, box.y2);
            int a = std::abs(y0 - cy), b = std::abs(y1 - cy);
            int near = (y0 <= cy && cy <= y1) ? 0 : std::min(a, b), far = std::max(a, b);
            int out = static_cast<int>(std::ceil(std::sqrt(static_cast<float>(std::max(0, outer * outer - near * near)))));
            int in = far < inner ? static_cast<int>(std::floor(std::sqrt(static_cast<float>(inner * inner - far * far)))) : 0;
            if (in > 0) {
                lv_area_t left = {cx - out, y0, cx - in, y1}, right = {cx + in, y0, cx + out, y1};
                lv_obj_invalidate_area(arc_, &left);
                lv_obj_invalidate_area(arc_, &right);
            } else {
                lv_area_t row = {cx - out, y0, cx + out, y1};
                lv_obj_invalidate_area(arc_, &row);
            }
        }
    }

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
        // Moved only when its place changes: placing a scrolling label again starts its scroll afresh (a timer's
        // update every second would keep it at the start).
        const int label_y = -above - lv_font_get_line_height(ui::font(12)) / 2;
        if (label_y != label_y_) {
            lv_obj_align(label_, LV_ALIGN_CENTER, 0, label_y);
            label_y_ = label_y;
        }
        if (lv_label_get_text(label_)[0]) lv_obj_remove_flag(label_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(label_, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t* arc_;
    uint32_t color_ = theme::Accent;
    lv_obj_t* text_;
    lv_obj_t* label_;
    std::string text_value_;
    int label_y_ = INT32_MIN;  // where the label was last placed
    std::unique_ptr<Wheel> wheel_;  // the colour wheel, when colors are sent
    lv_timer_t* timer_ = nullptr;   // turns it, when spin is sent
    float spin_seconds_ = 0;
    uint32_t spin_from_ = 0;  // the tick the turning is timed from
    int spin_start_ = 0;      // and where the wheel was then
    int phase_now_ = 0;
    bool track_hidden_ = false;
};

}  // namespace

TemplateScreen* make_ring(JsonObjectConst data, TemplateSink sink) { return new RingScreen(data, std::move(sink)); }
