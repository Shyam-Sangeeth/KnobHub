#pragma once

// The knob boards Knob OS knows. Their screens are always round and they're always ESP32s; they differ in the
// screen's size and in what else they have. The board in use comes from the hardware layer (hal_board): the real
// knob's own, or the one the simulator or Knob Hub's preview pretends to be. Knob OS tells Knob Hub which it is in
// its hello message.
struct Board {
    const char* id;    // "crowpanel-1.28": in hello, and for picking one in the simulator and the preview
    const char* name;  // for people: "CrowPanel 1.28\""
    int size;          // the round screen's diameter, pixels
    bool touch;        // a touch screen
    int leds;          // RGB LEDs (0 = none)
    bool vibration;    // a vibration motor, for clicks and alerts you feel
    bool motor;        // a motor on the knob itself: detents and end stops in software (like SmartKnob)
};

namespace boards {

// Every board Knob OS knows, the first being the default.
const Board* all(int& count);

// A known board by id, or null.
const Board* find(const char* id);

}  // namespace boards
