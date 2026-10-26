#pragma once
#include <ArduinoJson.h>

#include <string>

// The knob's own settings. They live on the knob (so they work without the PC) and are kept in step with Knob Hub's
// Settings page: a change on either side is sent to the other.
struct KnobSettings {
    int brightness = 80;       // screen, percent
    int led_brightness = 70;   // percent
    int led_mode = 0;          // 0 level meter, 1 app colour, 2 off
    int timeout = 1;           // index into the timeout choices (15 s, 30 s, 1 min, 5 min, never)
    bool dim_idle = true;
    bool leds_idle_off = true;
    bool clock_24h = false;    // the Home clock: 12-hour with AM/PM, or 24-hour
    std::string name = "knob-7f3a";
};

namespace settings {

void load();
KnobSettings& get();

// The Settings app (opened from Home).
void open();

// Redraws whichever Settings page is on screen (e.g. after the connection changed).
void refresh();

void apply_from_hub(JsonObjectConst msg);

// The Settings pages were taken off the screen by someone else (Knob Hub syncing its preview).
void forget_pages();
void send_all();

}  // namespace settings
