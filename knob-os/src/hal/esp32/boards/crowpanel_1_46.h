#pragma once
// Elecrow CrowPanel 1.46" ESP32-S3 rotary display (DHR55146D): 360 x 360 round IPS screen (ST77961, SPI), CST816T
// touch (I2C), a rotary encoder with a press, a ring of 8 WS2812 LEDs. No vibration motor, no motor on the knob.
// ESP32-S3 with 8 MB PSRAM, 16 MB flash.
// Pins from Elecrow's wiki: elecrow.com/wiki/CrowPanel_1.46-inch-HMI_ESP32_Rotary_Display_360x360_IPS_Round_Touch_Knob_Screen.html
// and the display settings, power pins and LED ring from Elecrow's Arduino example (RotaryScreen_1_46.h and its .ino in
// github.com/Elecrow-RD/CrowPanel-1.46inch-HMI-ESP32-Rotary-Display).

#define KNOB_BOARD_ID "crowpanel-1.46"

// Elecrow's example drives these high at start: the screen's power rails.
#define PIN_POWER_1 1
#define PIN_POWER_2 2

// Display (ST77961, SPI2_HOST; no MISO), driven through LovyanGFX with this screen's own start-up commands
#include "crowpanel_1_46_panel.h"
#define KNOB_PANEL CrowPanel146Panel
#define LCD_SPI_HZ 80000000
#define LCD_INVERT false
#define LCD_RGB_ORDER true
#define PIN_LCD_SCLK 10
#define PIN_LCD_MOSI 11
#define PIN_LCD_CS 9
#define PIN_LCD_DC 3
#define PIN_LCD_RST 14
#define PIN_LCD_BACKLIGHT 46

// Touch (CST816T, I2C)
#define PIN_TOUCH_SDA 6
#define PIN_TOUCH_SCL 7
#define PIN_TOUCH_INT 5
#define PIN_TOUCH_RST 13

// Rotary encoder
#define PIN_ENCODER_A 45
#define PIN_ENCODER_B 42
#define PIN_ENCODER_PRESS 41
// Each click of the encoder is half a cycle of A and B (the 1.28"'s is a whole one).
#define ENCODER_HALF_CYCLE

// LEDs (8 WS2812 on one data line, powered through GPIO 17; GPIO 40 is the power light, on when low)
#define PIN_LED_RGB 48
#define PIN_LED_POWER 40
#define PIN_LED_RING_POWER 17
// The ring's first LED in Knob OS's order (the top of the screen) is the last in the chain.
#define LED_AT_TOP 7

// The spare I2C connector (e.g. for a DRV2605L vibration driver)
#define PIN_EXT_SDA 38
#define PIN_EXT_SCL 39
