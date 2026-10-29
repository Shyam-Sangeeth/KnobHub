// "media": what's playing, e.g. Knob Hub's Media app.
//   title, artist, source (the app playing, optional), playing (bool), position / duration (seconds; the edge arc),
//   can_seek, can_previous, can_next (bool, default true), color,
//   art {w, h, rgb565: base64 of w*h little-endian RGB565 pixels, screen: the screen size it was drawn for} (the album
//   art, up to 80 x 80 design px; {} goes back to the icon),
//   title_image / artist_image (a TextPicture, for text the knob's fonts can't draw; {} goes back to the text)
// Turning seeks 5 s per step: the knob shows the new position right away and reports rotate +1/-1. Pressing or tapping
// the play button reports tap "play"; the other buttons report tap "previous" / "next". While playing, the position
// moves on by itself between updates.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "templates.h"
#include "text_picture.h"
#include "theme.h"
#include "ui.h"

namespace {

constexpr uint32_t ArcTrack = 0x16181C;
constexpr uint32_t PlayIcon = 0x101114;
constexpr int ArtSize = 80;  // design px
constexpr int SeekStep = 5;            // seconds per click of the knob
constexpr uint32_t SeekShowMs = 1500;  // how long the position stays up after turning
constexpr int ArcRange = 1000;
constexpr int TitleY = 122, ArtistY = 143;  // design px
constexpr int TitleWidth = 190, ArtistWidth = 190;  // the round screen's width there; longer text scrolls round (design px)

class MediaScreen : public TemplateScreen {
public:
    MediaScreen(JsonObjectConst data, TemplateSink sink) : TemplateScreen(ui::screen_root(screens::parent()), std::move(sink)) {
        lv_obj_t* r = root();
        progress_ = ui::arc(r, ui::px(232), ui::px(3), 270, 360, ArcTrack, color_);
        lv_arc_set_range(progress_, 0, ArcRange);
        lv_obj_center(progress_);

        art_ = ui::plain(r);
        lv_obj_set_size(art_, ui::px(ArtSize), ui::px(ArtSize));
        lv_obj_set_pos(art_, (ui::size() - ui::px(ArtSize)) / 2, ui::px(34));
        lv_obj_set_style_radius(art_, ui::px(10), 0);
        lv_obj_set_style_clip_corner(art_, true, 0);
        lv_obj_set_style_bg_color(art_, theme::color(theme::SurfaceRaised), 0);
        lv_obj_set_style_bg_opa(art_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(art_, ui::px(1), 0);
        lv_obj_set_style_border_color(art_, theme::color(theme::Border), 0);
        lv_obj_set_style_border_post(art_, true, 0);  // the border goes over the picture
        art_icon_ = ui::label(art_, LV_SYMBOL_AUDIO, ui::font(28), color_);
        lv_obj_center(art_icon_);
        art_image_ = lv_image_create(art_);
        lv_obj_center(art_image_);
        lv_obj_add_flag(art_image_, LV_OBJ_FLAG_HIDDEN);

        source_ = ui::label(r, "", ui::font(12), theme::TextMuted);
        lv_obj_align(source_, LV_ALIGN_TOP_MID, 0, ui::px(16));
        title_ = ui::label(r, "", ui::font(16), theme::Text);
        lv_obj_align(title_, LV_ALIGN_TOP_MID, 0, ui::px(TitleY));
        artist_ = ui::label(r, "", ui::font(12), theme::TextSecondary);
        lv_obj_align(artist_, LV_ALIGN_TOP_MID, 0, ui::px(ArtistY));
        title_picture_ = std::make_unique<TextPicture>(r, ui::px(TitleWidth));
        title_picture_->set_color(theme::Text);
        artist_picture_ = std::make_unique<TextPicture>(r, ui::px(ArtistWidth));
        artist_picture_->set_color(theme::TextSecondary);

        lv_obj_t* row = ui::plain(r);
        lv_obj_set_size(row, ui::px(160), ui::px(40));
        lv_obj_align(row, LV_ALIGN_TOP_MID, 0, ui::px(166));
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, ui::px(14), 0);
        previous_ = button(row, "previous", LV_SYMBOL_PREV, ui::px(26), false);
        play_ = button(row, "play", LV_SYMBOL_PLAY, ui::px(36), true);
        next_ = button(row, "next", LV_SYMBOL_NEXT, ui::px(26), false);

        tick_ = lv_timer_create(on_tick, 500, this);
        update(data);
    }

    ~MediaScreen() override {
        lv_timer_delete(tick_);
        if (seek_timer_) lv_timer_delete(seek_timer_);
        lv_image_cache_drop(&art_dsc_);  // the pixels go with this screen
    }

    void input(KnobInput in) override {
        switch (in) {
            case KnobInput::TurnRight: seek(1); break;
            case KnobInput::TurnLeft: seek(-1); break;
            case KnobInput::Press: tap("play"); break;
            case KnobInput::Back: emit("back"); break;
        }
    }

