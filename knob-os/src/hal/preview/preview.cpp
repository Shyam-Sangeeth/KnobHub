// Knob OS as a DLL for Knob Hub's live preview of the knob. Knob Hub feeds it the same messages it sends the real
// knob and replays the real knob's actions (its "key" messages), so the preview shows exactly what the knob shows.
// With no knob connected it is the knob: Knob Hub talks to it like to a real one.
//
// Everything runs on the caller's thread: pick the board with kp_board (optional; the 1.28" CrowPanel by default),
// call kp_start once, then kp_tick about every 16 ms, and read the screen with kp_frame (kp_size() square, 32-bit
// BGRX, the layout of WPF's Bgr32). A different board needs a fresh copy of the DLL: Knob OS starts only once.
//
// The screen is 480 px whatever the board's (Knob Hub shows it 480 screen pixels across, one pixel each), drawn like
// a knob of that size would (see ui::px): only the rest of the board (LEDs, motors, so its settings) is the board's.
// Pictures Knob Hub drew for the connected knob's size are scaled (ui::picture_px). kp_screen picks another size, to
// see exactly what a 240 or 360 px knob shows.
//
// It has no motors or LEDs: kp_vibration, kp_motor and kp_leds tell Knob Hub what they'd do, so it can show it.
#include <windows.h>

#include <lvgl.h>

#include <ctime>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "core/hal.h"
#include "core/knob_os.h"

#define KP_API extern "C" __declspec(dllexport)

typedef void(__stdcall* kp_send_fn)(const char* line);

namespace {

int screen_size = 480;

Board board;  // the board picked, at screen_size
std::vector<uint32_t> frame;
uint32_t frame_serial = 0;  // changes whenever LVGL redraws part of the screen
kp_send_fn send_line = nullptr;
int brightness = 100;
std::map<std::string, std::vector<uint8_t>> store;  // the preview keeps nothing across restarts: Knob Hub refills it
std::deque<int> vibrations;                          // effects played, until Knob Hub reads them
haptics::Feel motor_feel;
uint32_t motor_serial = 0;                           // changes whenever the motor's feel does
std::vector<uint32_t> leds;
int leds_arc_start = 270, leds_arc_sweep = 360;
uint32_t leds_serial = 0;                            // changes whenever the LEDs do

void pick(const Board& b) {
    board = b;
    board.size = screen_size;
}

void flush(lv_display_t* display, const lv_area_t*, uint8_t*) {
    if (lv_display_flush_is_last(display)) frame_serial++;
    lv_display_flush_ready(display);
}

}  // namespace

const Board& hal_board() {
    if (!board.id) {
        int count = 0;
        pick(boards::all(count)[0]);
    }
    return board;
}

void hal_motor_feel(const haptics::Feel& feel, int) {
    motor_feel = feel;
    motor_serial++;
}

void hal_vibrate(haptics::Effect effect, int) {
    if (vibrations.size() < 32) vibrations.push_back(static_cast<int>(effect));
}

void hal_set_leds(const uint32_t* colors, int count, int arc_start, int arc_sweep) {
    leds.assign(colors, colors + count);
    leds_arc_start = arc_start;
    leds_arc_sweep = arc_sweep;
    leds_serial++;
}

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

// The board to be (an id from core/boards.cpp), before kp_start. Returns the preview's screen size, or 0 for an
// unknown board.
KP_API int kp_board(const char* id) {
    const Board* b = boards::find(id);
    if (!b) return 0;
    pick(*b);
    return screen_size;
}

// The screen's size, before kp_start: 480 (the default), or 240 or 360 for exactly what a knob of that size shows.
// Returns it, or 0 for a size Knob OS has no fonts for.
KP_API int kp_screen(int size) {
    if (size != 240 && size != 360 && size != 480) return 0;
    screen_size = size;
    if (board.id) board.size = size;
    return size;
}

// The screen's size in pixels (it's square).
KP_API int kp_size() { return screen_size; }

// send receives every message the preview knob sends to Knob Hub (a JSON object, no newline).
KP_API void kp_start(kp_send_fn send) {
    send_line = send;
    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return static_cast<uint32_t>(GetTickCount64()); });

    const int size = screen_size;
    frame.assign(static_cast<size_t>(size) * size, 0);
    lv_display_t* display = lv_display_create(size, size);
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

// A touch on the preview in screen pixels (0 to kp_size() - 1): down while the mouse button is held (knob_os_touch).
KP_API void kp_touch(int x, int y, int down) { knob_os_touch(x, y, down != 0); }

KP_API void kp_link_connected(int connected) { knob_os_link_connected(connected != 0); }

KP_API void kp_link_line(const char* line) { knob_os_link_line(line); }

// The screen: kp_size() x kp_size() pixels, 4 bytes each. serial (optional) tells whether it changed since the last
// call.
KP_API const void* kp_frame(uint32_t* serial) {
    if (serial) *serial = frame_serial;
    return frame.data();
}

// The screen brightness the knob is set to (5-100); the preview dims itself to match.
KP_API int kp_brightness() { return brightness; }

// The next effect the vibration motor played (haptics::Effect: 0 tick, 1 bump, 2 click, 3 alert), or -1 for none.
KP_API int kp_vibration() {
    if (vibrations.empty()) return -1;
    int effect = vibrations.front();
    vibrations.pop_front();
    return effect;
}

// How the motor on the knob feels now (haptics::Feel; detent 0 fine, 1 medium, 2 firm). Returns a serial that changes
// whenever it does (0: never set).
KP_API uint32_t kp_motor(int* positions, int* position, int* wrap, int* detent) {
    if (positions) *positions = motor_feel.positions;
    if (position) *position = motor_feel.position;
    if (wrap) *wrap = motor_feel.wrap ? 1 : 0;
    if (detent) *detent = static_cast<int>(motor_feel.detent);
    return motor_serial;
}

// The LEDs' colours (0xRRGGBB with the brightness applied, in order round the ring; see core/lights.h): up to max into
// colors, and how many there are into count; and the screen's arc to show them along (degrees clockwise from 3 o'clock,
// and how far round). Returns a serial that changes whenever they do (0: never set).
KP_API uint32_t kp_leds(uint32_t* colors, int max, int* count, int* arc_start, int* arc_sweep) {
    for (int i = 0; colors && i < max && i < static_cast<int>(leds.size()); i++) colors[i] = leds[i];
    if (count) *count = static_cast<int>(leds.size());
    if (arc_start) *arc_start = leds_arc_start;
    if (arc_sweep) *arc_sweep = leds_arc_sweep;
    return leds_serial;
}
