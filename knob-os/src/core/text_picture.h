#pragma once
#include <ArduinoJson.h>
#include <lvgl.h>

#include <cstdint>
#include <vector>

// Text that Knob Hub drew because the knob's fonts can't (Malayalam, Hindi, Japanese...): an alpha-only picture shown
// in a colour, in place of a label. Sent as {w, h, a8: base64 of w*h alpha bytes, row by row}; anything else, such
// as {}, means "no picture: use the label". A picture wider than the space scrolls round like ui::marquee.
class TextPicture {
public:
    TextPicture(lv_obj_t* parent, int max_width);
    ~TextPicture();
    TextPicture(const TextPicture&) = delete;
    TextPicture& operator=(const TextPicture&) = delete;

    // Takes a new picture (or none). Returns whether there is one.
    bool set(JsonObjectConst picture);
    bool has() const { return !pixels_.empty(); }
    void set_color(uint32_t color);
    // Shows it centred at (center_x, center_y) in its parent, or hides it. Showing it again keeps it scrolling.
    void show(bool visible, int center_x, int center_y);

private:
    static void scroll(lv_anim_t* a, int32_t x);
    void stop();

    int max_width_;
    lv_obj_t* box_;     // clips the picture to max_width_
    lv_obj_t* image_;
    lv_obj_t* copy_;    // follows the picture round while it scrolls
    std::vector<uint8_t> pixels_;
    lv_image_dsc_t dsc_ = {};
    bool shown_ = false;
};
