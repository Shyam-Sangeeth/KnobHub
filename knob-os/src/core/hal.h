#pragma once
#include <cstdint>
#include <ctime>
#include <vector>

// What Knob OS needs from the hardware layer (src/hal/<target>/). Everything else in src/core is portable.

// Local time for the clock. Returns false until the time is known (the knob gets it from Knob Hub or Wi-Fi).
bool hal_local_time(std::tm& out);

// Sends one message to Knob Hub (a JSON object; the hardware layer adds the newline). Dropped if not connected.
void hal_link_send(const char* line);

// Screen backlight, 0-100.
void hal_set_brightness(int percent);

// Small files kept across restarts (what Knob Hub sent last: the app list and icons), read and written whole by name.
// hal_store_read returns false when there's no such file.
bool hal_store_read(const char* name, std::vector<uint8_t>& out);
void hal_store_write(const char* name, const std::vector<uint8_t>& data);