    // How far the song has played as the level meter.
    lights::Glow glow() const override {
        int played = duration_ > 0 ? static_cast<int>(position() * 100 / duration_) : -1;
        return {color_, played > 100 ? 100 : played};
    }

    // Seeking: endless fine clicks.
    haptics::Feel feel() const override {
        haptics::Feel f;
        f.detent = haptics::Detent::Fine;
        return f;
    }

    void update(JsonObjectConst data) override {
        if (data["color"].is<const char*>()) {
            color_ = ui::parse_color(data["color"], color_);
            lv_obj_set_style_arc_color(progress_, theme::color(color_), LV_PART_INDICATOR);
            lv_obj_set_style_text_color(art_icon_, theme::color(color_), 0);
        }
        if (data["title"].is<const char*>()) set_text(title_, data["title"], ui::px(TitleWidth));
        if (data["artist"].is<const char*>()) artist_text_ = data["artist"].as<const char*>();
        if (data["source"].is<const char*>()) {
            lv_label_set_text(source_, data["source"]);
            ui::one_line(source_, ui::px(120));
        }
        if (data["playing"].is<bool>()) {
            set_position(position());  // keep the position where it is when playing stops or starts
            playing_ = data["playing"];
        }
        if (data["duration"].is<double>()) duration_ = std::max(0.0, data["duration"].as<double>());
        // Not while the knob is turning: the app is still catching up with the seek.
        if (data["position"].is<double>() && !seek_timer_) set_position(data["position"].as<double>());
        if (data["can_seek"].is<bool>()) can_seek_ = data["can_seek"];
        if (data["can_previous"].is<bool>()) set_enabled(previous_, data["can_previous"]);
        if (data["can_next"].is<bool>()) set_enabled(next_, data["can_next"]);
        if (data["art"].is<JsonObjectConst>()) set_art(data["art"]);
        if (data["title_image"].is<JsonObjectConst>()) title_picture_->set(data["title_image"]);
        if (data["artist_image"].is<JsonObjectConst>()) artist_picture_->set(data["artist_image"]);
        bool title_picture = title_picture_->has();
        if (title_picture) lv_obj_add_flag(title_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(title_, LV_OBJ_FLAG_HIDDEN);
        title_picture_->show(title_picture, ui::size() / 2, ui::px(TitleY) + lv_font_get_line_height(ui::font(16)) / 2);
        show_playing();
        show_position();
    }

private:
    lv_obj_t* button(lv_obj_t* parent, const char* id, const char* symbol, int size, bool main) {
        lv_obj_t* b = ui::circle(parent, size, main ? theme::Text : theme::Surface, theme::Border, main ? 0 : ui::px(1));
        touchable(b, id);
        lv_obj_t* icon = ui::label(b, symbol, main ? ui::font(16) : ui::font(12),
                                   main ? PlayIcon : theme::Text);
        lv_obj_center(icon);
        return b;
    }

    void touchable(lv_obj_t* obj, const char* id) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
        ui::press_feedback(obj, 88);
        lv_obj_set_user_data(obj, const_cast<char*>(id));
        lv_obj_add_event_cb(obj, on_tap, LV_EVENT_CLICKED, this);
    }

    static void set_enabled(lv_obj_t* b, bool enabled) {
        lv_obj_set_style_opa(b, enabled ? LV_OPA_COVER : LV_OPA_40, 0);
        if (enabled) lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        else lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
    }

    static bool enabled(lv_obj_t* b) { return lv_obj_has_flag(b, LV_OBJ_FLAG_CLICKABLE); }

    void tap(const std::string& id) {
        if (id == "play") {
            set_position(position());
            playing_ = !playing_;  // right away; the app confirms
            show_playing();
        } else if (!enabled(id == "next" ? next_ : previous_)) {
            return;
        }
        emit("tap", id);
    }

    static void on_tap(lv_event_t* e) {
        auto* self = static_cast<MediaScreen*>(lv_event_get_user_data(e));
        auto* obj = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
        self->tap(static_cast<const char*>(lv_obj_get_user_data(obj)));
    }

    // --- Position ---

    // The position now: the last known one plus the time played since.
    double position() const {
        double p = base_position_;
        if (playing_) p += (lv_tick_get() - base_tick_) / 1000.0;
        return duration_ > 0 ? std::min(p, duration_) : p;
    }

    void set_position(double seconds) {
        base_position_ = std::max(0.0, seconds);
        base_tick_ = lv_tick_get();
    }

    void seek(int delta) {
        if (!can_seek_ || duration_ <= 0) return;
        set_position(std::min(duration_, std::max(0.0, position() + delta * SeekStep)));
        if (seek_timer_) {
            lv_timer_reset(seek_timer_);
        } else {
            seek_timer_ = lv_timer_create(on_seek_done, SeekShowMs, this);
            lv_timer_set_repeat_count(seek_timer_, 1);
        }
        show_position();
        emit("rotate", "", delta);
    }

