#pragma once
// Elecrow CrowPanel 1.46" ESP32-S3 rotary display (DHR55146D): 360 x 360 round IPS screen over SPI (80 MHz write;
// Elecrow's demo drives it with LovyanGFX; the driver chip isn't named on the wiki), CST816T touch (I2C), a rotary
// encoder with a press, one RGB LED. No vibration motor, no motor on the knob.
// Pins from Elecrow's wiki: elecrow.com/wiki/CrowPanel_1.46-inch-HMI_ESP32_Rotary_Display_360x360_IPS_Round_Touch_Knob_Screen.html

#define KNOB_BOARD_ID "crowpanel-1.46"

// Display (SPI2_HOST; no MISO)
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

// LEDs
#define PIN_LED_RGB 48
#define PIN_LED_POWER 40

// The spare I2C connector (e.g. for a DRV2605L vibration driver)
#define PIN_EXT_SDA 38
#define PIN_EXT_SCL 39
