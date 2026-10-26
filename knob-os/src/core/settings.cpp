// The Settings app: four groups (Display, Lights, Connection, About), each a list page, plus dials for brightness.
// Pages are built with the same templates as Knob Hub apps, fed from here instead of from the PC.
#include "settings.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "hal.h"
#include "knob_os.h"
#include "link.h"
#include "screens.h"
#include "templates.h"
#include "theme.h"
#include "toast.h"

namespace settings {
namespace {

constexpr const char* SettingsFile = "settings.json";
KnobSettings values;

const char* const TimeoutNames[] = {"15 s", "30 s", "1 min", "5 min", "Never"};
constexpr int TimeoutCount = 5;
const char* const LedModeNames[] = {"Level meter", "App colour", "Off"};
constexpr int LedModeCount = 3;
constexpr uint32_t Brightness = 0xF2C14E;

enum class Page { None, Categories, Display, Lights, Connection, About, Dial };
Page page = Page::None;           // the Settings page on top, if any
TemplateScreen* page_screen = nullptr;
Page dial_parent = Page::None;    // the page a brightness dial was opened from
bool dial_is_leds = false;

void save() {
    JsonDocument d;
    d["brightness"] = values.brightness;
    d["led_brightness"] = values.led_brightness;
    d["led_mode"] = values.led_mode;
    d["timeout"] = values.timeout;
    d["dim_idle"] = values.dim_idle;
    d["leds_idle_off"] = values.leds_idle_off;
    d["clock_24h"] = values.clock_24h;
    d["name"] = values.name;
    std::string s;
    serializeJson(d, s);
    hal_store_write(SettingsFile, std::vector<uint8_t>(s.begin(), s.end()));
}

void apply_values(JsonObjectConst msg) {
    if (msg["brightness"].is<int>()) values.brightness = std::min(100, std::max(5, msg["brightness"].as<int>()));
    if (msg["led_brightness"].is<int>()) values.led_brightness = std::min(100, std::max(0, msg["led_brightness"].as<int>()));
    if (msg["led_mode"].is<int>()) values.led_mode = std::min(LedModeCount - 1, std::max(0, msg["led_mode"].as<int>()));
    if (msg["timeout"].is<int>()) values.timeout = std::min(TimeoutCount - 1, std::max(0, msg["timeout"].as<int>()));
    if (msg["dim_idle"].is<bool>()) values.dim_idle = msg["dim_idle"];
    if (msg["leds_idle_off"].is<bool>()) values.leds_idle_off = msg["leds_idle_off"];
    if (msg["clock_24h"].is<bool>()) values.clock_24h = msg["clock_24h"];
    if (msg["name"].is<const char*>()) values.name = msg["name"].as<const char*>();
}

void changed() {
    hal_set_brightness(values.brightness);
    save();
    send_all();
}

void fill_categories(JsonDocument& d) {
    d["title"] = "SETTINGS";
    d["color"] = "#9AA0AA";
    JsonArray items = d["items"].to<JsonArray>();
    char detail[48];
    JsonObject display = items.add<JsonObject>();
    display["id"] = "display";
    display["label"] = "Display";
    display["icon"] = "sun";
    std::snprintf(detail, sizeof detail, "%d%%, %s", values.brightness, TimeoutNames[values.timeout]);
    display["detail"] = detail;
    JsonObject lights = items.add<JsonObject>();
    lights["id"] = "lights";
    lights["label"] = "Lights";
    lights["icon"] = "lights";
    std::snprintf(detail, sizeof detail, "%d%%, %s", values.led_brightness, LedModeNames[values.led_mode]);
    lights["detail"] = detail;
    JsonObject connection = items.add<JsonObject>();
    connection["id"] = "connection";
    connection["label"] = "Connection";
    connection["icon"] = hub_link::connected() ? "usb" : "wifi";
    connection["detail"] = hub_link::connected() ? "Connected to Knob Hub" : "Knob Hub not connected";
    JsonObject about = items.add<JsonObject>();
    about["id"] = "about";
    about["label"] = "About";
    about["icon"] = "about";
    about["detail"] = "Knob OS " KNOB_OS_VERSION;
}

void add_row(JsonArray items, const char* id, const char* label, const char* icon, const char* value,
             const char* detail = nullptr, bool danger = false) {
    JsonObject row = items.add<JsonObject>();
    row["id"] = id;
    row["label"] = label;
    row["icon"] = icon;
    // Copied: ArduinoJson keeps only a pointer to a const char*, and a value is often a buffer that's about to go.
    if (value) row["value"] = std::string(value);
    if (detail) row["detail"] = std::string(detail);
    if (danger) row["danger"] = true;
}

void fill_page(Page p, JsonDocument& d) {
    d["color"] = "#9AA0AA";
    JsonArray items = d["items"].to<JsonArray>();
    char value[32];
    switch (p) {
        case Page::Display:
            d["title"] = "DISPLAY";
            std::snprintf(value, sizeof value, "%d%%", values.brightness);
            add_row(items, "brightness", "Brightness", "sun", value);
            add_row(items, "timeout", "Screen timeout", "clock", TimeoutNames[values.timeout]);
            add_row(items, "dim", "Dim when idle", "sun", values.dim_idle ? "On" : "Off");
            add_row(items, "clock", "Clock", "clock", values.clock_24h ? "24-hour" : "12-hour");
            break;
        case Page::Lights:
            d["title"] = "LIGHTS";
            std::snprintf(value, sizeof value, "%d%%", values.led_brightness);
            add_row(items, "led_brightness", "LED brightness", "lights", value);
            add_row(items, "led_mode", "LED mode", "lights", LedModeNames[values.led_mode]);
            add_row(items, "leds_idle", "LEDs when idle", "lights", values.leds_idle_off ? "Off" : "On");
            break;
        case Page::Connection:
            d["title"] = "CONNECTION";
            d["note"] = hub_link::connected() ? "Connected to Knob Hub" : "Knob Hub not connected";
            add_row(items, "wifi", "Wi-Fi", "wifi", "Not set up");
            add_row(items, "pairing", "Wi-Fi setup", "wifi", nullptr, "Pairing mode, 5 min");
            add_row(items, "forget", "Forget Wi-Fi", "trash", nullptr, nullptr, true);
            break;
        case Page::About:
            d["title"] = "ABOUT";
            add_row(items, "name", "Name", "about", values.name.c_str());
            add_row(items, "firmware", "Firmware", "about", KNOB_OS_VERSION);
            add_row(items, "restart", "Restart", "restart", nullptr, "Restarts the knob");
            break;
        default: break;
    }
}

void fill_dial(JsonDocument& d) {
    d["label"] = dial_is_leds ? "LED BRIGHTNESS" : "BRIGHTNESS";
    d["icon"] = dial_is_leds ? "lights" : "sun";
    d["color"] = "#F2C14E";
    d["value"] = dial_is_leds ? values.led_brightness : values.brightness;
    d["button"] = false;
}

void show(Page p, bool push);

// Rebuilding a page from inside one of its own row clicks would delete the row being clicked, so redraws wait for
// LVGL to finish the event.
void redraw_later() {
    lv_async_call([](void*) {
        if (!page_screen || page == Page::Dial) return;
        JsonDocument d;
        if (page == Page::Categories) fill_categories(d);
        else fill_page(page, d);
        page_screen->update(d.as<JsonObjectConst>());
    }, nullptr);
}

void go_back() {
    Page parent = page == Page::Dial ? dial_parent : page == Page::Categories ? Page::None : Page::Categories;
    screens::pop();
    page = parent;
    page_screen = parent == Page::None ? nullptr : static_cast<TemplateScreen*>(screens::top());
    if (parent != Page::None) redraw_later();
}

void on_dial(const TemplateEvent& ev) {
    if (std::strcmp(ev.kind, "rotate") == 0) {
        int& v = dial_is_leds ? values.led_brightness : values.brightness;
        v = std::min(100, std::max(dial_is_leds ? 0 : 5, v + ev.delta * 2));
        changed();
    } else if (std::strcmp(ev.kind, "back") == 0) {
        go_back();
    }
}

void open_dial(bool leds) {
    dial_parent = page;
    dial_is_leds = leds;
    page = Page::Dial;
    JsonDocument d;
    fill_dial(d);
    page_screen = make_template("dial", d.as<JsonObjectConst>(), on_dial);
    screens::push(page_screen);
}

void on_row(const TemplateEvent& ev) {
    if (std::strcmp(ev.kind, "back") == 0) {
        go_back();
        return;
    }
    if (std::strcmp(ev.kind, "press") != 0) return;
    const std::string& id = ev.item;
    if (page == Page::Categories) {
        if (id == "display") show(Page::Display, true);
        else if (id == "lights") show(Page::Lights, true);
        else if (id == "connection") show(Page::Connection, true);
        else if (id == "about") show(Page::About, true);
        return;
    }
    if (id == "brightness") return open_dial(false);
    if (id == "led_brightness") return open_dial(true);
    if (id == "timeout") values.timeout = (values.timeout + 1) % TimeoutCount;
    else if (id == "dim") values.dim_idle = !values.dim_idle;
    else if (id == "clock") values.clock_24h = !values.clock_24h;
    else if (id == "led_mode") values.led_mode = (values.led_mode + 1) % LedModeCount;
    else if (id == "leds_idle") values.leds_idle_off = !values.leds_idle_off;
    else if (id == "pairing" || id == "forget" || id == "wifi") {
        toast_show(hub_link::connected() ? "Wi-Fi setup needs the real knob" : "Pair over USB first");
        return;
    } else if (id == "restart") {
        toast_show("Restart needs the real knob");
        return;
    } else {
        return;
    }
    changed();
    redraw_later();
}

void show(Page p, bool push) {
    JsonDocument d;
    if (p == Page::Categories) fill_categories(d);
    else fill_page(p, d);
    TemplateScreen* screen = make_template("list", d.as<JsonObjectConst>(), on_row);
    page = p;
    page_screen = screen;
    if (push) screens::push(screen);
    else screens::replace(screen);
}

}  // namespace

void load() {
    std::vector<uint8_t> data;
    if (!hal_store_read(SettingsFile, data)) return;
    JsonDocument doc;
    if (deserializeJson(doc, data.data(), data.size())) return;
    apply_values(doc.as<JsonObjectConst>());
    hal_set_brightness(values.brightness);
}

KnobSettings& get() { return values; }

void open() { show(Page::Categories, true); }

void refresh() {
    if (page != Page::None) redraw_later();
}

void apply_from_hub(JsonObjectConst msg) {
    apply_values(msg);
    save();
    hal_set_brightness(values.brightness);
    if (page == Page::Dial && page_screen) {
        JsonDocument d;
        fill_dial(d);
        page_screen->update(d.as<JsonObjectConst>());
    } else {
        refresh();
    }
}

void forget_pages() {
    page = Page::None;
    page_screen = nullptr;
}

void send_all() {
    JsonDocument d;
    d["t"] = "settings";
    d["brightness"] = values.brightness;
    d["led_brightness"] = values.led_brightness;
    d["led_mode"] = values.led_mode;
    d["timeout"] = values.timeout;
    d["dim_idle"] = values.dim_idle;
    d["leds_idle_off"] = values.leds_idle_off;
    d["clock_24h"] = values.clock_24h;
    d["name"] = values.name;
    hub_link::send(d);
}

}  // namespace settings
