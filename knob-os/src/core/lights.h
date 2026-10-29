#pragma once
#include <cstdint>

// The knob's RGB LEDs, on boards that have them (Board::leds). Like haptics, apps don't deal with them: each screen
// says what its lights show (its Glow), and Knob OS turns that into the LEDs' colours by the Lights settings:
//   Level meter: the LEDs fill up with the screen's level (a volume, how far a song has played) in its colour, in
//                order round the ring; one LED gets brighter instead. A screen without a level lights them all.
//   App colour:  all of them in the screen's colour (the open app's, or the Home page in focus).
//   Off.
// Brightness is the LED brightness setting. They're off while the screen is off, and while it's dimmed too with LEDs
// when dimmed set to off (see idle.h).
namespace lights {

constexpr uint32_t NoColor = 0xFFFFFFFF;

struct Glow {
    uint32_t color = NoColor;  // 0xRRGGBB, or NoColor: the open app's colour
    int level = -1;            // 0-100 for the level meter, or -1: none on this screen
    // Where the screen's arc for the level is: its start in degrees clockwise from 3 o'clock (as LVGL's arcs) and how
    // far round it goes. A knob's LEDs are where they are; Knob Hub's preview draws them along it. The default is all
    // the way round from the top.
    int arc_start = 270;
    int arc_sweep = 360;

    bool operator==(const Glow& o) const {
        return color == o.color && level == o.level && arc_start == o.arc_start && arc_sweep == o.arc_sweep;
    }
};

// The top screen's glow, given to the LEDs when it or the settings changed.
void apply(const Glow& glow);

}  // namespace lights
