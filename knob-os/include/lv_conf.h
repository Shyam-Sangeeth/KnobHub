/* LVGL settings for Knob OS (LVGL 9.2). Anything not set here keeps LVGL's default. */
#ifndef LV_CONF_H
#define LV_CONF_H

/* The knob's GC9A01 panel is RGB565; the simulator and Knob Hub's preview draw in 32-bit so colours match the PC
   screen exactly. */
#if defined(KNOB_SIM) || defined(KNOB_PREVIEW)
#define LV_COLOR_DEPTH 32
#else
#define LV_COLOR_DEPTH 16
#endif

/* The system allocator: the round screen's clipping layer alone needs 115 KB (the knob has 8 MB of PSRAM). */
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB

#define LV_DEF_REFR_PERIOD 16
#define LV_DPI_DEF 130

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

/* Fonts: Knob OS's own (src/core/fonts, made by fonts/make_fonts.py), LVGL's Montserrat with the characters song
   titles and names use beyond ASCII. LVGL's built-in fonts stay off. Sizes are at the knob's real 240x240 scale. */
#define LV_FONT_MONTSERRAT_14 0
#define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(knob_font_14)
#define LV_FONT_DEFAULT &knob_font_14

/* Wrapped text breaks at spaces and punctuation but not at hyphens, so words like "Wi-Fi" stay whole. */
#define LV_TXT_BREAK_CHARS " ,.;:_)]}"

#ifdef KNOB_SIM
/* The simulator draws into an SDL window. */
#define LV_USE_SDL 1
#define LV_SDL_INCLUDE_PATH <SDL2/SDL.h>
#define LV_SDL_RENDER_MODE LV_DISPLAY_RENDER_MODE_DIRECT
#define LV_SDL_BUF_COUNT 1
#define LV_SDL_FULLSCREEN 0
#define LV_SDL_DIRECT_EXIT 1
#endif

#endif /* LV_CONF_H */