    static void on_seek_done(lv_timer_t* t) {
        auto* self = static_cast<MediaScreen*>(lv_timer_get_user_data(t));
        self->seek_timer_ = nullptr;  // a one-shot timer deletes itself
        self->show_position();
    }

    static void on_tick(lv_timer_t* t) { static_cast<MediaScreen*>(lv_timer_get_user_data(t))->show_position(); }

    static std::string clock(double seconds) {
        int s = static_cast<int>(seconds);
        char text[16];
        if (s >= 3600) std::snprintf(text, sizeof text, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
        else std::snprintf(text, sizeof text, "%d:%02d", s / 60, s % 60);
        return text;
    }

    // The arc, and the line under the title: the artist (as text or Knob Hub's picture of it), or while seeking the
    // position.
    void show_position() {
        double p = position();
        lv_arc_set_value(progress_, duration_ > 0 ? static_cast<int>(std::lround(p / duration_ * ArcRange)) : 0);
        bool picture = !seek_timer_ && artist_picture_->has();
        artist_picture_->show(picture, ui::size() / 2, ui::px(ArtistY) + lv_font_get_line_height(ui::font(12)) / 2);
        if (picture) lv_obj_add_flag(artist_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(artist_, LV_OBJ_FLAG_HIDDEN);
        if (seek_timer_) {
            lv_obj_set_style_text_color(artist_, theme::color(color_), 0);
            set_text(artist_, (clock(p) + " / " + clock(duration_)).c_str(), ui::px(ArtistWidth));
        } else {
            lv_obj_set_style_text_color(artist_, theme::color(theme::TextSecondary), 0);
            set_text(artist_, artist_text_.c_str(), ui::px(ArtistWidth));
        }
    }

    // Only when the text changes: setting it again would restart the marquee.
    static void set_text(lv_obj_t* label, const char* text, int max_width) {
        if (std::strcmp(lv_label_get_text(label), text) == 0) return;
        lv_label_set_text(label, text);
        ui::marquee(label, max_width);
    }

    void show_playing() { lv_label_set_text(lv_obj_get_child(play_, 0), playing_ ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY); }

    // --- Album art ---

    void set_art(JsonObjectConst art) {
        int w = art["w"] | 0, h = art["h"] | 0;
        std::vector<uint8_t> pixels = ui::base64_decode(art["rgb565"] | "");
        lv_image_cache_drop(&art_dsc_);
        int shown_w = ui::picture_px(w, art["screen"] | 0), shown_h = ui::picture_px(h, art["screen"] | 0);
        bool ok = w > 0 && h > 0 && shown_w <= ui::px(ArtSize) && shown_h <= ui::px(ArtSize) &&
                  pixels.size() == static_cast<size_t>(w * h * 2);
        if (!ok) {
            art_pixels_.clear();
            lv_obj_add_flag(art_image_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(art_icon_, LV_OBJ_FLAG_HIDDEN);
            return;
        }
        art_pixels_ = std::move(pixels);
        art_dsc_ = {};
        art_dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
        art_dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
        art_dsc_.header.w = w;
        art_dsc_.header.h = h;
        art_dsc_.header.stride = w * 2;
        art_dsc_.data_size = static_cast<uint32_t>(art_pixels_.size());
        art_dsc_.data = art_pixels_.data();
        lv_image_set_src(art_image_, &art_dsc_);
        // Art drawn for another screen size is scaled, about its centre.
        lv_image_set_scale(art_image_, shown_w == w ? LV_SCALE_NONE : static_cast<uint32_t>(LV_SCALE_NONE * shown_w / w));
        lv_obj_center(art_image_);
        lv_obj_remove_flag(art_image_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(art_icon_, LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t* progress_;
    lv_obj_t* art_;
    lv_obj_t* art_icon_;
    lv_obj_t* art_image_;
    lv_obj_t* source_;
    lv_obj_t* title_;
    lv_obj_t* artist_;
    lv_obj_t* previous_;
    lv_obj_t* play_;
    lv_obj_t* next_;
    lv_timer_t* tick_ = nullptr;
    lv_timer_t* seek_timer_ = nullptr;
    std::vector<uint8_t> art_pixels_;
    lv_image_dsc_t art_dsc_ = {};
    std::unique_ptr<TextPicture> title_picture_, artist_picture_;
    std::string artist_text_;
    uint32_t color_ = theme::Accent;
    double duration_ = 0;
    double base_position_ = 0;
    uint32_t base_tick_ = 0;
    bool playing_ = false;
    bool can_seek_ = true;
};

}  // namespace

TemplateScreen* make_media(JsonObjectConst data, TemplateSink sink) { return new MediaScreen(data, std::move(sink)); }
