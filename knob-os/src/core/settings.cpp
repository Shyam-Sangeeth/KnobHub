// The Settings app: groups (Display, Lights, Haptics, Connection, About; Lights and Haptics only on boards with LEDs
// or haptics), each a list page, plus dials for brightness and the Wi-Fi setup screen.
// Pages are built with the same templates as Knob Hub apps, fed from here instead of from the PC.
#include "settings.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "apps.h"
#include "haptics.h"
#include "hal.h"
#include "home.h"
#include "idle.h"
#include "knob_os.h"
#include "link.h"
#include "message.h"
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
const char* const StrengthNames[] = {"Light", "Medium", "Strong"};
constexpr int StrengthCount = 3;
constexpr uint32_t Brightness = 0xF2C14E;

enum class Page { None, Categories, Display, Lights, Haptics, Connection, About, Dial };
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
    d["vibration"] = values.vibration;
    d["haptics"] = values.haptics;
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
    if (msg["vibration"].is<bool>()) values.vibration = msg["vibration"];
    if (msg["haptics"].is<int>()) values.haptics = std::min(StrengthCount - 1, std::max(0, msg["haptics"].as<int>()));
    if (msg["name"].is<const char*>()) values.name = msg["name"].as<const char*>();
}

void changed() {
    idle::refresh();
    haptics::reapply();
    save();
    send_all();
}

std::string link_text() {
    if (!hub_link::connected()) return "Knob Hub not connected";
    std::string kind = hal_link_kind();
    return kind.empty() ? "Connected to Knob Hub" : "Knob Hub, over " + kind;
}

const char* wifi_state_name(WifiState s) {
    switch (s) {
        case WifiState::Off: return "off";
        case WifiState::Connecting: return "connecting";
        case WifiState::Connected: return "connected";
        case WifiState::Failed: return "failed";
        case WifiState::Setup: return "setup";
        default: return "unsupported";
    }
}

std::string wifi_text(const WifiStatus& w) {
    switch (w.state) {
        case WifiState::Off: return "Not set up";
        case WifiState::Connecting: return "Connecting";
        case WifiState::Connected: return w.network;
        case WifiState::Failed: return "Can't connect";
        case WifiState::Setup: return "Setting up";
        default: return "Not on this knob";
    }
}

// --- Wi-Fi setup: a message with the knob's own network and password, until the hardware layer is done ---

Screen* setup_screen = nullptr;
WifiState shown_wifi = WifiState::Unsupported;  // what Knob Hub was last told
std::string shown_network, shown_password;
std::string shown_joining, shown_failed;  // what the toasts last said
std::string shown_joining_sent, shown_failed_sent;
bool shown_scanning = false;
bool scan_for_hub = false;  // Knob Hub asked for the networks: sent once the scan is done

void show_setup(const WifiStatus& w) {
    std::string text = "Join " + w.setup_network + " (password " + w.setup_password +
                       ") on your phone or PC, then open knobhub.local";
    setup_screen = new MessageScreen("wifi", "Wi-Fi setup", text, "Press to stop", [] {
        setup_screen = nullptr;
        hal_wifi_stop_setup();
        screens::pop();
    });
    screens::push(setup_screen);
}

void start_wifi_setup() {
    if (hal_wifi().state == WifiState::Unsupported) {
        toast_show("Wi-Fi setup needs the real knob");
        return;
    }
    std::string why;
    if (!hal_wifi_setup(why)) toast_show(why.c_str());
    // The setup screen comes up with the hardware layer's change (wifi_changed).
}

void show(Page p, bool push);

// Settings > Connection, from wherever the knob is: Home on the Settings page underneath, so going back works as
// usual. An open app is closed (Knob Hub closes its side too), and pairing mode stops.
void show_connection() {
    if (page == Page::Connection) return;
    if (setup_screen) {
        setup_screen = nullptr;
        hal_wifi_stop_setup();
    }
    if (!apps::open_id().empty()) apps::close();
    screens::home();
    page = Page::None;
    page_screen = nullptr;
    home_show("settings");
    show(Page::Categories, true);
    show(Page::Connection, true);
}

