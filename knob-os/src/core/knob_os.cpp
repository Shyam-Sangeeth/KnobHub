#include "knob_os.h"

#include <cstring>
#include <deque>

#include "hal.h"
#include "apps.h"
#include "home.h"
#include "icons.h"
#include "link.h"
#include "screens.h"
#include "settings.h"
#include "theme.h"

namespace {
void read_remote_touch(lv_indev_t*, lv_indev_data_t* data);
}

void knob_os_start() {
    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, theme::color(theme::Bezel), 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    // The panel itself is round; everything is drawn inside this circle, so the simulator looks the same.
    lv_obj_t* circle = lv_obj_create(screen);
    lv_obj_remove_style_all(circle);
    lv_obj_set_size(circle, 240, 240);
    lv_obj_center(circle);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(circle, true, 0);
    lv_obj_set_style_bg_color(circle, theme::color(theme::Screen), 0);
    lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
    lv_obj_remove_flag(circle, LV_OBJ_FLAG_SCROLLABLE);

    // What Knob Hub sent last time, so the Home ring is complete before it connects (it checks for changes then).
    apps::load();
    icons::load();
    settings::load();
    screens::init(circle);
    screens::push(home_create());

    lv_indev_t* remote = lv_indev_create();
    lv_indev_set_type(remote, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(remote, read_remote_touch);
    hal_set_brightness(settings::get().brightness);
}

namespace {
const char* const InputNames[] = {"right", "left", "press", "back"};
}

const char* knob_input_name(KnobInput input) { return InputNames[static_cast<int>(input)]; }

bool knob_input_parse(const char* name, KnobInput& out) {
    for (int i = 0; i < 4; i++) {
        if (name && std::strcmp(name, InputNames[i]) == 0) {
            out = static_cast<KnobInput>(i);
            return true;
        }
    }
    return false;
}

void knob_os_input(KnobInput input) {
    JsonDocument key;
    key["t"] = "key";
    key["input"] = knob_input_name(input);
    hub_link::send(key);
    screens::input(input);
}

namespace {

// Touches from Knob Hub's preview go to LVGL through a pointer device of their own, so taps, drags and swipes work
// exactly like a finger on the knob's screen. They're queued so a quick tap isn't lost between two reads.
struct TouchSample {
    int16_t x, y;
    bool down;
};
std::deque<TouchSample> touches;
TouchSample current_touch = {0, 0, false};  // what LVGL last read

void read_remote_touch(lv_indev_t*, lv_indev_data_t* data) {
    if (!touches.empty()) {
        current_touch = touches.front();
        touches.pop_front();
    }
    data->point.x = current_touch.x;
    data->point.y = current_touch.y;
    data->state = current_touch.down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->continue_reading = !touches.empty();
}

}  // namespace

void knob_os_report_touch(int x, int y, bool down) {
    JsonDocument touch;
    touch["t"] = "touch";
    touch["x"] = x;
    touch["y"] = y;
    touch["down"] = down;
    hub_link::send(touch);
}

void knob_os_touch(int x, int y, bool down) {
    knob_os_report_touch(x, y, down);
    if (touches.size() < 64) touches.push_back({static_cast<int16_t>(x), static_cast<int16_t>(y), down});
}

void knob_os_link_connected(bool connected) { hub_link::set_connected(connected); }

void knob_os_link_line(const char* line) { hub_link::receive(line); }
