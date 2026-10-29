#include "text_picture.h"

#include "theme.h"
#include "ui.h"

namespace {
constexpr int MaxWidth = 1024, MaxHeight = 64;
constexpr int Gap = 24;  // design px between the end of the text and its start coming round again
}

TextPicture::TextPicture(lv_obj_t* parent, int max_width) : max_width_(max_width), box_(ui::plain(parent)) {
    lv_obj_add_flag(box_, LV_OBJ_FLAG_HIDDEN);
    image_ = lv_image_create(box_);
    copy_ = lv_image_create(box_);
    for (lv_obj_t* image : {image_, copy_}) {
        lv_obj_set_style_image_recolor_opa(image, LV_OPA_COVER, 0);
        lv_image_set_pivot(image, 0, 0);  // scaled from its top left, where it's placed
    }
}

TextPicture::~TextPicture() {
    stop();
    lv_image_cache_drop(&dsc_);  // the objects themselves go with their screen
}

bool TextPicture::set(JsonObjectConst picture) {
    int w = picture["w"] | 0, h = picture["h"] | 0;
    std::vector<uint8_t> pixels = ui::base64_decode(picture["a8"] | "");
    stop();
    shown_ = false;
    lv_image_cache_drop(&dsc_);
    if (w <= 0 || h <= 0 || w > MaxWidth || h > MaxHeight || pixels.size() != static_cast<size_t>(w * h)) {
        pixels_.clear();
        lv_obj_add_flag(box_, LV_OBJ_FLAG_HIDDEN);
        return false;
    }
    pixels_ = std::move(pixels);
    dsc_ = {};
    dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc_.header.cf = LV_COLOR_FORMAT_A8;
    dsc_.header.w = w;
    dsc_.header.h = h;
    dsc_.header.stride = w;
    dsc_.data_size = static_cast<uint32_t>(pixels_.size());
    dsc_.data = pixels_.data();
    w_ = ui::picture_px(w, picture["screen"] | 0);
    h_ = ui::picture_px(h, picture["screen"] | 0);
    for (lv_obj_t* image : {image_, copy_}) {
        lv_image_set_src(image, &dsc_);
        lv_image_set_scale(image, w_ == w ? LV_SCALE_NONE : static_cast<uint32_t>(LV_SCALE_NONE * w_ / w));
    }
    return true;
}

void TextPicture::set_color(uint32_t color) {
    for (lv_obj_t* image : {image_, copy_}) lv_obj_set_style_image_recolor(image, theme::color(color), 0);
}

void TextPicture::show(bool visible, int center_x, int center_y) {
    if (!visible || !has()) {
        stop();
        shown_ = false;
        lv_obj_add_flag(box_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (shown_) return;  // already on show (and maybe scrolling)
    shown_ = true;
    int w = w_, h = h_;
    int box_width = w < max_width_ ? w : max_width_;
    lv_obj_set_size(box_, box_width, h);
    lv_obj_set_pos(box_, center_x - box_width / 2, center_y - h / 2);
    lv_obj_set_pos(image_, 0, 0);
    lv_obj_remove_flag(box_, LV_OBJ_FLAG_HIDDEN);
    if (w <= max_width_) {
        lv_obj_add_flag(copy_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    // Scroll round: the picture moves left by its width and the gap while the copy comes in behind it.
    lv_obj_remove_flag(copy_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(copy_, w + ui::px(Gap), 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, image_);
    lv_anim_set_user_data(&a, this);
    lv_anim_set_custom_exec_cb(&a, scroll);
    lv_anim_set_values(&a, 0, -(w + ui::px(Gap)));
    lv_anim_set_duration(&a, static_cast<uint32_t>((w + ui::px(Gap)) * 1000 / ui::px(ui::MarqueeSpeed)));
    lv_anim_set_delay(&a, ui::MarqueePauseMs);
    lv_anim_set_repeat_delay(&a, ui::MarqueePauseMs);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

void TextPicture::scroll(lv_anim_t* a, int32_t x) {
    auto* self = static_cast<TextPicture*>(lv_anim_get_user_data(a));
    lv_obj_set_x(self->image_, x);
    lv_obj_set_x(self->copy_, x + self->w_ + ui::px(Gap));
}

void TextPicture::stop() { lv_anim_delete(image_, nullptr); }
