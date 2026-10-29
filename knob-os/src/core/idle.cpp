#include "idle.h"

#include <lvgl.h>

#include <algorithm>
#include <cstring>

#include "hal.h"
#include "link.h"
#include "settings.h"

namespace idle {
namespace {

constexpr uint32_t TimeoutMs[] = {15000, 30000, 60000, 300000, 0};  // the Screen timeout setting's; 0 = never
constexpr uint32_t DimWithoutTimeoutMs = 60000;
const char* const Names[] = {"awake", "dim", "off"};

State current = State::Awake;
bool following = false;
int shown_brightness = -1;
// Over everything while the screen is off, so a touch only wakes it. It stays until that touch lets go: the rest of
// the touch (a drag, a swipe) mustn't reach the screen either.
lv_obj_t* blocker = nullptr;
bool blocker_pressed = false;

void place_blocker();

// The state the time since the last input calls for (LVGL counts touches; input() counts the knob).
State due() {
    const KnobSettings& s = settings::get();
    uint32_t timeout = TimeoutMs[std::min(std::max(s.timeout, 0), 4)];
    uint32_t idle_ms = lv_display_get_inactive_time(nullptr);
    if (timeout && idle_ms >= timeout) return State::Off;
    if (s.dim_idle && idle_ms >= (timeout ? timeout / 2 : DimWithoutTimeoutMs)) return State::Dim;
    return State::Awake;
}

void show_brightness() {
    int full = settings::get().brightness;
    int percent = current == State::Off ? 0 : current == State::Dim ? std::min(full, std::max(5, full / 4)) : full;
    if (percent == shown_brightness) return;
    shown_brightness = percent;
    hal_set_brightness(percent);
}

void report() {
    JsonDocument msg;
    msg["t"] = "idle";
    msg["state"] = Names[static_cast<int>(current)];
    hub_link::send(msg);
}

void set(State s) {
    if (s == current) return;
    current = s;
    show_brightness();
    place_blocker();
    if (!following) report();
}

void update() {
    if (!following) set(due());
}

void on_blocker(lv_event_t* e) {
    switch (lv_event_get_code(e)) {
        case LV_EVENT_PRESSED:
            blocker_pressed = true;
            update();  // LVGL counted the touch: awake now (unless following a knob, which decides)
            break;
        case LV_EVENT_RELEASED:
        case LV_EVENT_PRESS_LOST:
            blocker_pressed = false;
            place_blocker();
            break;
        default: break;
    }
}

void place_blocker() {
    bool wanted = current == State::Off || blocker_pressed;
    if (wanted && !blocker) {
        blocker = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(blocker);
        lv_obj_set_size(blocker, LV_PCT(100), LV_PCT(100));
        lv_obj_add_flag(blocker, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(blocker, on_blocker, LV_EVENT_ALL, nullptr);
    } else if (!wanted && blocker) {
        lv_obj_delete_async(blocker);  // maybe from inside its own event
        blocker = nullptr;
    }
}

}  // namespace

void init() {
    show_brightness();
    lv_timer_create([](lv_timer_t*) { update(); }, 250, nullptr);
}

bool input() {
    if (following) return false;  // the knob decided already: the preview only gets the inputs it acted on
    bool was_off = current == State::Off;
    lv_display_trigger_activity(nullptr);
    update();
    return was_off;
}

void wake() {
    if (following) return;  // the knob wakes for it and says so
    lv_display_trigger_activity(nullptr);
    update();
}

State state() { return current; }

bool leds_off() { return current == State::Off || (current == State::Dim && settings::get().leds_idle_off); }

void refresh() { show_brightness(); }

void follow(const char* state) {
    following = true;
    State s = State::Awake;
    for (int i = 0; i < 3; i++) {
        if (state && std::strcmp(state, Names[i]) == 0) s = static_cast<State>(i);
    }
    set(s);
}

void lead() {
    if (!following) return;
    following = false;
    lv_display_trigger_activity(nullptr);
    update();
}

void on_connected(bool connected) {
    if (connected && !following) report();
}

}  // namespace idle
