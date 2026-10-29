#pragma once

// The screen going idle. After the Screen timeout setting's time with nothing done on the knob (a turn, a press, a
// touch) the screen turns off; with Dim when idle on, it first dims to a quarter of its brightness, halfway there
// (after a minute when the timeout is Never). While it's dimmed, the knob works as usual and brightens it again; while
// it's off, an input only wakes it and isn't acted on. A toast (e.g. from Knob Hub) wakes it too, so it's seen; other
// messages (an app's screen updates) don't. The LEDs go off with the screen; with Lights > When dimmed set to off, they
// go off already when it dims.
//
// Knob OS tells Knob Hub the state (an idle message), and Knob Hub passes it to its preview of the knob: while the
// preview mirrors a knob it follows that knob's state instead of timing its own, so both act on the same inputs.
namespace idle {

enum class State { Awake, Dim, Off };

// Starts timing (after the display exists).
void init();

// A knob input (a turn or a press). Returns true when it only woke the screen: don't act on it.
bool input();

// Something to see (a toast): the screen wakes, and the timeout starts again.
void wake();

State state();

// Whether the LEDs are off for idleness: the screen is off, or dimmed with Lights > When dimmed set to off.
bool leds_off();

// Sets the screen brightness for the state and the settings now (after the brightness setting changed).
void refresh();

// From Knob Hub: follow a knob's state ("awake", "dim", "off"; the preview mirroring a knob), or time our own again.
void follow(const char* state);
void lead();

// The link to Knob Hub came up: it gets the state.
void on_connected(bool connected);

}  // namespace idle
