#pragma once
#include <ArduinoJson.h>

// The link to Knob Hub. Messages are JSON objects, one per line, with "t" naming the message.
//
// Knob Hub -> knob:
//   apps    {items: [{id, name, subtitle, icon, color}],      the apps on the Home ring, and the ring's order (app ids,
//            order: [...]}                                   "clock" and "settings"); sent when it connects, kept
//                                                             across restarts
//   screen  {app, screen, template, data}                     show a screen of the open app (see templates.h)
//   update  {app, screen, data}                               change parts of the screen on show
//   close   {app}                                             the app is done: back to Home
//   toast   {text}                                            a short message over anything
//   icon    {name, size, w, h, hash, a8}                      an icon Knob OS asked for (see icons.h)
//   icons   {known: {name: hash}}                             the icons Knob Hub has, when it connects (see icons.h)
//   settings{brightness, led_brightness, led_mode, timeout, dim_idle, leds_idle_off, clock_24h, vibration, haptics,
//            name}                                           after hello and on change; any subset
//   key     {input}                                           a knob action done in Knob Hub's preview of the knob
//   touch   {x, y, down}                                      a touch in Knob Hub's preview (see knob_os_touch)
//   sync    {page, app}                                       only to the preview: catch up with the real knob
//   idle    {state, follow}                                   only to the preview: follow the real knob's screen
//                                                             state (see idle.h), or with follow: false time its own
// Knob -> Knob Hub:
//   hello   {os, name, page, app, board, size, touch, leds,   sent when the link comes up; page = the Home page on
//            vibration, motor}                                show ("clock", an app id, "settings"), app = the open
//                                                             app; the rest describe the board (see board.h). A knob
//                                                             without them (Knob OS 0.1) is a CrowPanel 1.28"
//   open    {app}                                             an app was opened from Home
//   input   {app, screen, kind, item, delta}                  something done on the app's screen
//   settings{...}                                             all settings, after any change on the knob
//   icon_request {name, size}                                 an icon a screen shows (see icons.h)
//   key     {input: "right" | "left" | "press" | "back"}      every knob action, before it is handled, so Knob Hub
//                                                             can replay it on its preview of the knob
//   touch   {x, y, down}                                      the same for touches (on the knob, or from Knob Hub's
//                                                             preview)
//   idle    {state: "awake" | "dim" | "off"}                  the screen's state (see idle.h): when it changes and
//                                                             when the link comes up. An input that only wakes the
//                                                             screen isn't sent as a key
namespace hub_link {

bool connected();
void set_connected(bool connected);
void receive(const char* line);
void send(const JsonDocument& doc);

}  // namespace hub_link
