#include "board.h"

#include <cstring>

namespace boards {
namespace {

// Screens are designed at 240 px and scaled to the others (see ui::px). A board added here also needs its fonts
// (fonts/make_fonts.py) and, for the real thing, a board file in src/hal/esp32/boards/.
const Board Known[] = {
    {"crowpanel-1.28", "CrowPanel 1.28\"", 240, true, 5, false, false},
    {"crowpanel-1.46", "CrowPanel 1.46\"", 360, true, 1, false, false},
    // A knob with a motor and a vibration motor (SmartKnob style): not built yet, here so the haptics can be tried in
    // the simulator and Knob Hub's preview.
    {"motor-knob", "Motor knob", 240, true, 8, true, true},
};

}  // namespace

const Board* all(int& count) {
    count = sizeof Known / sizeof Known[0];
    return Known;
}

const Board* find(const char* id) {
    if (!id) return nullptr;
    for (const Board& b : Known) {
        if (std::strcmp(b.id, id) == 0) return &b;
    }
    return nullptr;
}

}  // namespace boards
