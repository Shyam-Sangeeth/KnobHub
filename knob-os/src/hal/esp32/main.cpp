// Knob OS on a real knob: an ESP32-S3 round display. One build per board (see platformio.ini); the board's pins are
// in boards/<board>.h, chosen by KNOB_BOARD.
// Done: the display (SPI, through LovyanGFX), touch (CST816), the rotary encoder and its press (short press =
// KnobInput::Press, held = KnobInput::Back), the backlight, the LEDs (WS2812), the link over USB serial and Wi-Fi
// (see "The link" below, and net.cpp for Wi-Fi and pairing), the flash's file system, and vibration through a haptic
// driver on the spare I2C connector when one is there (see "Vibration" below).
// Still to be written: on boards that have one, the motor on the knob (hal_motor_feel: detents and end stops by
// field-oriented control, e.g. SimpleFOC, on its own core).
#define LGFX_USE_V1
#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <LovyanGFX.hpp>
#include <USB.h>
#include <USBCDC.h>
#include <Wire.h>
#include <esp32-hal-tinyusb.h>
#include <lvgl.h>
#include <soc/gpio_reg.h>
#include <sys/time.h>

#include <atomic>
#include <deque>
#include <mutex>
#include <string>

#include KNOB_BOARD
#include "core/hal.h"
#include "core/knob_os.h"
#include "net.h"

USBCDC* usb_serial = nullptr;  // Knob OS's USB serial port: the link to Knob Hub (start_usb)

