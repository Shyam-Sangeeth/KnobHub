#pragma once
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

#include "board.h"
#include "haptics.h"

// What Knob OS needs from the hardware layer (src/hal/<target>/). Everything else in src/core is portable.

// The board this is: its screen size and what it has (see board.h). The display the hardware layer creates is
// Board::size square.
const Board& hal_board();

// Local time for the clock. Returns false until the time is known (the knob gets it from Knob Hub or Wi-Fi).
bool hal_local_time(std::tm& out);

// Sends one message to Knob Hub (a JSON object; the hardware layer adds the newline). Dropped if not connected.
void hal_link_send(const char* line);

// How the link to Knob Hub runs now: "USB", "Wi-Fi", or "" (not connected, or the simulator and the preview).
const char* hal_link_kind();

// Who the knob is, for its hello: a lasting id (the chip's MAC), its name on the network ("knob-b2a4", reached as
// knob-b2a4.local) and a fingerprint of the key it was paired with over USB ("" until it has been). All empty in the
// simulator and the preview.
struct KnobIdentity {
    std::string id, host, key_id;
};
KnobIdentity hal_identity();

// The knob's name (Settings > About), when it's loaded and whenever it changes: on Wi-Fi it's the name routers list
// the knob by. (Knob Hub still finds it by KnobIdentity::host, which never changes.)
void hal_set_name(const std::string& name);

// Wi-Fi. The simulator and the preview have none (Unsupported). A network is chosen either in Knob Hub (which has the
// knob look for networks and then join one) or in Setup, pairing mode: for a few minutes the knob opens its own
// network (setup_network, setup_password) with a setup page where you choose yours. The hardware layer calls
// knob_os_wifi_changed() from its main loop (never from inside these calls) whenever any of it changes.
enum class WifiState { Unsupported, Off, Connecting, Connected, Failed, Setup };
struct WifiStatus {
    WifiState state = WifiState::Unsupported;
    std::string network;                        // the network it's set up for (Connecting, Connected, Failed)
    std::string setup_network, setup_password;  // its own network, in Setup
    std::string joining;                        // a network chosen in Knob Hub or on the setup page, while it joins
    std::string join_failed, join_problem;      // the last one it couldn't join, and why ("password", "not_found",
                                                // "security", "timeout", "other")
    int join_reason = 0;                        // ... and the Wi-Fi driver's own reason (WIFI_REASON_*), for the log
    bool scanning = false;                      // looking for networks (hal_wifi_scan)
};
WifiStatus hal_wifi();
// Starts pairing mode; false, with why (for a toast), if it can't.
bool hal_wifi_setup(std::string& why);
void hal_wifi_stop_setup();
void hal_wifi_forget();
// Looks for networks; once hal_wifi().scanning is false again, hal_wifi_networks() has them, strongest first.
struct WifiNetwork {
    std::string ssid;
    int rssi = 0;  // dBm
    bool open = false;
};
void hal_wifi_scan();
std::vector<WifiNetwork> hal_wifi_networks();
// Joins a network now (joining, then Connected or join_failed). It's kept once joined; if it can't be, the saved one
// stays.
void hal_wifi_join(const std::string& ssid, const std::string& password);

// Screen backlight, 0-100.
void hal_set_brightness(int percent);

// Small files kept across restarts (what Knob Hub sent last: the app list and icons), read and written whole by name.
// hal_store_read returns false when there's no such file.
bool hal_store_read(const char* name, std::vector<uint8_t>& out);
void hal_store_write(const char* name, const std::vector<uint8_t>& data);

// Boards with a motor on the knob (Board::motor): how turning should feel now (see haptics.h), at strength percent.
void hal_motor_feel(const haptics::Feel& feel, int strength);

// Boards with a vibration motor (Board::vibration): plays an effect at strength percent.
void hal_vibrate(haptics::Effect effect, int strength);

// Boards with LEDs (Board::leds): each LED's colour, 0xRRGGBB with the brightness applied, in order round the ring
// (see lights.h), and where the screen's arc for them is (Glow::arc_start, arc_sweep: for a hardware layer that draws
// them, like the preview's). Called when they change.
void hal_set_leds(const uint32_t* colors, int count, int arc_start, int arc_sweep);
