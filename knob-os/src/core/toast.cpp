#include "toast.h"

#include "haptics.h"
#include "idle.h"
#include "theme.h"
#include "ui.h"

namespace {

lv_obj_t* current = nullptr;

void forget_toast(lv_event_t* e) {
    if (lv_event_get_target(e) == current) current = nullptr;
}

}  // namespace

void toast_show(const char* text) {
    idle::wake();
    if (current) lv_obj_delete(current);

    lv_obj_t* box = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(box);
    current = box;
    lv_obj_set_size(box, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(box, theme::color(theme::SurfaceRaised), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(box, theme::color(theme::Border), 0);
    lv_obj_set_style_border_width(box, ui::px(1), 0);
    lv_obj_set_style_radius(box, ui::px(11), 0);
    lv_obj_set_style_pad_hor(box, ui::px(12), 0);
    lv_obj_set_style_pad_ver(box, ui::px(8), 0);
    lv_obj_align(box, LV_ALIGN_CENTER, 0, ui::px(56));

    lv_obj_t* label = lv_label_create(box);
    lv_label_set_text(label, text);
    lv_obj_set_style_max_width(label, ui::px(150), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, theme::color(theme::Text), 0);
    lv_obj_set_style_text_font(label, ui::font(14), 0);

    lv_obj_add_event_cb(box, forget_toast, LV_EVENT_DELETE, nullptr);
    lv_obj_fade_out(box, 300, 1600);
    lv_obj_delete_delayed(box, 1950);
    haptics::play(haptics::Effect::Alert);
}
