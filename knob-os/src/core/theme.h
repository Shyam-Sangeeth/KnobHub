#pragma once
#include <lvgl.h>

// Knob OS colours (from the design mockups). Sizes in the screens are at the knob's real 240x240 scale.
namespace theme {

inline lv_color_t color(uint32_t rgb) { return lv_color_hex(rgb); }

constexpr uint32_t Screen = 0x07080A;      // the round screen's background
constexpr uint32_t Bezel = 0x1E2024;       // around the circle (only visible in the simulator)
constexpr uint32_t Text = 0xF2F3F5;
constexpr uint32_t TextSecondary = 0x9AA0AA;
constexpr uint32_t TextMuted = 0x8D939D;
constexpr uint32_t Surface = 0x16181C;
constexpr uint32_t SurfaceRaised = 0x1A1C21;
constexpr uint32_t Track = 0x1C1E23;
constexpr uint32_t Border = 0x2A2D33;
constexpr uint32_t DotOff = 0x3A3D44;

// The accent, for apps that don't send a colour of their own, and its dark fill (e.g. a toggle that's on).
constexpr uint32_t Accent = 0x8B7CF6;
constexpr uint32_t AccentFill = 0x1D1A33;
// Knob OS's own Settings.
constexpr uint32_t Settings = 0x9AA0AA;
constexpr uint32_t SettingsFill = 0x1A1C21;

}  // namespace theme
