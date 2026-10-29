#include "icons.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

#include "hal.h"
#include "link.h"
#include "theme.h"
#include "ui.h"

namespace icons {
namespace {

constexpr int MaxSize = 128;
constexpr const char* CacheFile = "icons.bin";
constexpr uint32_t CacheMagic = 0x3143494B;  // "KIC1"
constexpr uint32_t SaveDelayMs = 1500;       // icons come in bursts: saved once they stop

struct Icon {
    std::string name;
    int size;
    uint32_t hash;  // Knob Hub's fingerprint of how it draws the icon
    std::vector<uint8_t> pixels;
    lv_image_dsc_t dsc;
};

std::vector<std::unique_ptr<Icon>> cache;
// Icons dropped from the cache (their app went, or Knob Hub draws them differently now) that images on screen may
// still show: kept until restart, never saved.
std::vector<std::unique_ptr<Icon>> retired;

struct Waiting {
    lv_obj_t* image;
    std::string name;
    int size;
};
std::vector<Waiting> waiting;  // images shown empty until their icon arrives

std::vector<std::pair<std::string, int>> asked;  // since the link came up
lv_timer_t* save_timer = nullptr;

Icon* find(const std::string& name, int size) {
    for (const auto& icon : cache)
        if (icon->size == size && icon->name == name) return icon.get();
    return nullptr;
}

// Once per connection, unless again is set (Knob Hub draws it differently now).
void ask(const std::string& name, int size, bool again = false) {
    if (!hub_link::connected()) return;  // asked once it comes up
    auto key = std::make_pair(name, size);
    if (std::find(asked.begin(), asked.end(), key) != asked.end() && !again) return;
    asked.push_back(key);
    JsonDocument doc;
    doc["t"] = "icon_request";
    doc["name"] = name;
    doc["size"] = size;
    hub_link::send(doc);
}

void on_image_deleted(lv_event_t* e) {
    auto* image = static_cast<lv_obj_t*>(lv_event_get_target(e));
    waiting.erase(std::remove_if(waiting.begin(), waiting.end(), [&](const Waiting& w) { return w.image == image; }), waiting.end());
}

void point_dsc(Icon& icon, int w, int h) {
    icon.dsc = {};
    icon.dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    icon.dsc.header.cf = LV_COLOR_FORMAT_A8;
    icon.dsc.header.w = w;
    icon.dsc.header.h = h;
    icon.dsc.header.stride = w;
    icon.dsc.data_size = static_cast<uint32_t>(icon.pixels.size());
    icon.dsc.data = icon.pixels.data();
}

Icon* add(const std::string& name, int size, uint32_t hash, int w, int h, std::vector<uint8_t> pixels) {
    auto icon = std::make_unique<Icon>();
    icon->name = name;
    icon->size = size;
    icon->hash = hash;
    icon->pixels = std::move(pixels);
    point_dsc(*icon, w, h);
    cache.push_back(std::move(icon));
    return cache.back().get();
}

void retire(Icon* icon) {
    auto it = std::find_if(cache.begin(), cache.end(), [&](const auto& c) { return c.get() == icon; });
    if (it == cache.end()) return;
    retired.push_back(std::move(*it));
    cache.erase(it);
}

// --- Saved icons: "KIC1", then per icon: name length, name, size, w, h (1 byte each but the name), hash (4 bytes),
// then w x h alpha bytes ---

void put_u32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; i++) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

void save() {
    std::vector<uint8_t> out;
    put_u32(out, CacheMagic);
    for (const auto& icon : cache) {
        out.push_back(static_cast<uint8_t>(std::min<size_t>(icon->name.size(), 255)));
        out.insert(out.end(), icon->name.begin(), icon->name.begin() + std::min<size_t>(icon->name.size(), 255));
        out.push_back(static_cast<uint8_t>(icon->size));
        out.push_back(static_cast<uint8_t>(icon->dsc.header.w));
        out.push_back(static_cast<uint8_t>(icon->dsc.header.h));
        put_u32(out, icon->hash);
        out.insert(out.end(), icon->pixels.begin(), icon->pixels.end());
    }
    hal_store_write(CacheFile, out);
}

void save_soon() {
    if (save_timer) {
        lv_timer_reset(save_timer);
        return;
    }
    save_timer = lv_timer_create([](lv_timer_t*) {
        save_timer = nullptr;  // a one-shot timer deletes itself
        save();
    }, SaveDelayMs, nullptr);
    lv_timer_set_repeat_count(save_timer, 1);
}

}  // namespace

