#include "link.h"

#include <cstring>
#include <string>

#include "apps.h"
#include "hal.h"
#include "home.h"
#include "icons.h"
#include "screens.h"
#include "knob_os.h"
#include "settings.h"
#include "toast.h"

namespace hub_link {
namespace {
bool is_connected = false;
}

bool connected() { return is_connected; }

void send(const JsonDocument& doc) {
    if (!is_connected) return;
    std::string out;
    serializeJson(doc, out);
    hal_link_send(out.c_str());
}

void set_connected(bool c) {
    if (c == is_connected) return;
    is_connected = c;
    if (c) {
        JsonDocument hello;
        hello["t"] = "hello";
        hello["os"] = KNOB_OS_VERSION;
        hello["name"] = settings::get().name;
        hello["page"] = home_page();
        hello["app"] = apps::open_id();
        send(hello);
        settings::send_all();
    }
    apps::on_connected(c);
    icons::on_connected(c);
    settings::refresh();
}

void receive(const char* line) {
    JsonDocument doc;
    if (deserializeJson(doc, line)) return;  // ignore anything that isn't JSON
    JsonObjectConst msg = doc.as<JsonObjectConst>();
    const char* t = msg["t"] | "";
    if (std::strcmp(t, "apps") == 0) apps::set_list(msg["items"], msg["order"]);
    else if (std::strcmp(t, "screen") == 0) apps::show_screen(msg);
    else if (std::strcmp(t, "update") == 0) apps::update_screen(msg);
    else if (std::strcmp(t, "close") == 0) apps::close();
    else if (std::strcmp(t, "icon") == 0) icons::receive(msg);
    else if (std::strcmp(t, "icons") == 0) icons::check(msg);
    else if (std::strcmp(t, "toast") == 0) toast_show(msg["text"] | "");
    else if (std::strcmp(t, "settings") == 0) settings::apply_from_hub(msg);
    else if (std::strcmp(t, "key") == 0) {
        KnobInput input;
        if (knob_input_parse(msg["input"] | "", input)) knob_os_input(input);
    } else if (std::strcmp(t, "touch") == 0) {
        knob_os_touch(msg["x"] | 0, msg["y"] | 0, msg["down"] | false);
    } else if (std::strcmp(t, "sync") == 0) {
        screens::home();
        settings::forget_pages();
        home_show(msg["page"] | "clock");
        apps::sync(msg["app"] | "");
    }
}

}  // namespace hub_link
