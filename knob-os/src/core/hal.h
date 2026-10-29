#pragma once
#include <cstdint>
#include <ctime>
#include <vector>

#include "board.h"
#include "haptics.h"

// What Knob OS needs from the hardware layer (src/hal/<target>/). Everything else in src/core is portable.

// The board this is: its screen size and what it has (see board.h). The display the hardware layer creates is
// Board::size square.
const Board& hal_board();

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

// Boards with a motor on the knob (Board::motor): how turning should feel now (see haptics.h), at strength percent.
void hal_motor_feel(const haptics::Feel& feel, int strength);

// Boards with a vibration motor (Board::vibration): plays an effect at strength percent.
void hal_vibrate(haptics::Effect effect, int strength);

// Boards with LEDs (Board::leds): each LED's colour, 0xRRGGBB with the brightness applied, in order round the ring
// (see lights.h), and where the screen's arc for them is (Glow::arc_start, arc_sweep: for a hardware layer that draws
// them, like the preview's). Called when they change.
void hal_set_leds(const uint32_t* colors, int count, int arc_start, int arc_sweep);
