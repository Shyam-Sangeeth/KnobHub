#pragma once
#include <ArduinoJson.h>

#include <cstdint>
#include <string>
#include <vector>

// Apps that run on the PC as Knob Hub apps (plugins). Knob OS knows nothing about any of them: Knob Hub sends the list,
// Knob OS shows their screens from its templates and reports what's done on them, and the app does the work. Clock and
// Settings are Knob OS's own and aren't in this list.
struct AppInfo {
    std::string id, name, subtitle, icon;
    uint32_t color = 0;
};

namespace apps {

// The apps on the Home ring, in order. None until Knob Hub sends its list.
const std::vector<AppInfo>& list();
// The whole Home ring's order as Knob Hub sets it: app ids plus "clock" and "settings". Empty until it's sent: Clock
// first, then the apps, then Settings.
const std::vector<std::string>& order();
void set_list(JsonArrayConst items, JsonArrayConst order);  // from Knob Hub: applied and saved if it changed
void load();                                                // at start: the list as last saved

// Opened from Home: asks Knob Hub for the app's first screen (or explains that Knob Hub isn't connected).
void open(const AppInfo& app);

void show_screen(JsonObjectConst msg);
void update_screen(JsonObjectConst msg);
void close(const std::string& app_id = "");  // back to Home
void on_connected(bool connected);

// The app on screen ("" on Home or in Settings).
const std::string& open_id();

// Knob Hub's preview catching up with the real knob: shows app_id as being opened, without asking Knob Hub (the
// real knob already did), or nothing. Call with only Home on screen.
void sync(const std::string& app_id);

}  // namespace apps
