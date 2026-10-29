#include "haptics.h"

#include "hal.h"
#include "settings.h"

namespace haptics {
namespace {

Feel last;
bool sent = false;

// The Strength setting (Light, Medium, Strong) as a percentage for the hardware.
int strength() {
    static const int Percent[] = {40, 70, 100};
    int level = settings::get().haptics;
    return Percent[level < 0 ? 0 : level > 2 ? 2 : level];
}

}  // namespace

void apply(const Feel& feel) {
    if (!hal_board().motor || (sent && feel == last)) return;
    last = feel;
    sent = true;
    hal_motor_feel(feel, strength());
}

void reapply() {
    if (hal_board().motor && sent) hal_motor_feel(last, strength());
}

void turned(bool moved) {
    if (!hal_board().motor) play(moved ? Effect::Tick : Effect::Bump);
}

void play(Effect effect) {
    if (hal_board().vibration && settings::get().vibration) hal_vibrate(effect, strength());
}

}  // namespace haptics
