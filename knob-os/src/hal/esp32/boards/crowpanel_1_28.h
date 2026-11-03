#pragma once
// Elecrow CrowPanel 1.28" ESP32-S3 rotary display: 240 x 240 GC9A01 (SPI), CST816D touch (I2C), a rotary encoder
// with a press, 5 WS2812 LEDs. No vibration motor, no motor on the knob. The USB-C port (power and programming) is
// the ESP32-S3's own USB: no USB-to-serial chip is named. ESP32-S3R8: 8 MB PSRAM (octal), 16 MB flash.
// Pins from Elecrow's wiki: elecrow.com/wiki/CrowPanel_1.28inch-HMI_ESP32_Rotary_Display.html, and the start-up
// steps (power pins, display settings, touch reset) from Elecrow's Arduino example for the board.

#define KNOB_BOARD_ID "crowpanel-1.28"

// Elecrow's example drives these high at start: they power the board's parts.
#define PIN_POWER_1 1
#define PIN_POWER_2 2

// Display (GC9A01, SPI; no MISO), driven through LovyanGFX
#define KNOB_PANEL lgfx::Panel_GC9A01
#define LCD_SPI_HZ 80000000
#define LCD_INVERT true
#define LCD_RGB_ORDER false
#define PIN_LCD_SCLK 10
#define PIN_LCD_MOSI 11
#define PIN_LCD_CS 9
#define PIN_LCD_DC 3
#define PIN_LCD_RST 14
#define PIN_LCD_BACKLIGHT 46
// The screen (and touch) is turned 180 degrees, so its top is where the LED ring starts.
#define SCREEN_UPSIDE_DOWN

// Touch (CST816D, I2C)
#define PIN_TOUCH_SDA 6
#define PIN_TOUCH_SCL 7
#define PIN_TOUCH_INT 5
#define PIN_TOUCH_RST 13

// Rotary encoder
#define PIN_ENCODER_A 45
#define PIN_ENCODER_B 42
#define PIN_ENCODER_PRESS 41

// LEDs (5 WS2812 on one data line; GPIO 40 is held low, as in Elecrow's example). They're chained the other way round
// the ring from Knob OS's order.
#define LEDS_REVERSED
#define PIN_LED_RGB 48
#define PIN_LED_POWER 40

// The spare I2C connector (Elecrow's examples put an SSD1306 OLED there)
#define PIN_EXT_SDA 38
#define PIN_EXT_SCL 39
