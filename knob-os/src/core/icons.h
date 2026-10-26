#pragma once
#include <ArduinoJson.h>
#include <lvgl.h>

#include <cstdint>
#include <string>

// Icons for the apps, drawn by Knob Hub: Knob OS asks for an icon by name at the size it shows it, and Knob Hub sends
// it as an alpha-only picture that Knob OS colours like text. So Knob OS needs no app's icons, and an app (a Knob Hub
// plugin) can bring its own. Knob OS's own few icons are font symbols (see ui::icon).
//
// Icons are kept across restarts, with Knob Hub's fingerprint (hash) of how it draws each. Every time Knob Hub
// connects it lists the icons it has now; Knob OS drops the ones no longer there (their app was removed) and fetches
// again the ones drawn differently (their app was updated).
//
// Knob -> Knob Hub:  icon_request {name, size}
// Knob Hub -> knob:  icon {name, size, w, h, hash, a8 (base64 of w x h alpha bytes, row by row)}
//                    icons {known: {name: hash, ...}}   every icon Knob Hub can draw, sent when it connects
namespace icons {

void load();  // at start: the icons kept from before

// An image of the icon name, size x size, in color: drawn right away if Knob Hub already sent it, otherwise once it
// arrives (asked for now, or when the link comes up). Not positioned. It takes its room from the start, so layouts
// don't move when the picture comes in; an icon Knob Hub doesn't know stays empty.
lv_obj_t* create(lv_obj_t* parent, const std::string& name, int size, uint32_t color);

// Asks Knob Hub for an icon not kept yet (e.g. one Home will show), without showing it now. Knob OS's own font-symbol
// icons are skipped.
void fetch(const std::string& name, int size);

void receive(JsonObjectConst msg);  // an "icon" message
void check(JsonObjectConst msg);    // an "icons" message
void on_connected(bool connected);  // asks again for the icons still awaited

}  // namespace icons
