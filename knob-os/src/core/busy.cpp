#include "busy.h"

#include <lvgl.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "screens.h"
#include "theme.h"
#include "ui.h"

namespace busy {
namespace {

// The spinner: rounded spokes round a circle, in the colours of the rainbow. The head steps round clockwise and the
// colours go round with it, the spokes behind it fading a little.
//
// It's drawn into a canvas by hand: LVGL draws a thin line's rounded ends as circles of their own, which at this size
// don't line up with the line and make the spokes look bent. Which spoke covers each pixel, and how much, is worked out
// once; each step only colours the pixels again.
constexpr int Spokes = 12;
constexpr uint32_t StepMs = 1000 / Spokes;  // once round a second
constexpr lv_opa_t FaintestOpa = 110;       // the last spoke behind the head

lv_obj_t* overlay = nullptr;
lv_obj_t* canvas = nullptr;
lv_timer_t* timer = nullptr;
int head = 0;
int size = 0;
std::vector<lv_color32_t> pixels;  // the canvas (ARGB8888)
std::vector<uint8_t> spoke_of;     // per pixel: the spoke on it
std::vector<uint8_t> coverage;     // per pixel: how much of it the spoke covers (0 = none)

float distance_to_segment(float px, float py, float ax, float ay, float bx, float by) {
    float dx = bx - ax, dy = by - ay;
    float t = std::clamp(((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy), 0.0f, 1.0f);
    return std::hypot(px - (ax + t * dx), py - (ay + t * dy));
}

// Each spoke is a capsule: every point within half its width of the line from inner to outer. Edges anti-aliased by
// how far each pixel's centre is from it.
void shape_spokes() {
    const float centre = size / 2.0f, inner = ui::px(14), outer = ui::px(26), half_width = ui::px(5) / 2.0f;
    spoke_of.assign(size * size, 0);
    coverage.assign(size * size, 0);
    for (int i = 0; i < Spokes; i++) {
        float angle = i * 2 * 3.14159265f / Spokes;  // clockwise from the top
        float x = std::sin(angle), y = -std::cos(angle);
        float ax = centre + x * inner, ay = centre + y * inner, bx = centre + x * outer, by = centre + y * outer;
        int left = std::max(0, static_cast<int>(std::min(ax, bx) - half_width - 1));
        int right = std::min(size - 1, static_cast<int>(std::max(ax, bx) + half_width + 1));
        int top = std::max(0, static_cast<int>(std::min(ay, by) - half_width - 1));
        int bottom = std::min(size - 1, static_cast<int>(std::max(ay, by) + half_width + 1));
        for (int py = top; py <= bottom; py++) {
            for (int px = left; px <= right; px++) {
                float d = distance_to_segment(px + 0.5f, py + 0.5f, ax, ay, bx, by);
                float cover = std::clamp(half_width + 0.5f - d, 0.0f, 1.0f);
                uint8_t c = static_cast<uint8_t>(std::lround(cover * 255));
                if (c > coverage[py * size + px]) {
                    coverage[py * size + px] = c;
                    spoke_of[py * size + px] = static_cast<uint8_t>(i);
                }
            }
        }
    }
}

void paint() {
    lv_color32_t colors[Spokes];
    lv_opa_t opas[Spokes];
    for (int i = 0; i < Spokes; i++) {
        int behind = (head - i + Spokes) % Spokes;
        lv_color_t c = lv_color_hsv_to_rgb(static_cast<uint16_t>(behind * 360 / Spokes), 85, 100);  // red at the head
        colors[i] = lv_color_to_32(c, LV_OPA_COVER);
        opas[i] = LV_OPA_COVER - behind * (LV_OPA_COVER - FaintestOpa) / (Spokes - 1);
    }
    for (size_t p = 0; p < pixels.size(); p++) {
        lv_color32_t px = colors[spoke_of[p]];
        px.alpha = static_cast<uint8_t>(coverage[p] * opas[spoke_of[p]] / 255);
        pixels[p] = px;
    }
    lv_obj_invalidate(canvas);
}

void step(lv_timer_t*) {
    head = (head + 1) % Spokes;
    paint();
}

// A swipe right on the overlay is Back, as it is on the screen under it. Done once LVGL has finished with the touch,
// since going back may change the screen.
void back_soon(void*) { screens::input(KnobInput::Back); }

void on_gesture(lv_event_t*) {
    lv_indev_t* indev = lv_indev_active();
    if (!indev || lv_indev_get_gesture_dir(indev) != LV_DIR_RIGHT) return;
    lv_indev_wait_release(indev);
    lv_async_call(back_soon, nullptr);
}

}  // namespace

void set(bool on) {
    if (!on) {
        if (timer) lv_timer_delete(timer);
        if (overlay) lv_obj_delete(overlay);
        overlay = canvas = nullptr;
        timer = nullptr;
        pixels = {};
        spoke_of = {};
        coverage = {};
        return;
    }
    if (overlay) return;

    // Over the screen and taking every touch, so nothing under it can be used.
    overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(overlay, theme::color(theme::Screen), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_80, 0);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(overlay, on_gesture, LV_EVENT_GESTURE, nullptr);

    // The spokes on the screen's own background, so what's under them doesn't show through the gaps.
    size = ui::px(70);
    lv_obj_t* ring = ui::circle(overlay, size, theme::Screen, 0, 0);
    lv_obj_center(ring);
    pixels.assign(size * size, lv_color32_t{});
    shape_spokes();
    canvas = lv_canvas_create(ring);
    lv_canvas_set_buffer(canvas, pixels.data(), size, size, LV_COLOR_FORMAT_ARGB8888);
    lv_obj_center(canvas);
    lv_obj_remove_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
    head = 0;
    paint();
    timer = lv_timer_create(step, StepMs, nullptr);
}

bool active() { return overlay != nullptr; }

}  // namespace busy
