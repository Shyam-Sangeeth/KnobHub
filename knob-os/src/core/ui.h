#pragma once
#include <lvgl.h>

#include "fonts/fonts.h"

#include <cstdint>
#include <vector>

#include <string>

// Small helpers shared by Knob OS's screens.
//
// Screens are designed for a 240 px screen and scaled to the board's (360 px on the 1.46" CrowPanel): every length in
// a screen's code is a design length passed through px(), and fonts come from font() and digits() by design size.
// Lengths measured at run time (a label's width, a font's line height) are real pixels already. The helpers below
// take real pixels.
namespace ui {

constexpr int DesignSize = 240;

// The round screen's diameter, pixels.
int size();

// A design length (at 240 px) on this screen, rounded.
int px(int design);

// Knob OS's font for a design size (8, 12, 14, 16, 20, 24, 28; 48 for big ASCII), scaled to this screen.
const lv_font_t* font(int design_size);

// The digits-only font for a design size (36, 60, 72, 96), for big numbers.
const lv_font_t* digits(int design_size);

// A full-size container for a screen inside the round display.
lv_obj_t* screen_root(lv_obj_t* parent);

// An object without theme styles that doesn't scroll or take clicks.
lv_obj_t* plain(lv_obj_t* parent);

lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* font, uint32_t color);

// Keeps a label on one line, at most max_width wide, ending in "..." when the text is longer. Call after setting the
// text (and again whenever it changes).
void one_line(lv_obj_t* label, int max_width);

// Like one_line, but text longer than max_width scrolls round in a loop (a marquee), pausing at the start of each
// round. Call after setting the text; setting the same text again restarts the scroll, so skip that.
void marquee(lv_obj_t* label, int max_width);

// Where a letter's ink starts below the top of its line, and how tall it is: for spacing text by what's drawn (a
// label's letters sit inside its line, with room above and below). 'M' measures capitals.
int ink_top(const lv_font_t* font, uint32_t letter = 'M');
int ink_height(const lv_font_t* font, uint32_t letter = 'M');

// The marquee's speed and pause, shared with pictures of text that scroll the same way.
constexpr int MarqueeSpeed = 30;        // design pixels per second (px(MarqueeSpeed) on this screen)
constexpr int MarqueePauseMs = 1500;

// A filled circle with an optional border.
lv_obj_t* circle(lv_obj_t* parent, int size, uint32_t fill, uint32_t border, int border_width);

// A ring/arc widget for values: bg_degrees of track, starting at rotation degrees (0 = 3 o'clock, clockwise).
lv_obj_t* arc(lv_obj_t* parent, int size, int width, int rotation, int bg_degrees, uint32_t track, uint32_t color);

// "#RRGGBB" to 0xRRGGBB; fallback when missing or malformed.
uint32_t parse_color(const char* hex, uint32_t fallback);

// A darker tint of a colour for filled backgrounds behind it.
uint32_t tint(uint32_t color);

// A light tint of a colour, for text on that dark tint.
uint32_t light(uint32_t color);

// Touch feedback for a tappable object: while pressed it shrinks to scale_percent and darkens, and eases back when
// let go. Big objects (list rows) want a smaller change than buttons.
void press_feedback(lv_obj_t* obj, int scale_percent = 92);

// A length of a picture Knob Hub drew (album art, text) on this screen. Pictures carry the screen size they were drawn
// for ("screen"; 0 when missing, meaning this one's): Knob Hub's preview shows the connected knob's, which may be
// another size.
int picture_px(int length, int drawn_for);

// Decodes base64 (e.g. an image Knob Hub sent); stops at the first character that isn't base64.
std::vector<uint8_t> base64_decode(const char* text);

// The font symbol for one of Knob OS's own icons (settings, clock, usb, wifi, sun, lights, about, trash, restart,
// check), or null for any other name.
const char* symbol(const std::string& icon);

// An icon by name, about size pixels: Knob OS's own as a font symbol, any other (an app's) as the picture Knob Hub
// draws for it (see icons.h). A new label or image, not positioned.
lv_obj_t* icon(lv_obj_t* parent, const std::string& name, int size, uint32_t color);

// Crosses out an icon (e.g. muted): a slash corner to corner across box, which is size x size, with a gap in
// background around it so it reads over the icon.
void cross_out(lv_obj_t* box, int size, uint32_t color, uint32_t background);

// Changes an icon's colour (a picture or a font symbol, as made by icon).
void set_icon_color(lv_obj_t* icon, uint32_t color);

// A small round badge with an icon in it, 15 px (design), centred at (center_x, center_y) in parent: e.g. on a tile's
// ring.
lv_obj_t* badge(lv_obj_t* parent, const std::string& icon, int center_x, int center_y, uint32_t color);

}  // namespace ui
