#pragma once
#include <lvgl.h>

#include "input.h"

// A full-screen view inside the round display: Home, an app screen, a Settings page, a message.
class Screen {
public:
    explicit Screen(lv_obj_t* root) : root_(root) {}
    virtual ~Screen() { lv_obj_delete(root_); }
    Screen(const Screen&) = delete;
    Screen& operator=(const Screen&) = delete;

    virtual void input(KnobInput) {}
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