namespace {

// --- Display ---

#ifdef KNOB_PANEL
class Screen : public lgfx::LGFX_Device {
    KNOB_PANEL panel_;
    lgfx::Bus_SPI bus_;

public:
    void setup(int size) {
        auto bus = bus_.config();
        bus.spi_host = SPI2_HOST;
        bus.spi_mode = 0;
        bus.freq_write = LCD_SPI_HZ;
        bus.spi_3wire = true;  // no MISO
        bus.dma_channel = SPI_DMA_CH_AUTO;
        bus.pin_sclk = PIN_LCD_SCLK;
        bus.pin_mosi = PIN_LCD_MOSI;
        bus.pin_miso = -1;
        bus.pin_dc = PIN_LCD_DC;
        bus_.config(bus);
        panel_.setBus(&bus_);

        auto panel = panel_.config();
        panel.pin_cs = PIN_LCD_CS;
        panel.pin_rst = PIN_LCD_RST;
        panel.pin_busy = -1;
        panel.memory_width = panel.panel_width = size;
        panel.memory_height = panel.panel_height = size;
        panel.invert = LCD_INVERT;
        panel.rgb_order = LCD_RGB_ORDER;
        panel.readable = false;
        panel.bus_shared = false;
        panel_.config(panel);
        setPanel(&panel_);
    }

#ifdef PANEL_RESTART_MS
    void restart_panel() {
        waitDMA();
        endWrite();
        panel_.restart();
        startWrite();
    }
#endif

};

Screen screen;

#ifdef KNOB_PERF
// Built with -D KNOB_PERF, the knob prints how long each frame took (tools/knob_perf.py reads it).
struct {
    uint32_t start = 0, flush_us = 0, pixels = 0, flushes = 0;
} perf;
#endif

// LVGL draws into one buffer while the other goes to the screen by DMA. LovyanGFX waits for a transfer to finish
// before starting the next, so a buffer is free again by the time LVGL draws into it.

void flush(lv_display_t* display, const lv_area_t* area, uint8_t* pixels) {
    uint32_t w = lv_area_get_width(area), h = lv_area_get_height(area);
#ifdef KNOB_PERF
    uint32_t t0 = micros();
    perf.pixels += w * h;
    perf.flushes++;
#endif
    lv_draw_sw_rgb565_swap(pixels, w * h);  // LVGL's RGB565 is low byte first; the screen takes the high byte first
    screen.pushImageDMA(area->x1, area->y1, w, h, reinterpret_cast<const lgfx::swap565_t*>(pixels));
#ifdef KNOB_PERF
    perf.flush_us += micros() - t0;
#endif
    lv_display_flush_ready(display);
}

void start_display() {
    int size = hal_board().size;
    screen.setup(size);
    screen.init();
#ifdef SCREEN_UPSIDE_DOWN
    screen.setRotation(2);
#endif
    screen.fillScreen(TFT_BLACK);  // before the backlight comes on, so it never shows what was left in its memory
    screen.startWrite();           // kept open: closing it would wait for each transfer

    lv_display_t* display = lv_display_create(size, size);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    // 38 KB per buffer, in DMA-capable internal RAM: a third of a 240 px screen (a sixth took a fifth longer per frame),
    // and as much of a bigger one as leaves room for Wi-Fi.
    uint32_t bytes = size * (240 * 80 / size) * 2;
    void* a = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void* b = heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    lv_display_set_buffers(display, a, b, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
#ifdef PANEL_RESTART_MS
    // Boards whose screen can miss its start (see the board file): start it again once, and draw everything again.
    lv_timer_t* restart = lv_timer_create([](lv_timer_t*) {
        screen.restart_panel();
        lv_obj_invalidate(lv_screen_active());
        lv_obj_invalidate(lv_layer_top());
    }, PANEL_RESTART_MS, nullptr);
    lv_timer_set_repeat_count(restart, 1);
#endif
#ifdef KNOB_PERF
    lv_display_add_event_cb(display, [](lv_event_t*) { perf = {}; perf.start = micros(); }, LV_EVENT_REFR_START, nullptr);
    lv_display_add_event_cb(display, [](lv_event_t*) {
        if (perf.pixels) usb_serial->printf("perf frame=%lu us flush=%lu us px=%lu strips=%lu cpu=%lu free=%u\n", micros() - perf.start,
                                       perf.flush_us, perf.pixels, perf.flushes, getCpuFrequencyMhz(),
                                       heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    }, LV_EVENT_REFR_READY, nullptr);
#endif
}
#else
void start_display() { lv_display_create(hal_board().size, hal_board().size); }  // no screen driver for this board yet
#endif

// --- Touch (CST816: finger count at register 0x02, then X and Y, 12 bits each) ---

constexpr uint8_t TouchAddress = 0x15;

bool touch_read(int& x, int& y) {
    Wire.beginTransmission(TouchAddress);
    Wire.write(0x02);
    if (Wire.endTransmission(false) != 0) return false;
    uint8_t d[5];
    if (Wire.requestFrom(TouchAddress, static_cast<uint8_t>(sizeof d)) != sizeof d) return false;
    for (uint8_t& v : d) v = Wire.read();
    if ((d[0] & 0x0F) == 0) return false;
    x = ((d[1] & 0x0F) << 8) | d[2];
    y = ((d[3] & 0x0F) << 8) | d[4];
    return true;
}

struct {
    bool down = false;
    int x = 0, y = 0;
} finger;

// The knob is pressed by pushing the screen, so a push touches it too, and the tap would act as well as the press
// (pressing About on the Settings list opened Connection, the row in the middle). A touch under way when the button
// goes down is cancelled, not a tap, and so is one that starts while it's down; the finger is ignored until it's lifted.
bool touch_ignored = false;

void read_touch(lv_indev_t* indev, lv_indev_data_t* data) {
    int x, y;
    bool down = touch_read(x, y);
    if (touch_ignored && !down) touch_ignored = false;
    if (!touch_ignored && down && digitalRead(PIN_ENCODER_PRESS) == LOW) {
        touch_ignored = true;
        if (finger.down) {
            finger.down = false;
            lv_indev_reset(indev, nullptr);  // forgets the press: lifting it isn't a tap
            knob_os_report_touch(finger.x, finger.y, false, true);
        }
    }
    if (touch_ignored) {
        data->point.x = finger.x;
        data->point.y = finger.y;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    int size = hal_board().size;
    if (down) {
#ifdef SCREEN_UPSIDE_DOWN
        x = size - 1 - x;
        y = size - 1 - y;
#endif
        x = std::min(std::max(x, 0), size - 1);
        y = std::min(std::max(y, 0), size - 1);
        if (!finger.down || x != finger.x || y != finger.y) knob_os_report_touch(x, y, true);
        finger = {true, x, y};
    } else if (finger.down) {
        finger.down = false;
        knob_os_report_touch(finger.x, finger.y, false);
    }
    data->point.x = finger.x;
    data->point.y = finger.y;
    data->state = finger.down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void start_touch() {
    Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL, 400000);
    // Reset, as Elecrow's example does it.
    pinMode(PIN_TOUCH_INT, OUTPUT);
    digitalWrite(PIN_TOUCH_INT, HIGH);
    delay(1);
    digitalWrite(PIN_TOUCH_INT, LOW);
    delay(1);
    pinMode(PIN_TOUCH_INT, INPUT);
    pinMode(PIN_TOUCH_RST, OUTPUT);
    digitalWrite(PIN_TOUCH_RST, LOW);
    delay(10);
    digitalWrite(PIN_TOUCH_RST, HIGH);
    delay(300);
    // Never sleep: asleep, it stops answering on I2C.
    Wire.beginTransmission(TouchAddress);
    Wire.write(0xFE);
    Wire.write(0xFF);
    Wire.endTransmission();

    lv_indev_t* touch = lv_indev_create();
    lv_indev_set_type(touch, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch, read_touch);
}

// --- The knob: a rotary encoder (A and B) with a press ---

// Read straight from the GPIO registers: the interrupt may run while the flash is busy (a file being saved), when
// code in flash can't run.
inline bool IRAM_ATTR pin_high(int pin) {
    return pin < 32 ? (REG_READ(GPIO_IN_REG) >> pin) & 1 : (REG_READ(GPIO_IN1_REG) >> (pin - 32)) & 1;
}

// Quarter steps from each change of A and B; one turn step per detent, counted when it's back at rest. A detent is a
// full cycle of A and B, or half of one on boards with ENCODER_HALF_CYCLE (then it rests at both ends of the cycle).
std::atomic<int> encoder_steps{0};
uint8_t encoder_rest = 0b11;
volatile uint8_t encoder_state = 0;
volatile int encoder_quarters = 0;

void IRAM_ATTR on_encoder() {
    static const int8_t Quarter[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
    uint8_t ab = (pin_high(PIN_ENCODER_A) << 1) | pin_high(PIN_ENCODER_B);
    encoder_state = ((encoder_state << 2) | ab) & 0x0F;
    encoder_quarters = encoder_quarters + Quarter[encoder_state];
#ifdef ENCODER_HALF_CYCLE
    if (ab != encoder_rest && ab != (encoder_rest ^ 0b11)) return;
#else
    if (ab != encoder_rest) return;
#endif
    if (encoder_quarters >= 2) encoder_steps++;
    else if (encoder_quarters <= -2) encoder_steps--;
    encoder_quarters = 0;
}

// The press: a press when let go quickly, back (the long press) once held long enough, never both.
constexpr uint32_t LongPressMs = 500;
constexpr uint32_t DebounceMs = 20;
struct {
    bool raw = false, down = false, long_sent = false;
    uint32_t raw_since = 0, since = 0;
} button;

void start_knob() {
    pinMode(PIN_ENCODER_A, INPUT_PULLUP);
    pinMode(PIN_ENCODER_B, INPUT_PULLUP);
    pinMode(PIN_ENCODER_PRESS, INPUT_PULLUP);
    encoder_rest = (digitalRead(PIN_ENCODER_A) << 1) | digitalRead(PIN_ENCODER_B);  // it starts at a detent
    encoder_state = encoder_rest;
    attachInterrupt(PIN_ENCODER_A, on_encoder, CHANGE);
    attachInterrupt(PIN_ENCODER_B, on_encoder, CHANGE);
}

void poll_knob() {
    uint32_t now = millis();
    bool raw = digitalRead(PIN_ENCODER_PRESS) == LOW;

    // Pushing the knob in can turn it a click, which would move the choice before the press lands (pressing About
    // opened Connection, the row above). Turns while it's held in are dropped.
    int steps = encoder_steps.exchange(0);
    if (raw || button.down) steps = 0;
    for (; steps > 0; steps--) knob_os_input(KnobInput::TurnRight);
    for (; steps < 0; steps++) knob_os_input(KnobInput::TurnLeft);

    if (raw != button.raw) {
        button.raw = raw;
        button.raw_since = now;
    }
    if (now - button.raw_since >= DebounceMs && raw != button.down) {
        button.down = raw;
        if (raw) {
            button.since = now;
            button.long_sent = false;
        } else if (!button.long_sent) {
            knob_os_input(KnobInput::Press);
        }
    }
    if (button.down && !button.long_sent && now - button.since >= LongPressMs) {
        button.long_sent = true;
        knob_os_input(KnobInput::Back);
    }
}

// The board as listed; vibration is added at start if a haptic driver is found.
Board& board_info() {
    static Board board = *boards::find(KNOB_BOARD_ID);
    return board;
}

// --- Vibration: an optional haptic driver on the spare I2C connector ---
//
// DFRobot's Gravity DRI0056 (a TM6605 driving an LRA motor) at 0x2D: an effect number (one of the 44 the chip has) in
// register 0x04, then 1 in register 0x0C plays it, and 0 stops it (from DFRobot's MIT-licensed DFRobot_TM6605 library).
// It's looked for at start (it answers a moment after power-on); without it the board has no vibration.

#ifdef PIN_EXT_SDA
constexpr uint8_t HapticAddress = 0x2D;
constexpr uint8_t HapticEffect = 0x04, HapticControl = 0x0C;
bool haptic_found = false;

void haptic_write(uint8_t reg, uint8_t value) {
    Wire1.beginTransmission(HapticAddress);
    Wire1.write(reg);
    Wire1.write(value);
    Wire1.endTransmission();
}

void start_vibration() {
    Wire1.begin(PIN_EXT_SDA, PIN_EXT_SCL, 400000);
    for (int tries = 0; tries < 10 && !haptic_found; tries++) {
        if (tries) delay(30);
        Wire1.beginTransmission(HapticAddress);
        haptic_found = Wire1.endTransmission() == 0;
    }
    if (haptic_found) board_info().vibration = true;
}
#else
constexpr bool haptic_found = false;
void start_vibration() {}
#endif

// --- Backlight ---

constexpr uint32_t BacklightHz = 5000;

void start_backlight() {
    ledcAttach(PIN_LCD_BACKLIGHT, BacklightHz, 8);
    ledcWrite(PIN_LCD_BACKLIGHT, 0);  // off until Knob OS sets it
}

// --- LEDs (WS2812: 24 bits each, green-red-blue, by the RMT at 10 MHz: a 1 is 0.8 us high then 0.4 us low, a 0 the
// other way round) ---

constexpr int MaxLeds = 8;

void start_leds() {
    pinMode(PIN_LED_POWER, OUTPUT);
    digitalWrite(PIN_LED_POWER, LOW);
#ifdef PIN_LED_RING_POWER
    pinMode(PIN_LED_RING_POWER, OUTPUT);
    digitalWrite(PIN_LED_RING_POWER, HIGH);
#endif
    rmtInit(PIN_LED_RGB, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_1, 10000000);
}

// --- The link ---
//
// Over USB, Knob OS doesn't go by the port's DTR to tell when Knob Hub opens it (the chip's USB Serial/JTAG unit,
// which Knob OS used before 1.0.2, has none to read), so Knob Hub sends {"t":"hub","session","time","offset"} as soon as it opens it and every 2 seconds after. The link is
// up from the first one (Knob OS says hello) until none has come for 6 seconds; a new session (Knob Hub started
// again) brings it up afresh. "time" (Unix seconds) and "offset" (minutes from UTC) set the clock. Those messages stay
// here, as does "pair" (net.cpp); every other line goes to Knob OS.
//
// Over Wi-Fi (net.cpp) the link is up once Knob Hub has proved it holds the key, and Knob Hub sends the same "hub"
// messages, for the time and to show it's still there. USB always wins: when it comes up, Wi-Fi is let go of.

constexpr uint32_t HubSilentMs = 6000;
constexpr size_t MaxLine = 256 * 1024;  // longer lines are dropped

// Lines from Knob Hub, read by a task of their own: the USB serial's buffer drops what doesn't fit, so it's emptied
// even while the main loop is busy drawing.
std::mutex lines_lock;
std::deque<std::string> lines;

void read_link(void*) {
    std::string line;
    bool too_long = false;
    uint8_t buffer[512];
    while (true) {
        int available = usb_serial->available();
        if (available <= 0) {
            vTaskDelay(1);
            continue;
        }
        size_t n = usb_serial->read(buffer, std::min(static_cast<size_t>(available), sizeof buffer));
        for (size_t i = 0; i < n; i++) {
            char c = static_cast<char>(buffer[i]);
            if (c == '\n') {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (!too_long && !line.empty()) {
                    std::lock_guard<std::mutex> lock(lines_lock);
                    lines.push_back(std::move(line));
                }
                line.clear();
                too_long = false;
            } else if (too_long) {
            } else if (line.size() < MaxLine) {
                line += c;
            } else {
                too_long = true;
                line = std::string();
            }
        }
    }
}

enum class Via { None, Usb, Wifi };
Via via = Via::None;
uint32_t heard_at = 0;
std::string usb_session;
unsigned wifi_generation = 0;
bool time_known = false;
int utc_offset_minutes = 0;

bool starts(const std::string& line, const char* prefix) { return line.rfind(prefix, 0) == 0; }

void link_up(Via v) {
    if (via != Via::None) knob_os_link_connected(false);
    via = v;
    heard_at = millis();
    knob_os_link_connected(true);
}

void link_down() {
    if (via == Via::None) return;
    if (via == Via::Wifi) net::drop();
    via = Via::None;
    knob_os_link_connected(false);
}

// Knob Hub's heartbeat: the time, and over USB the link itself.
void hub_message(const std::string& line, Via from) {
    JsonDocument msg;
    if (deserializeJson(msg, line)) return;
    if (msg["time"].is<long long>()) {
        timeval now = {static_cast<time_t>(msg["time"].as<long long>()), 0};
        settimeofday(&now, nullptr);
        utc_offset_minutes = msg["offset"] | 0;
        time_known = true;
    }
    if (from != Via::Usb) return;
    std::string session = msg["session"] | "";
    if (via == Via::Usb && session == usb_session) return;
    usb_session = session;
    net::drop();
    link_up(Via::Usb);
}

void poll_link() {
    std::deque<std::string> received;
    {
        std::lock_guard<std::mutex> lock(lines_lock);
        received.swap(lines);
    }
    for (const std::string& line : received) {
        if (starts(line, "{\"t\":\"pair\"")) {
            net::pair(line);
            continue;
        }
        if (starts(line, "{\"t\":\"hub\"")) hub_message(line, Via::Usb);
        if (via != Via::Usb) continue;
        heard_at = millis();
        if (!starts(line, "{\"t\":\"hub\"")) knob_os_link_line(line.c_str());
    }

    net::poll();
    if (via == Via::Usb) {
        if (net::linked()) net::drop();
    } else if (net::linked()) {
        if (via != Via::Wifi || net::generation() != wifi_generation) {
            wifi_generation = net::generation();
            link_up(Via::Wifi);
        }
        std::string line;
        while (via == Via::Wifi && net::next_line(line)) {
            heard_at = millis();
            if (starts(line, "{\"t\":\"hub\"")) hub_message(line, Via::Wifi);
            else knob_os_link_line(line.c_str());
        }
    } else if (via == Via::Wifi) {
        link_down();
    }

    if (via != Via::None && millis() - heard_at > HubSilentMs) link_down();
}

}  // namespace

const Board& hal_board() { return board_info(); }

bool hal_local_time(std::tm& out) {
    if (!time_known) return false;  // until Knob Hub sends it
    time_t local = time(nullptr) + utc_offset_minutes * 60;
    gmtime_r(&local, &out);
    return true;
}

void hal_link_send(const char* line) {
    if (via == Via::Wifi) {
        net::send(line);
    } else if (via == Via::Usb) {
        usb_serial->write(reinterpret_cast<const uint8_t*>(line), strlen(line));
        usb_serial->write('\n');
    }
}

const char* hal_link_kind() { return via == Via::Usb ? "USB" : via == Via::Wifi ? "Wi-Fi" : ""; }

void hal_set_brightness(int percent) {
    percent = std::min(std::max(percent, 0), 100);
    ledcWrite(PIN_LCD_BACKLIGHT, percent * 255 / 100);
}

void hal_motor_feel(const haptics::Feel& feel, int strength) {
    (void)feel;
    (void)strength;
}

void hal_vibrate(haptics::Effect effect, int strength) {
#ifdef PIN_EXT_SDA
    if (!haptic_found) return;
    // The TM6605's effects for each of Knob OS's, light, medium and strong.
    static const uint8_t Effects[4][3] = {
        {4, 1, 17},    // Tick: instant click, sharp click, sharp click 2
        {7, 21, 24},   // Bump: light tap, medium click, flash strike
        {1, 21, 24},   // Click: sharp click, medium click, flash strike
        {13, 15, 47},  // Alert: light pulse, medium alert, alert
    };
    int e = std::min(std::max(static_cast<int>(effect), 0), 3);
    int s = strength < 55 ? 0 : strength < 85 ? 1 : 2;  // the Haptics setting: 40, 70 or 100%
    haptic_write(HapticControl, 0);  // ends the one playing, so quick ticks each start afresh
    haptic_write(HapticEffect, Effects[e][s]);
    haptic_write(HapticControl, 1);
#else
    (void)effect;
    (void)strength;
#endif
}

void hal_set_leds(const uint32_t* colors, int count, int arc_start, int arc_sweep) {
    (void)arc_start;  // the LEDs are where they are
    (void)arc_sweep;
    static rmt_data_t bits[MaxLeds * 24];
    count = std::min(count, MaxLeds);
    int n = 0;
    for (int i = 0; i < count; i++) {  // i: the LED's place in the chain
        int k = i;                       // its place in Knob OS's order, clockwise from the top
#ifdef LEDS_REVERSED
        k = count - 1 - k;
#endif
#ifdef LED_AT_TOP
        k = (k - LED_AT_TOP + count) % count;
#endif
        uint32_t rgb = colors[k];
        uint32_t grb = ((rgb >> 8) & 0xFF) << 16 | ((rgb >> 16) & 0xFF) << 8 | (rgb & 0xFF);
        for (int bit = 23; bit >= 0; bit--) {
            bool one = (grb >> bit) & 1;
            bits[n].level0 = 1;
            bits[n].duration0 = one ? 8 : 4;
            bits[n].level1 = 0;
            bits[n].duration1 = one ? 4 : 8;
            n++;
        }
    }
    rmtWrite(PIN_LED_RGB, bits, n, RMT_WAIT_FOR_EVER);
}

// Files in the flash's file system (LittleFS, in the partition table's data partition).
bool hal_store_read(const char* name, std::vector<uint8_t>& out) {
    File f = LittleFS.open(String("/") + name, "r");
    if (!f) return false;
    out.resize(f.size());
    bool ok = f.read(out.data(), out.size()) == out.size();
    f.close();
    return ok;
}

void hal_store_write(const char* name, const std::vector<uint8_t>& data) {
    File f = LittleFS.open(String("/") + name, "w");
    if (!f) return;
    f.write(data.data(), data.size());
    f.close();
}

// --- USB ---
// Knob OS's USB serial port, named after the knob ("Smart Knob") for Windows: the device and its serial port both carry
// the knob's own name. USB starts here, once the name is read (the core would start it before setup, with fixed names:
// ARDUINO_USB_CDC_ON_BOOT=0). A new name shows from the next restart.
std::string usb_name;  // TinyUSB keeps the pointer, so this lives on

// The name saved in the knob's settings (settings.cpp), in plain ASCII (USB strings here can't carry more).
std::string saved_name() {
    std::vector<uint8_t> data;
    JsonDocument doc;
    std::string name;
    if (hal_store_read("settings.json", data) && !deserializeJson(doc, data.data(), data.size()))
        for (const char* c = doc["name"] | ""; *c && name.size() < 60; c++)
            if (*c >= 0x20 && *c < 0x7F) name += *c;
    while (!name.empty() && name.back() == ' ') name.pop_back();
    return name.empty() ? USB_PRODUCT : name;
}

// The serial port's interface, as the core's own (USBCDC.cpp) but with the knob's name instead of "TinyUSB CDC".
uint16_t load_serial_descriptor(uint8_t* dst, uint8_t* itf) {
    uint8_t name = tinyusb_add_string_descriptor(usb_name.c_str());
    uint8_t descriptor[TUD_CDC_DESC_LEN] = {TUD_CDC_DESCRIPTOR(*itf, name, 0x85, CFG_TUD_ENDPOINT_SIZE, 0x03, 0x84, CFG_TUD_ENDPOINT_SIZE)};
    *itf += 2;
    memcpy(dst, descriptor, TUD_CDC_DESC_LEN);
    return TUD_CDC_DESC_LEN;
}

void start_usb() {
    usb_name = saved_name();
    tinyusb_enable_interface(USB_INTERFACE_CDC, TUD_CDC_DESC_LEN, load_serial_descriptor);
    usb_serial = new USBCDC(0);  // its own interface is refused ("already enabled"): the one above names the port
    usb_serial->setRxBufferSize(16 * 1024);  // before begin, which would make it 256 bytes
    usb_serial->begin(115200);
    // Knob Hub restarts the knob into its ROM loader (to install Knob OS) by opening the port at 1200 baud. The core
    // would also restart into it on esptool's DTR/RTS sequence, but Windows plays that by itself when a port opened
    // with DTR on and RTS off is closed: Knob Hub quitting would leave the knob in its loader. So only 1200 baud does.
    usb_serial->enableReboot(false);
    usb_serial->onEvent(ARDUINO_USB_CDC_LINE_CODING_EVENT, [](void*, esp_event_base_t, int32_t, void* data) {
        if (static_cast<arduino_usb_cdc_event_data_t*>(data)->line_coding.bit_rate == 1200)
            usb_persist_restart(RESTART_BOOTLOADER);
    });
    USB.productName(usb_name.c_str());
    USB.begin();
}

void setup() {
#ifdef PIN_POWER_1
    pinMode(PIN_POWER_1, OUTPUT);
    digitalWrite(PIN_POWER_1, HIGH);
#endif
#ifdef PIN_POWER_2
    pinMode(PIN_POWER_2, OUTPUT);
    digitalWrite(PIN_POWER_2, HIGH);
#endif
    LittleFS.begin(true);  // formats it the first time
    start_usb();           // after the store: it reads the knob's name

    start_backlight();
    start_leds();
    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return millis(); });
    start_display();
    if (hal_board().touch) start_touch();
    start_knob();
    start_vibration();  // before Knob OS tells Knob Hub what the board has
    knob_os_start();
#ifdef WIFI_TX_POWER
    net::start(WIFI_TX_POWER);  // after Knob OS has loaded the knob's name, its name on the network
#else
    net::start();
#endif
    xTaskCreatePinnedToCore(read_link, "link", 4096, nullptr, 5, nullptr, 0);  // the main loop is on core 1
}

void loop() {
    poll_link();
    poll_knob();
    uint32_t wait = lv_timer_handler();
    delay(std::min<uint32_t>(std::max<uint32_t>(wait, 1), 5));
}
