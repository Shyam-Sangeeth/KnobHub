// Knob OS on the real knob (Elecrow CrowPanel 1.28" ESP32-S3 rotary display).
// Still to be written when the board arrives: the GC9A01 display (SPI), CST816D touch (I2C), the rotary encoder and
// its press (short press = KnobInput::Press, long press = KnobInput::Back), the 5 WS2812 LEDs, the link over USB
// serial (one JSON message per line) and Wi-Fi. The pins come from Elecrow's examples and wiki.
#include <Arduino.h>
#include <LittleFS.h>
#include <lvgl.h>

#include "core/hal.h"
#include "core/knob_os.h"

bool hal_local_time(std::tm& out) {
    (void)out;
    return false;  // the time will come from Knob Hub (USB) or the network (Wi-Fi)
}

void hal_link_send(const char* line) { Serial.println(line); }

void hal_set_brightness(int percent) { (void)percent; }

// Files in the flash's file system (LittleFS, in the default partition table's data partition).
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
    // Create the 240x240 display and the touch input here, then: knob_os_start();
}

void loop() {
    lv_timer_handler();
    delay(5);
}
