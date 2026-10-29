#pragma once
#include <lvgl.h>

#include "haptics.h"
#include "input.h"
#include "lights.h"

// A full-screen view inside the round display: Home, an app screen, a Settings page, a message.
class Screen {
public:
    explicit Screen(lv_obj_t* root) : root_(root) {}
    virtual ~Screen() { lv_obj_delete(root_); }
    Screen(const Screen&) = delete;
    Screen& operator=(const Screen&) = delete;

    virtual void input(KnobInput) {}
    // How turning the knob should feel on this screen right now (on boards with a motor on the knob): its detents
    // and end stops, from what the screen shows. The default is endless medium clicks.
    virtual haptics::Feel feel() const { return {}; }
    // What the LEDs show on this screen (on boards with LEDs): its colour and level. The default is the open app's
    // colour with no level.
    virtual lights::Glow glow() const { return {}; }
    lv_obj_t* root() const { return root_; }

private:
    lv_obj_t* root_;
};

// The screens on the knob as a stack: Home at the bottom, an app's screens on top. Only the top one is visible and
// gets the knob's input.
namespace screens {

void init(lv_obj_t* round_display);

// Where new screens create their root object.
lv_obj_t* parent();

void push(Screen* screen);
// Removes the top screen (never Home).
void pop();
// Replaces the top screen (for an app's screen changes); pushes if only Home is shown.
void replace(Screen* screen);
// Back to Home.
void home();

Screen* top();
int depth();  // 1 = Home only

void input(KnobInput input);

}  // namespace screens
