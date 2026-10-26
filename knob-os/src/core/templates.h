#pragma once
#include <ArduinoJson.h>

#include <functional>
#include <string>
#include <utility>

#include "screens.h"

// Screen templates: Knob Hub apps (and Knob OS's own Settings) describe a screen as data, and the template draws it
// and handles the knob. Each template reports what you did as an event:
//   press  - pressed the knob (or tapped) on an item; item = its id ("" when the screen has no items)
//   tap    - tapped/pressed a control such as a pill or button; item = its id, delta = new index for segments
//   rotate - turned the knob where the turn itself is the value (a dial); delta = +1 / -1 per step
//   select - picked an option (on a controls screen's picker); item = its id
//   back   - long press; the app decides where that goes
struct TemplateEvent {
    const char* kind;
    std::string item;
    int delta;
};
using TemplateSink = std::function<void(const TemplateEvent&)>;

class TemplateScreen : public Screen {
public:
    TemplateScreen(lv_obj_t* root, TemplateSink sink) : Screen(root), sink_(std::move(sink)) {}

    // Applies a partial update (e.g. a new volume) without rebuilding the screen.
    virtual void update(JsonObjectConst data) = 0;

protected:
    void emit(const char* kind, const std::string& item = std::string(), int delta = 0) {
        if (sink_) sink_(TemplateEvent{kind, item, delta});
    }

private:
    TemplateSink sink_;
};

// "dial", "tiles", "list", "media", "controls" or "ring"; nullptr for an unknown template.
TemplateScreen* make_template(const char* name, JsonObjectConst data, TemplateSink sink);
