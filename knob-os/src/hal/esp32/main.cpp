// Knob OS on a real knob: an ESP32-S3 round display. One build per board (see platformio.ini); the board's pins are
// in boards/<board>.h, chosen by KNOB_BOARD.
// Still to be written when the boards arrive: the display (SPI), touch (I2C), the rotary encoder and its press (short
// press = KnobInput::Press, long press = KnobInput::Back), the LEDs (hal_set_leds), the link over USB serial (one JSON message per
// line) and Wi-Fi; on boards that have them, the vibration motor (hal_vibrate) and the motor on the knob
// (hal_motor_feel: detents and end stops by field-oriented control, e.g. SimpleFOC, on its own core).
#include <Arduino.h>
#include <LittleFS.h>
#include <lvgl.h>

#include KNOB_BOARD
#include "core/hal.h"
#include "core/knob_os.h"

const Board& hal_board() {
    static const Board* board = boards::find(KNOB_BOARD_ID);
    return *board;
}

bool hal_local_time(std::tm& out) {
    (void)out;
    return false;  // the time will come from Knob Hub (USB) or the network (Wi-Fi)
}

void hal_link_send(const char* line) { Serial.println(line); }

void hal_set_brightness(int percent) { (void)percent; }

void hal_motor_feel(const haptics::Feel& feel, int strength) {
    (void)feel;
    (void)strength;
}

void hal_vibrate(haptics::Effect effect, int strength) {
    (void)effect;
    (void)strength;
}

void hal_set_leds(const uint32_t* colors, int count, int arc_start, int arc_sweep) {
    (void)colors;
    (void)count;
    (void)arc_start;  // the LEDs are where they are
    (void)arc_sweep;
}

// Files in the flash's file system (LittleFS, in the partition table's data partition).
bool hal_store_read(const char* name, std::vector<uint8_t>& out) {
    File f = LittleFS.open(String("/") + name, "r");
    if (!f) return false;
    out.resize(f.size());
    bool ok = f.read(out.data(), out.size()) == out.size();
    f.close();
    return ok;
}

void hal_store_write(const char* name, const std::vector<uint8_t>& data) {
    File f = LittleFS.open(String("/") + name, "w");
    if (!f) return;
    f.write(data.data(), data.size());
    f.close();
}

void setup() {
    Serial.begin(115200);
    LittleFS.begin(true);  // formats it the first time
    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return millis(); });
    // Create the display (hal_board().size square) and the touch input here, then: knob_os_start();
}

void loop() {
    lv_timer_handler();
    delay(5);
}
