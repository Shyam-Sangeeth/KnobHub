#include "lights.h"

#include <vector>

#include "apps.h"
#include "hal.h"
#include "idle.h"
#include "settings.h"
#include "theme.h"

namespace lights {
namespace {

std::vector<uint32_t> last;
int last_start = 0, last_sweep = 0;
bool sent = false;

uint32_t open_app_color() {
    for (const AppInfo& app : apps::list()) {
        if (app.id == apps::open_id() && app.color != 0) return app.color;
    }
    return theme::Accent;
}

// A colour at percent of its brightness.
uint32_t dim(uint32_t color, int percent) {
    uint32_t r = (color >> 16 & 0xFF) * percent / 100, g = (color >> 8 & 0xFF) * percent / 100,
             b = (color & 0xFF) * percent / 100;
    return r << 16 | g << 8 | b;
}

}  // namespace

void apply(const Glow& glow) {
    const int count = hal_board().leds;
    if (count <= 0) return;
    const KnobSettings& s = settings::get();
    uint32_t color = glow.color == NoColor ? open_app_color() : glow.color;
    std::vector<uint32_t> colors(count, 0);
    if (s.led_mode != 2 && !idle::leds_off()) {
        for (int i = 0; i < count; i++) {
            int percent = s.led_brightness;
            if (s.led_mode == 0 && glow.level >= 0) {
                // How much of this LED the level reaches: LED i covers (i, i + 1] of count.
                int fill = glow.level * count - i * 100;
                percent = percent * (fill < 0 ? 0 : fill > 100 ? 100 : fill) / 100;
            }
            colors[i] = dim(color, percent);
        }
    }
    if (sent && colors == last && glow.arc_start == last_start && glow.arc_sweep == last_sweep) return;
    last = colors;
    last_start = glow.arc_start;
    last_sweep = glow.arc_sweep;
    sent = true;
    hal_set_leds(last.data(), count, last_start, last_sweep);
}

}  // namespace lights
