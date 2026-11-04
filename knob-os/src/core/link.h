#pragma once
#include <ArduinoJson.h>

// The link to Knob Hub. Messages are JSON objects, one per line, with "t" naming the message.
//
// Over USB the knob is a USB serial port (the ESP32-S3's own USB). It can't see Knob Hub open the port, so Knob Hub
// sends {"t":"hub", session, time, offset} every 2 seconds and the link is up while they come (handled by the
// hardware layer, src/hal/esp32/main.cpp, which also sets the clock from them): the knob then says hello, and Knob
// Hub only counts it as connected once it has. Anything else the knob prints before (boot messages) is skipped, so
// log lines must never look like a hello.
//
// Over USB, Knob Hub also pairs the knob: {"t":"pair", key} gives it a random key (base64, 32 bytes), kept on the
// knob and taken only over USB. Over Wi-Fi the knob is a TCP server on port 5719 (knob-xxxx.local), and Knob Hub
// connects to it. Both prove they hold the key before anything else (HMAC-SHA256 over fresh nonces):
//   Knob Hub -> knob: {"t":"auth", nonce}           knob -> Knob Hub: {"t":"auth", nonce, proof} ({"t":"auth", error}
//   Knob Hub -> knob: {"t":"auth", proof}           when it isn't paired)
// After that every line, both ways, is sealed with AES-256-GCM under a key for that connection. The "pair", "hub" and
// "auth" messages are the hardware layer's (src/hal/esp32/net.cpp); Knob OS sees the rest.
//
// Knob Hub -> knob:
//   apps    {items: [{id, name, subtitle, icon, color}],      the apps on the Home ring, and the ring's order (app ids,
//            order: [...]}                                   "clock" and "settings"); sent when it connects, kept
//                                                             across restarts
//   screen  {app, screen, template, data}                     show a screen of the open app (see templates.h)
//   update  {app, screen, data}                               change parts of the screen on show (in either, data
//                                                             can say busy: see busy.h)
//   close   {app}                                             the app is done: back to Home
//   toast   {text}                                            a short message over anything
//   awake   {on}                                              keep the screen on (an app asked, e.g. a timer that's
//                                                             running): no timeout, no dimming (see idle.h)
//   icon    {name, size, w, h, hash, a8}                      an icon Knob OS asked for (see icons.h)
//   icons   {known: {name: hash}}                             the icons Knob Hub has, when it connects (see icons.h)
//   settings{brightness, led_brightness, led_mode, timeout, dim_idle, leds_idle_off, clock_24h, vibration, haptics,
//            name}                                           after hello and on change; any subset
//   key     {input}                                           a knob action done in Knob Hub's preview of the knob
//   touch   {x, y, down}                                      a touch in Knob Hub's preview (see knob_os_touch)
//   sync    {page, app}                                       only to the preview: catch up with the real knob
//   idle    {state, follow}                                   only to the preview: follow the real knob's screen
//                                                             state (see idle.h), or with follow: false time its own
//   wifi    {action: "setup" | "stop" | "forget" | "scan" |   Knob Hub's Wi-Fi window and buttons: pairing mode on or
//            "join", ssid, password | "done"}                 off, forget the network, look for networks (answered
//                                                             with wifi_networks; the knob shows its Wi-Fi screen
//                                                             while the window is open), join one now, or the window
//                                                             closed
// Knob -> Knob Hub:
//   hello   {os, name, page, app, board, size, touch, leds,   sent when the link comes up; page = the Home page on
//            vibration, motor, id, host, key_id, wifi}        show ("clock", an app id, "settings"), app = the open
//                                                             app; board to motor describe the board (see board.h).
//                                                             A knob without them (Knob OS 0.1) is a CrowPanel 1.28".
//                                                             id, host and key_id are the knob's (hal_identity; none
//                                                             in the simulator and the preview), wifi as below
//   wifi    {state, network, setup_network, setup_password,   the Wi-Fi state: "unsupported", "off", "connecting",
//            joining, join_failed, join_problem, scanning}    "connected", "failed", "setup" (with the knob's own
//                                                             network and its password); a network being joined, or
//                                                             the last that couldn't be and why (see hal.h); when
//                                                             any of it changes. Knob Hub passes it on to its preview
//                                                             as mirror_wifi (see src/hal/preview/preview.cpp)
//   wifi_networks {networks: [{ssid, rssi, open}]}            the networks the knob can see, strongest first, after a
//                                                             wifi scan
//   wifi_left {}                                              something was done on the knob's Wi-Fi screen (turn,
//                                                             press, back, tap), which leaves it: Knob Hub closes its
//                                                             Wi-Fi window
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
