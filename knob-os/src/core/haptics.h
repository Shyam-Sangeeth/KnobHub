#pragma once

// What you feel through the knob, on boards that can: a motor on the knob gives it detents and end stops in software
// (SmartKnob style), and a vibration motor gives short clicks and buzzes. Boards without either ignore all this.
//
// Apps don't deal with it: each screen describes how turning it should feel (its Feel), from what it shows (a list's
// rows, a dial's range), and Knob OS passes that to the hardware whenever it changes.
namespace haptics {

// How strong each detent (click) is: Fine for values you turn through quickly (a volume), Medium for items, Firm for
// a few distinct choices (Home's pages, a picker's options).
enum class Detent { Fine, Medium, Firm };

struct Feel {
    int positions = 0;  // detent positions; 0 = endless (clicks with no end, e.g. seeking)
    int position = 0;   // which one the screen is on
    bool wrap = false;  // the last position turns round to the first: no end stops
    Detent detent = Detent::Medium;

    bool bounded() const { return positions > 0 && !wrap; }
    bool operator==(const Feel& o) const {
        return positions == o.positions && position == o.position && wrap == o.wrap && detent == o.detent;
    }
    bool operator!=(const Feel& o) const { return !(*this == o); }
};

// Things to feel on the vibration motor.
enum class Effect {
    Tick,   // turned one detent
    Bump,   // turned against an end stop: nothing moved
    Click,  // pressed
    Alert,  // a message popped up (a toast)
};

// The top screen's feel, given to the motor when it differs from the last one. A changed position without a turn
// means the value changed elsewhere (e.g. the volume on the PC): a motor knob can turn itself to match.
void apply(const Feel& feel);

// Gives the motor the last feel again (its strength setting changed).
void reapply();

// The knob was turned and the screen did (moved) or didn't (at an end stop) change: a tick or a bump on the vibration
// motor. Boards with a motor on the knob skip these: the motor's own detents and end stops are felt already.
void turned(bool moved);

void play(Effect effect);

}  // namespace haptics
