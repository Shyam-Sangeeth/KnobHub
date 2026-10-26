#pragma once

// The knob's controls as the hardware layer reports them. On the knob: turning, a short press, a long press (back).
// In the simulator: mouse wheel, middle click (held: back).
enum class KnobInput { TurnRight, TurnLeft, Press, Back };

// Names used on the link ("key" messages): "right", "left", "press", "back".
const char* knob_input_name(KnobInput input);
bool knob_input_parse(const char* name, KnobInput& out);