void forget_wifi() {
    if (hal_wifi().state == WifiState::Unsupported) {
        toast_show("Wi-Fi setup needs the real knob");
        return;
    }
    hal_wifi_forget();
    toast_show("Wi-Fi forgotten");
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
    const Board& board = hal_board();
    if (board.leds > 0) {
        JsonObject lights = items.add<JsonObject>();
        lights["id"] = "lights";
        lights["label"] = "Lights";
        lights["icon"] = "lights";
        std::snprintf(detail, sizeof detail, "%d%%, %s", values.led_brightness, LedModeNames[values.led_mode]);
        lights["detail"] = detail;
    }
    if (board.vibration || board.motor) {
        JsonObject feel = items.add<JsonObject>();
        feel["id"] = "haptics";
        feel["label"] = "Haptics";
        feel["icon"] = "haptics";
        std::snprintf(detail, sizeof detail, board.vibration ? "%s, vibration %s" : "%s", StrengthNames[values.haptics],
                      values.vibration ? "on" : "off");
        feel["detail"] = detail;
    }
    JsonObject connection = items.add<JsonObject>();
    connection["id"] = "connection";
    connection["label"] = "Connection";
    connection["icon"] = std::strcmp(hal_link_kind(), "Wi-Fi") == 0 ? "wifi" : "usb";
    connection["detail"] = link_text();
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
            add_row(items, "leds_idle", "When dimmed", "lights", values.leds_idle_off ? "Off" : "On");
            break;
        case Page::Haptics:
            d["title"] = "HAPTICS";
            add_row(items, "strength", "Strength", "haptics", StrengthNames[values.haptics]);
            if (hal_board().vibration) add_row(items, "vibration", "Vibration", "haptics", values.vibration ? "On" : "Off");
            break;
        case Page::Connection: {
            d["title"] = "CONNECTION";
            d["note"] = link_text();
            WifiStatus wifi = hal_wifi();
            add_row(items, "wifi", "Wi-Fi", "wifi", wifi_text(wifi).c_str());
            bool paired = !hal_identity().key_id.empty();
            add_row(items, "pairing", "Wi-Fi setup", "wifi", nullptr,
                    paired || wifi.state == WifiState::Unsupported ? "Pairing mode, 5 min" : "Pair over USB first");
            add_row(items, "forget", "Forget Wi-Fi", "trash", nullptr, nullptr, true);
            break;
        }
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
        else if (id == "haptics") show(Page::Haptics, true);
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
    else if (id == "strength") values.haptics = (values.haptics + 1) % StrengthCount;
    else if (id == "vibration") values.vibration = !values.vibration;
    else if (id == "pairing") return start_wifi_setup();
    else if (id == "forget") return forget_wifi();
    else if (id == "wifi") return;  // shows the state only
    else if (id == "restart") {
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
    JsonDocument doc;
    if (hal_store_read(SettingsFile, data) && !deserializeJson(doc, data.data(), data.size()))
        apply_values(doc.as<JsonObjectConst>());
    hal_set_name(values.name);
}

KnobSettings& get() { return values; }

void open() { show(Page::Categories, true); }

void refresh() {
    if (page != Page::None) redraw_later();
}

void apply_from_hub(JsonObjectConst msg) {
    int timeout = values.timeout;
    bool dim_idle = values.dim_idle;
    apply_values(msg);
    hal_set_name(values.name);
    save();
    // A new timeout counts from now, not from the last time the knob was used (it turned the screen off at once).
    if (values.timeout != timeout || values.dim_idle != dim_idle) idle::wake();
    idle::refresh();
    haptics::reapply();
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
    d["vibration"] = values.vibration;
    d["haptics"] = values.haptics;
    d["name"] = values.name;
    hub_link::send(d);
}

void describe_wifi(JsonObject out) {
    WifiStatus w = hal_wifi();
    out["state"] = wifi_state_name(w.state);
    if (!w.network.empty()) out["network"] = w.network;
    if (w.state == WifiState::Setup) {
        out["setup_network"] = w.setup_network;
        out["setup_password"] = w.setup_password;
    }
    if (!w.joining.empty()) out["joining"] = w.joining;
    if (!w.join_failed.empty()) {
        out["join_failed"] = w.join_failed;
        out["join_problem"] = w.join_problem;
        if (w.join_reason) out["join_reason"] = w.join_reason;
    }
    if (w.scanning) out["scanning"] = true;
}

// Why a network chosen in Knob Hub or on the setup page couldn't be joined, for a toast.
std::string join_problem_text(const WifiStatus& w) {
    const std::string& p = w.join_problem;
    if (p == "password") return w.join_failed + " turned the password down";
    if (p == "not_found") return "Can't see " + w.join_failed + " (2.4 GHz only)";
    if (p == "security") return w.join_failed + " needs WPA2 or WPA3";
    if (p == "timeout") return w.join_failed + " didn't answer";
    return "Couldn't join " + w.join_failed;
}

// --- Knob Hub's Wi-Fi window: while it's open, the knob shows what it's doing on a screen over its Connection page ---

namespace {

Screen* pc_screen = nullptr;
std::string pc_join;  // the network Knob Hub asked to join, once the knob has started on it
std::string pc_text_shown;
// Once the knob has joined, the screen goes by itself after a moment, and Knob Hub closes its window too.
constexpr uint32_t LeaveAfterJoinMs = 5000;
lv_timer_t* leave_after = nullptr;

// The knob joined the network Knob Hub asked for.
bool pc_joined(const WifiStatus& w) {
    return w.joining.empty() && !pc_join.empty() && w.join_failed != pc_join && w.state == WifiState::Connected &&
           w.network == pc_join;
}

std::string pc_text(const WifiStatus& w) {
    if (!w.joining.empty()) return "Joining " + w.joining + "...";
    if (!pc_join.empty() && w.join_failed == pc_join) return join_problem_text(w) + ". Try again in Knob Hub.";
    // The network's name on a line of its own: a name like "Home_2.4" would otherwise break at its dot.
    if (pc_joined(w)) return "Connected to\n" + pc_join;
    if (w.scanning) return "Looking for networks...";
    return "Choose a network in Knob Hub on your PC.";
}

void stop_leave_after() {
    if (!leave_after) return;
    lv_timer_delete(leave_after);
    leave_after = nullptr;
}

void close_pc_screen() {
    stop_leave_after();
    if (!pc_screen) return;
    if (screens::top() == pc_screen) screens::pop();
    pc_screen = nullptr;
    pc_join.clear();
}

// Left on the knob: back to the Connection page, and Knob Hub closes its window.
void leave_pc_screen() {
    if (!pc_screen) return;
    close_pc_screen();
    JsonDocument d;
    d["t"] = "wifi_left";
    hub_link::send(d);
}

// Anything done on the knob leaves the screen: turning, pressing, going back (a long press or a swipe) or a tap.
class PcWifiScreen : public MessageScreen {
public:
    explicit PcWifiScreen(const std::string& text) : MessageScreen("wifi", "Wi-Fi", text, "Press to go back") {
        lv_obj_add_flag(root(), LV_OBJ_FLAG_CLICKABLE);
        // Once LVGL has finished with the tap: leaving deletes this screen.
        lv_obj_add_event_cb(root(), [](lv_event_t*) { lv_async_call([](void*) { leave_pc_screen(); }, nullptr); },
                            LV_EVENT_CLICKED, nullptr);
    }
    void input(KnobInput) override { leave_pc_screen(); }
};

// Shows the screen, or redraws it with what's happening now.
void show_pc_screen() {
    WifiStatus w = hal_wifi();
    pc_text_shown = pc_text(w);
    Screen* screen = new PcWifiScreen(pc_text_shown);
    if (pc_screen && screens::top() == pc_screen) screens::replace(screen);
    else screens::push(screen);
    pc_screen = screen;
    // Joined: back to the Connection page in a few seconds, unless the knob is used first (which leaves at once).
    if (!pc_joined(w)) stop_leave_after();
    else if (!leave_after) {
        leave_after = lv_timer_create([](lv_timer_t*) {
            leave_after = nullptr;  // a one-off: LVGL deletes it after this
            leave_pc_screen();
        }, LeaveAfterJoinMs, nullptr);
        lv_timer_set_repeat_count(leave_after, 1);
    }
}

}  // namespace

void wifi_changed() {
    WifiStatus w = hal_wifi();
    if (pc_screen && !w.joining.empty()) pc_join = w.joining;
    // Otherwise toasts say how a join went: the phone or PC on the setup page usually drops off the knob's network
    // while it joins (one radio, one channel).
    if (!pc_screen) {
        if (w.joining != shown_joining && !w.joining.empty()) toast_show(("Joining " + w.joining + "...").c_str());
        if (w.join_failed != shown_failed && !w.join_failed.empty()) toast_show(join_problem_text(w).c_str());
    }
    shown_joining = w.joining;
    shown_failed = w.join_failed;
    if (pc_screen && screens::top() == pc_screen && pc_text(w) != pc_text_shown) show_pc_screen();
    if (scan_for_hub && !w.scanning) {
        scan_for_hub = false;
        JsonDocument d;
        d["t"] = "wifi_networks";
        JsonArray list = d["networks"].to<JsonArray>();
        for (const WifiNetwork& n : hal_wifi_networks()) {
            JsonObject item = list.add<JsonObject>();
            item["ssid"] = n.ssid;
            item["rssi"] = n.rssi;
            item["open"] = n.open;
        }
        hub_link::send(d);
    }
    if (w.state == WifiState::Setup && !setup_screen) {
        show_setup(w);
    } else if (w.state != WifiState::Setup && setup_screen) {
        // Setup ended by itself (joined a network, or its time ran out), not by pressing.
        if (screens::top() == setup_screen) screens::pop();
        setup_screen = nullptr;
        toast_show(w.state == WifiState::Connected ? ("Connected to " + w.network).c_str() : "Wi-Fi setup ended");
    }
    if (w.state != shown_wifi || w.network != shown_network || w.setup_password != shown_password ||
        w.joining != shown_joining_sent || w.join_failed != shown_failed_sent || w.scanning != shown_scanning) {
        shown_joining_sent = w.joining;
        shown_failed_sent = w.join_failed;
        shown_scanning = w.scanning;
        shown_wifi = w.state;
        shown_network = w.network;
        shown_password = w.setup_password;
        JsonDocument d;
        JsonObject msg = d.to<JsonObject>();
        msg["t"] = "wifi";
        describe_wifi(msg);
        hub_link::send(d);
    }
    refresh();
}

void wifi_action(JsonObjectConst msg) {
    const char* action = msg["action"] | "";
    if (std::strcmp(action, "setup") == 0) {
        start_wifi_setup();
    } else if (std::strcmp(action, "stop") == 0) {
        hal_wifi_stop_setup();
    } else if (std::strcmp(action, "forget") == 0) {
        forget_wifi();
    } else if (std::strcmp(action, "scan") == 0) {
        // Knob Hub's Wi-Fi window opened (or looks again): the knob goes to its Connection page and shows what it's
        // doing over it, then looks for networks.
        if (!pc_screen) {
            show_connection();
            show_pc_screen();
        }
        scan_for_hub = true;
        hal_wifi_scan();
    } else if (std::strcmp(action, "join") == 0) {
        hal_wifi_join(msg["ssid"] | "", msg["password"] | "");
    } else if (std::strcmp(action, "done") == 0) {
        close_pc_screen();  // Knob Hub's Wi-Fi window closed: back to the Connection page
    }
}

bool keeps_screen_awake() {
    Screen* shown = screens::top();
    if (!shown) return false;
    return (page == Page::Connection && shown == page_screen) || (pc_screen && shown == pc_screen) ||
           (setup_screen && shown == setup_screen);
}

}  // namespace settings
