#include "apps.h"

#include <cstring>

#include "hal.h"
#include "home.h"
#include "link.h"
#include "message.h"
#include "screens.h"
#include "templates.h"
#include "theme.h"
#include "toast.h"
#include "ui.h"

namespace apps {
namespace {

std::vector<AppInfo> apps_list;        // from Knob Hub

std::vector<std::string> ring_order;   // the Home ring's order, from Knob Hub
std::string saved_list;                // the list as saved (JSON), to tell whether a new one changes anything
constexpr const char* ListFile = "apps.json";
std::string open_app;                  // the app on screen, "" on Home
TemplateScreen* current = nullptr;     // its screen, while one is shown
std::string current_screen;

const AppInfo* find(const std::string& id) {
    for (const AppInfo& app : apps_list)
        if (app.id == id) return &app;
    return nullptr;
}

void show_message(const std::string& icon, const std::string& title, const std::string& text, const std::string& hint) {
    current = nullptr;
    screens::replace(new MessageScreen(icon, title, text, hint, [] { close(); }));
}

void apply(JsonArrayConst items, JsonArrayConst order) {
    ring_order.clear();
    for (const char* id : order)
        if (id) ring_order.push_back(id);
    apps_list.clear();
    for (JsonObjectConst item : items) {
        AppInfo app;
        app.id = item["id"] | "";
        app.name = item["name"] | "";
        app.subtitle = item["subtitle"] | "";
        app.icon = item["icon"] | "";
        app.color = ui::parse_color(item["color"], theme::Accent);
        if (!app.id.empty()) apps_list.push_back(app);
    }
    if (!open_app.empty() && !find(open_app)) close();  // the open app was turned off in Knob Hub
    home_refresh();
}

}  // namespace

const std::vector<AppInfo>& list() { return apps_list; }

const std::vector<std::string>& order() { return ring_order; }

// Knob Hub sends the list every time it connects: only a change is applied (an app added, changed, moved or removed)
// and saved, so the Home ring is there at once next time, even before Knob Hub connects.
void set_list(JsonArrayConst items, JsonArrayConst order) {
    JsonDocument doc;
    doc["items"] = items;
    doc["order"] = order;
    std::string list;
    serializeJson(doc, list);
    if (list != saved_list) {
        saved_list = list;
        hal_store_write(ListFile, std::vector<uint8_t>(list.begin(), list.end()));
        apply(items, order);
    }
    home_fetch_icons();  // any of the apps' Home icons not kept yet
}

void load() {
    std::vector<uint8_t> data;
    if (!hal_store_read(ListFile, data)) return;
    JsonDocument doc;
    if (deserializeJson(doc, data.data(), data.size())) return;
    saved_list.assign(data.begin(), data.end());
    apply(doc["items"], doc["order"]);
}

void open(const AppInfo& app) {
    open_app = app.id;
    if (!hub_link::connected()) {
        show_message("usb", app.name, "Open Knob Hub on your PC to use this app.", "Press to go back");
        return;
    }
    show_message(app.icon, app.name, "Opening...", "");
    JsonDocument doc;
    doc["t"] = "open";
    doc["app"] = app.id;
    hub_link::send(doc);
}

void show_screen(JsonObjectConst msg) {
    std::string app = msg["app"] | "";
    if (app.empty() || app != open_app) return;
    std::string screen_id = msg["screen"] | "";

    TemplateSink sink = [app, screen_id](const TemplateEvent& ev) {
        if (std::strcmp(ev.kind, "back") == 0 && !hub_link::connected()) {
            close();
            return;
        }
        JsonDocument doc;
        doc["t"] = "input";
        doc["app"] = app;
        doc["screen"] = screen_id;
        doc["kind"] = ev.kind;
        if (!ev.item.empty()) doc["item"] = ev.item;
        if (ev.delta) doc["delta"] = ev.delta;
        hub_link::send(doc);
    };
    TemplateScreen* screen = make_template(msg["template"] | "", msg["data"], sink);
    if (!screen) {
        toast_show("This knob can't show that screen yet");
        return;
    }
    current = screen;
    current_screen = screen_id;
    screens::replace(screen);
}

void update_screen(JsonObjectConst msg) {
    std::string app = msg["app"] | "";
    std::string screen = msg["screen"] | "";
    if (current && app == open_app && screen == current_screen) current->update(msg["data"]);
}

void close(const std::string& app_id) {
    if (open_app.empty()) return;  // e.g. a stray "close" while Settings is open
    if (!app_id.empty() && app_id != open_app) return;
    open_app.clear();
    current = nullptr;
    current_screen.clear();
    screens::home();
}

const std::string& open_id() { return open_app; }

void sync(const std::string& app_id) {
    open_app.clear();
    current = nullptr;
    current_screen.clear();
    const AppInfo* app = find(app_id);
    if (!app) return;
    open_app = app->id;
    show_message(app->icon, app->name, "Opening...", "");
}

// An app left open waits for Knob Hub ("Disconnected" or "Open Knob Hub") and comes back by itself when it connects.
void on_connected(bool connected) {
    if (open_app.empty()) return;
    if (!connected) {
        show_message("usb", "Disconnected", "Waiting for Knob Hub. Check the cable or Wi-Fi.", "Press to go back");
        return;
    }
    if (const AppInfo* app = find(open_app)) open(*app);
    else close();
}

}  // namespace apps
