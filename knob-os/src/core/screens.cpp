#include "screens.h"

#include <vector>

#include "haptics.h"

namespace screens {
namespace {

lv_obj_t* display = nullptr;
std::vector<Screen*> stack;

// Swiping right means "back" on every screen above Home, like the knob's long press. Done once LVGL has finished
// with the touch, since going back may delete the screen.
void back_soon(void*) { input(KnobInput::Back); }

void on_back_gesture(lv_event_t* e) {
    lv_indev_t* indev = lv_indev_active();
    if (!indev || lv_indev_get_gesture_dir(indev) != LV_DIR_RIGHT) return;
    lv_indev_wait_release(indev);  // no tap when the finger lifts
    lv_async_call(back_soon, nullptr);
}

void add_back_gesture(Screen* screen) {
    lv_obj_add_event_cb(screen->root(), on_back_gesture, LV_EVENT_GESTURE, nullptr);
}

void show_top() {
    for (size_t i = 0; i < stack.size(); i++) {
        if (i + 1 == stack.size()) lv_obj_remove_flag(stack[i]->root(), LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(stack[i]->root(), LV_OBJ_FLAG_HIDDEN);
    }
}

}  // namespace

void init(lv_obj_t* round_display) { display = round_display; }

lv_obj_t* parent() { return display; }

void push(Screen* screen) {
    if (!stack.empty()) add_back_gesture(screen);  // not on Home
    stack.push_back(screen);
    show_top();
}

void pop() {
    if (stack.size() <= 1) return;
    delete stack.back();
    stack.pop_back();
    show_top();
}

void replace(Screen* screen) {
    add_back_gesture(screen);
    if (stack.size() > 1) {
        delete stack.back();
        stack.back() = screen;
    } else {
        stack.push_back(screen);
    }
    show_top();
}

void home() {
    while (stack.size() > 1) {
        delete stack.back();
        stack.pop_back();
    }
    show_top();
}

Screen* top() { return stack.empty() ? nullptr : stack.back(); }

int depth() { return static_cast<int>(stack.size()); }

// Turning ticks when it changed something and bumps at an end stop; pressing (or a long press) clicks.
void input(KnobInput in) {
    Screen* s = top();
    if (!s) return;
    bool turn = in == KnobInput::TurnRight || in == KnobInput::TurnLeft;
    haptics::Feel before = s->feel();
    int depth_before = depth();
    s->input(in);
    if (!turn) haptics::play(haptics::Effect::Click);
    else if (top() != s || depth() != depth_before) haptics::turned(true);  // it opened or closed a screen
    else haptics::turned(!before.bounded() || s->feel().position != before.position);
    if (Screen* now = top()) haptics::apply(now->feel());
}

}  // namespace screens
