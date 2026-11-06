#pragma once
#include <lvgl.h>

#include "input.h"

#define KNOB_OS_VERSION "1.0.6"

// Knob OS's entry points, called by the hardware layer (src/hal/<target>/) from its main loop.

// Builds the interface on the active display. The display and a touch pointer (if any) must already exist.
void knob_os_start();

// A knob action (turn, press, back).
void knob_os_input(KnobInput input);

// A touch from Knob Hub's preview (screen pixels, finger down or lifted). It behaves exactly like a finger on the
// screen. (The knob's own touch screen goes through the hardware layer.)
void knob_os_touch(int x, int y, bool down);

// The touch under way ends without being a tap (on the knob: the button was pushed with it). For Knob Hub's preview
// following the knob.
void knob_os_cancel_touch(int x, int y);

// The knob's own touch screen changed (finger down, moved, lifted), or its touch was cancelled. The hardware layer's
// touch input already handles it; this only tells Knob Hub, so its preview can follow.
void knob_os_report_touch(int x, int y, bool down, bool cancelled = false);

// The link to Knob Hub came up or went down (USB, Wi-Fi, or the simulator's local socket).
void knob_os_link_connected(bool connected);

// One message received from Knob Hub: a JSON object, without the trailing newline.
void knob_os_link_line(const char* line);

// The Wi-Fi state changed (see hal_wifi).
void knob_os_wifi_changed();