void load() {
    std::vector<uint8_t> data;
    if (!hal_store_read(CacheFile, data) || data.size() < 4) return;
    size_t at = 0;
    auto u32 = [&]() {
        uint32_t v = 0;
        for (int i = 0; i < 4; i++) v |= static_cast<uint32_t>(data[at++]) << (8 * i);
        return v;
    };
    if (u32() != CacheMagic) return;
    while (at < data.size()) {
        size_t name_length = data[at++];
        if (at + name_length + 7 > data.size()) return;
        std::string name(data.begin() + at, data.begin() + at + name_length);
        at += name_length;
        int size = data[at++], w = data[at++], h = data[at++];
        uint32_t hash = u32();
        size_t count = static_cast<size_t>(w) * h;
        if (w <= 0 || h <= 0 || w > MaxSize || h > MaxSize || at + count > data.size()) return;
        add(name, size, hash, w, h, std::vector<uint8_t>(data.begin() + at, data.begin() + at + count));
        at += count;
    }
}

lv_obj_t* create(lv_obj_t* parent, const std::string& name, int size, uint32_t color) {
    lv_obj_t* image = lv_image_create(parent);
    lv_obj_set_size(image, size, size);
    lv_obj_set_style_image_recolor(image, theme::color(color), 0);
    lv_obj_set_style_image_recolor_opa(image, LV_OPA_COVER, 0);
    if (name.empty()) return image;
    if (const Icon* icon = find(name, size)) {
        lv_image_set_src(image, &icon->dsc);
        return image;
    }
    waiting.push_back({image, name, size});
    lv_obj_add_event_cb(image, on_image_deleted, LV_EVENT_DELETE, nullptr);
    ask(name, size);
    return image;
}

void fetch(const std::string& name, int size) {
    if (!name.empty() && !ui::symbol(name) && !find(name, size)) ask(name, size);
}

void receive(JsonObjectConst msg) {
    std::string name = msg["name"] | "";
    int size = msg["size"] | 0, w = msg["w"] | 0, h = msg["h"] | 0;
    uint32_t hash = msg["hash"] | 0u;
    if (name.empty() || w <= 0 || h <= 0 || w > MaxSize || h > MaxSize) return;
    std::vector<uint8_t> pixels = ui::base64_decode(msg["a8"] | "");
    if (pixels.size() != static_cast<size_t>(w * h)) return;

    Icon* icon = find(name, size);
    if (icon && icon->hash == hash) return;
    if (icon && static_cast<int>(icon->dsc.header.w) == w && static_cast<int>(icon->dsc.header.h) == h) {
        // Drawn differently now (e.g. its app was updated): changed in place, so images showing it change too.
        icon->pixels = std::move(pixels);
        icon->hash = hash;
        point_dsc(*icon, w, h);
        lv_image_cache_drop(&icon->dsc);
        lv_obj_invalidate(lv_screen_active());
    } else {
        if (icon) retire(icon);
        icon = add(name, size, hash, w, h, std::move(pixels));
    }
    save_soon();

    for (auto it = waiting.begin(); it != waiting.end();) {
        if (it->size == size && it->name == name) {
            lv_obj_remove_event_cb(it->image, on_image_deleted);
            lv_image_set_src(it->image, &icon->dsc);
            it = waiting.erase(it);
        } else {
            ++it;
        }
    }
}

void check(JsonObjectConst msg) {
    JsonObjectConst known = msg["known"];
    bool changed = false;
    for (size_t i = 0; i < cache.size();) {
        Icon* icon = cache[i].get();
        JsonVariantConst hash = known[icon->name];
        if (!hash.is<uint32_t>()) {
            retire(icon);  // Knob Hub has no such icon any more: its app was removed or turned off
            changed = true;
            continue;
        }
        if (hash.as<uint32_t>() != icon->hash) ask(icon->name, icon->size, true);  // shown as it was until the new one comes
        i++;
    }
    if (changed) save_soon();
}

void on_connected(bool connected) {
    asked.clear();
    if (!connected) return;
    for (const Waiting& w : waiting) ask(w.name, w.size);
}

}  // namespace icons
