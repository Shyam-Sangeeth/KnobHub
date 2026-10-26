// Knob OS as a DLL for Knob Hub's live preview of the knob. Knob Hub feeds it the same messages it sends the real
// knob and replays the real knob's actions (its "key" messages), so the preview shows exactly what the knob shows.
// With no knob connected it is the knob: Knob Hub talks to it like to a real one.
//
// Everything runs on the caller's thread: call kp_start once, then kp_tick about every 16 ms, and read the screen
// with kp_frame (240x240, 32-bit BGRX, the layout of WPF's Bgr32).
#include <windows.h>

#include <lvgl.h>

#include <ctime>
#include <map>
#include <string>
#include <vector>

#include "core/hal.h"
#include "core/knob_os.h"

#define KP_API extern "C" __declspec(dllexport)

typedef void(__stdcall* kp_send_fn)(const char* line);

namespace {

constexpr int Size = 240;

std::vector<uint32_t> frame(Size * Size);
uint32_t frame_serial = 0;  // changes whenever LVGL redraws part of the screen
kp_send_fn send_line = nullptr;
int brightness = 100;
std::map<std::string, std::vector<uint8_t>> store;  // the preview keeps nothing across restarts: Knob Hub refills it

void flush(lv_display_t* display, const lv_area_t*, uint8_t*) {
    if (lv_display_flush_is_last(display)) frame_serial++;
    lv_display_flush_ready(display);
}

}  // namespace

bool hal_local_time(std::tm& out) {
    std::time_t now = std::time(nullptr);
    std::tm* local = std::localtime(&now);
    if (!local) return false;
    out = *local;
    return true;
}

void hal_link_send(const char* line) {
    if (send_line) send_line(line);
}

void hal_set_brightness(int percent) { brightness = percent; }

bool hal_store_read(const char* name, std::vector<uint8_t>& out) {
    auto it = store.find(name);
    if (it == store.end()) return false;
    out = it->second;
    return true;
}

void hal_store_write(const char* name, const std::vector<uint8_t>& data) { store[name] = data; }

// send receives every message the preview knob sends to Knob Hub (a JSON object, no newline).
KP_API void kp_start(kp_send_fn send) {
    send_line = send;
    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return static_cast<uint32_t>(GetTickCount64()); });

    lv_display_t* display = lv_display_create(Size, Size);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(display, frame.data(), nullptr, static_cast<uint32_t>(frame.size() * sizeof(uint32_t)),
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, flush);

    knob_os_start();
}

KP_API void kp_tick() { lv_timer_handler(); }

// 0 turn right, 1 turn left, 2 press, 3 back (KnobInput).
KP_API void kp_input(int input) {
    if (input >= 0 && input <= 3) knob_os_input(static_cast<KnobInput>(input));
}

// A touch on the preview in screen pixels (0-239): down while the mouse button is held (knob_os_touch).
KP_API void kp_touch(int x, int y, int down) { knob_os_touch(x, y, down != 0); }

KP_API void kp_link_connected(int connected) { knob_os_link_connected(connected != 0); }

KP_API void kp_link_line(const char* line) { knob_os_link_line(line); }

// The screen: Size x Size pixels, 4 bytes each. serial (optional) tells whether it changed since the last call.
KP_API const void* kp_frame(uint32_t* serial) {
    if (serial) *serial = frame_serial;
    return frame.data();
}

// The screen brightness the knob is set to (5-100); the preview dims itself to match.
KP_API int kp_brightness() { return brightness; }
