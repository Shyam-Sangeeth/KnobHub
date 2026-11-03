// "cards": one item at a time, drawn like an app's page on Home, e.g. the displays to pick the main one from.
//   color (the default for the items), items [{id, label, detail (under the label, optional), icon, color (optional),
//   selected (the current one: a green tick on its icon)}], focus (item id, optional), empty_text (shown when there
//   are no items)
// Turning goes to the next or previous item (it slides in; no wrap round), swiping does the same; pressing or tapping
// reports press (item). An update keeps the item on show (the selected one if it's gone) unless it sends focus. Long
// press: back.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "templates.h"
#include "theme.h"
#include "ui.h"

namespace {

constexpr uint32_t PeekIcon = 0xC9CDD4;
constexpr uint32_t Tick = 0x4ADE80;
constexpr int SlideMs = 220;
constexpr int Circle = 76, IconSize = 28, PeekIconSize = 12, TickSize = 24;  // design px, Circle and IconSize as Home's
constexpr int NameWidth = 196;  // what fits across the card at the name's height, clear of the edge (design px)

struct Card {
    std::string id, label, detail, icon;
    uint32_t color = theme::Accent;
    bool selected = false;
};

class CardsScreen : public TemplateScreen {
public:
    CardsScreen(JsonObjectConst data, TemplateSink sink) : TemplateScreen(ui::screen_root(screens::parent()), std::move(sink)) {
        lv_obj_set_style_bg_opa(root(), LV_OPA_TRANSP, 0);
        build_overlay();
        read(data);
        show(focus_, 0);
        lv_obj_add_event_cb(root(), on_gesture, LV_EVENT_GESTURE, this);
    }

    ~CardsScreen() override { cancel_pulse(); }

    void update(JsonObjectConst data) override {
        read(data);
        show(focus_, 0);
    }

    void input(KnobInput in) override {
        switch (in) {
            case KnobInput::TurnRight: step(1); break;
            case KnobInput::TurnLeft: step(-1); break;
            case KnobInput::Press:
                if (cards_.empty()) break;
                pulse();
                emit("press", cards_[focus_].id);
                break;
            case KnobInput::Back: emit("back"); break;
        }
    }

    lights::Glow glow() const override {
        lights::Glow g;
        if (!cards_.empty()) g.color = cards_[focus_].color;
        return g;
    }

    // A firm click per card, as on Home, with end stops at the first and the last.
    haptics::Feel feel() const override {
        haptics::Feel f;
        f.positions = std::max<int>(1, count());
        f.position = focus_;
        f.detent = haptics::Detent::Firm;
        return f;
    }

private:
    int count() const { return static_cast<int>(cards_.size()); }

    void read(JsonObjectConst d) {
        if (d["color"].is<const char*>()) color_ = ui::parse_color(d["color"], color_);
        if (d["empty_text"].is<const char*>()) empty_text_ = d["empty_text"].as<const char*>();
        std::string keep = cards_.empty() ? "" : cards_[focus_].id;
        if (d["items"].is<JsonArrayConst>()) {
            cards_.clear();
            for (JsonObjectConst item : d["items"].as<JsonArrayConst>()) {
                Card card;
                card.id = item["id"] | "";
                card.label = item["label"] | "";
                card.detail = item["detail"] | "";
                card.icon = item["icon"] | "";
                card.color = ui::parse_color(item["color"], color_);
                card.selected = item["selected"] | false;
                cards_.push_back(card);
            }
            // Stay on the item on show; at first, or when it's gone, go to the current one.
            auto at = [&](auto match) {
                auto it = std::find_if(cards_.begin(), cards_.end(), match);
                return it == cards_.end() ? -1 : static_cast<int>(it - cards_.begin());
            };
            focus_ = at([&](const Card& c) { return !keep.empty() && c.id == keep; });
            if (focus_ < 0) focus_ = std::max(0, at([](const Card& c) { return c.selected; }));
        }
        if (d["focus"].is<const char*>()) {
            std::string id = d["focus"].as<const char*>();
            for (size_t i = 0; i < cards_.size(); i++)
                if (cards_[i].id == id) focus_ = static_cast<int>(i);
        }
        focus_ = std::max(0, std::min(focus_, count() - 1));
    }

    // --- A card ---

    lv_obj_t* make_page(int index) {
        lv_obj_t* page = ui::plain(root());
        lv_obj_set_size(page, ui::size(), ui::size());
        if (cards_.empty()) {
            lv_obj_t* text = ui::label(page, empty_text_.c_str(), ui::font(14), theme::TextSecondary);
            lv_obj_set_width(text, ui::px(NameWidth));
            lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_center(text);
            return page;
        }
        const Card& card = cards_[index];
        // The whole page is the button; its icon shows the touch.
        lv_obj_add_flag(page, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(page, on_clicked, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(page, on_touch, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(page, on_touch, LV_EVENT_RELEASED, this);
        lv_obj_add_event_cb(page, on_touch, LV_EVENT_PRESS_LOST, this);

        lv_obj_t* ring = ui::plain(page);
        lv_obj_set_size(ring, ui::px(226), ui::px(226));
        lv_obj_center(ring);
        lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(ring, ui::px(2), 0);
        lv_obj_set_style_border_color(ring, theme::color(card.color), 0);
        lv_obj_set_style_border_opa(ring, LV_OPA_40, 0);

        // The icon, the label and the detail, a fixed gap apart (by the letters, not their lines) and centred, as Home.
        const int circle = ui::px(Circle), gap = ui::px(14), detail_gap = ui::px(10);
        const lv_font_t* name_font = fitting_font(card.label);
        const lv_font_t* detail_font = ui::font(12);
        int height = circle + gap + ui::ink_height(name_font) + (card.detail.empty() ? 0 : detail_gap + ui::ink_height(detail_font));
        int y = (ui::size() - height) / 2;
        lv_obj_t* icon = ui::circle(page, circle, ui::tint(card.color), card.color, ui::px(2));
        lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, y);
        ui::press_feedback(icon, 88);
        lv_obj_set_user_data(page, icon);
        lv_obj_center(ui::icon(icon, card.icon, ui::px(IconSize), theme::Text));
        if (card.selected) {
            // On the circle's edge at the bottom right, ringed in the screen's colour so it stands off the circle.
            const int tick = ui::px(TickSize), offset = static_cast<int>(std::lround(circle / 2.0 * std::sqrt(0.5)));
            lv_obj_t* badge = ui::circle(page, tick, Tick, theme::Screen, ui::px(2));
            lv_obj_set_pos(badge, ui::size() / 2 + offset - tick / 2, y + circle / 2 + offset - tick / 2);
            lv_obj_center(ui::icon(badge, "check", ui::px(14), theme::Screen));
        }
        y += circle + gap;

        lv_obj_t* name = ui::label(page, card.label.c_str(), name_font, theme::Text);
        ui::one_line(name, ui::px(NameWidth));
        lv_obj_align(name, LV_ALIGN_TOP_MID, 0, y - ui::ink_top(name_font));
        if (card.detail.empty()) return page;
        y += ui::ink_height(name_font) + detail_gap;
        lv_obj_t* detail = ui::label(page, card.detail.c_str(), detail_font, theme::TextSecondary);
        ui::one_line(detail, ui::px(NameWidth));
        lv_obj_align(detail, LV_ALIGN_TOP_MID, 0, y - ui::ink_top(detail_font));
        return page;
    }

    // Home's 24 when the label fits across; smaller for a long one (a monitor's model name), "..." past the smallest.
    static const lv_font_t* fitting_font(const std::string& text) {
        for (int size : {24, 20}) {
            if (lv_text_get_width(text.c_str(), static_cast<uint32_t>(text.size()), ui::font(size), 0) <= ui::px(NameWidth))
                return ui::font(size);
        }
        return ui::font(16);
    }

    lv_obj_t* page_icon() const { return page_ ? static_cast<lv_obj_t*>(lv_obj_get_user_data(page_)) : nullptr; }

    // From the knob (no finger on the screen) the icon gets a quick press of its own.
    void pulse() {
        cancel_pulse();
        lv_obj_t* icon = page_icon();
        if (!icon) return;
        lv_obj_add_state(icon, LV_STATE_PRESSED);
        pulse_timer_ = lv_timer_create(on_pulse_timer, 110, this);
        lv_timer_set_repeat_count(pulse_timer_, 1);
    }

    void cancel_pulse() {
        if (!pulse_timer_) return;
        lv_timer_delete(pulse_timer_);
        pulse_timer_ = nullptr;
        if (lv_obj_t* icon = page_icon()) lv_obj_remove_state(icon, LV_STATE_PRESSED);
    }

    static void on_pulse_timer(lv_timer_t* t) {
        auto* self = static_cast<CardsScreen*>(lv_timer_get_user_data(t));
        self->pulse_timer_ = nullptr;  // its only run: the timer deletes itself
        if (lv_obj_t* icon = self->page_icon()) lv_obj_remove_state(icon, LV_STATE_PRESSED);
    }

    static void on_clicked(lv_event_t* e) {
        auto* self = static_cast<CardsScreen*>(lv_event_get_user_data(e));
        if (!self->cards_.empty()) self->emit("press", self->cards_[self->focus_].id);
    }

    static void on_touch(lv_event_t* e) {
        auto* page = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
        auto* icon = static_cast<lv_obj_t*>(lv_obj_get_user_data(page));
        if (!icon) return;
        if (lv_event_get_code(e) == LV_EVENT_PRESSED) lv_obj_add_state(icon, LV_STATE_PRESSED);
        else lv_obj_remove_state(icon, LV_STATE_PRESSED);
    }

    // --- Overlay: dots for the items and the neighbours peeking in at the edges, as on Home ---

    void build_overlay() {
        overlay_ = ui::plain(root());
        const int size = ui::size(), peek_size = ui::px(24), edge = ui::px(11);
        lv_obj_set_size(overlay_, size, size);
        for (int side = 0; side < 2; side++) {
            lv_obj_t* peek = ui::circle(overlay_, peek_size, theme::Surface, theme::Border, ui::px(1));
            lv_obj_set_pos(peek, side == 0 ? edge : size - edge - peek_size, size / 2 - peek_size / 2);
            lv_obj_set_style_opa(peek, LV_OPA_40, 0);
            peeks_[side] = peek;
        }
    }

    void update_overlay() {
        for (lv_obj_t* dot : dots_) lv_obj_delete(dot);
        dots_.clear();
        // Dots along the bottom arc, the first item on the left; none for a single item.
        constexpr double StepDegrees = 8.5;
        const double radius = 98.0 * ui::size() / ui::DesignSize;
        for (int i = 0; count() > 1 && i < count(); i++) {
            bool on = i == focus_;
            int size = ui::px(on ? 7 : 5);
            double degrees = 180 - (i - (count() - 1) / 2.0) * StepDegrees;
            double rad = degrees * 3.14159265 / 180;
            lv_obj_t* dot = ui::plain(overlay_);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(dot, theme::color(on ? theme::Text : theme::DotOff), 0);
            lv_obj_set_size(dot, size, size);
            lv_obj_set_pos(dot, static_cast<int>(std::lround(ui::size() / 2 + radius * std::sin(rad))) - size / 2,
                           static_cast<int>(std::lround(ui::size() / 2 - radius * std::cos(rad))) - size / 2);
            dots_.push_back(dot);
        }
        for (int side = 0; side < 2; side++) {
            int neighbour = focus_ + (side == 0 ? -1 : 1);
            bool shown = neighbour >= 0 && neighbour < count();
            if (shown) lv_obj_remove_flag(peeks_[side], LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(peeks_[side], LV_OBJ_FLAG_HIDDEN);
            std::string icon = shown ? cards_[neighbour].icon : "";
            if (peek_icons_[side] && peek_ids_[side] == icon) continue;
            if (peek_icons_[side]) lv_obj_delete(peek_icons_[side]);
            peek_icons_[side] = shown ? ui::icon(peeks_[side], icon, ui::px(PeekIconSize), PeekIcon) : nullptr;
            if (peek_icons_[side]) lv_obj_center(peek_icons_[side]);
            peek_ids_[side] = icon;
        }
    }

    // --- Changing cards ---

    static void delete_after_slide(lv_anim_t* a) { lv_obj_delete(static_cast<lv_obj_t*>(a->var)); }

    static void slide(lv_obj_t* obj, int from, int to, bool delete_when_done) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, obj);
        lv_anim_set_values(&a, from, to);
        lv_anim_set_duration(&a, SlideMs);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_set_exec_cb(&a, [](void* var, int32_t x) { lv_obj_set_x(static_cast<lv_obj_t*>(var), x); });
        if (delete_when_done) lv_anim_set_completed_cb(&a, delete_after_slide);
        lv_anim_start(&a);
    }

    // direction: +1 = the next card (it comes in from the right), -1 = the previous, 0 = no animation.
    void show(int index, int direction) {
        cancel_pulse();
        lv_obj_t* old = page_;
        focus_ = index;
        page_ = make_page(index);
        lv_obj_move_foreground(overlay_);
        update_overlay();
        if (old && direction != 0) {
            slide(page_, direction * ui::size(), 0, false);
            slide(old, lv_obj_get_x(old), -direction * ui::size(), true);
        } else if (old) {
            lv_anim_delete(old, nullptr);  // it may still be sliding
            lv_obj_delete(old);
        }
    }

    void step(int delta) {
        int index = focus_ + delta;
        if (index < 0 || index >= count()) return;  // an end stop
        show(index, delta);
    }

    static void on_gesture(lv_event_t* e) {
        auto* self = static_cast<CardsScreen*>(lv_event_get_user_data(e));
        lv_indev_t* indev = lv_indev_active();
        if (!indev) return;
        lv_dir_t dir = lv_indev_get_gesture_dir(indev);
        if (dir == LV_DIR_LEFT) self->step(1);
        else if (dir == LV_DIR_RIGHT) self->step(-1);
        else return;
        lv_indev_wait_release(indev);
    }

    uint32_t color_ = theme::Accent;
    std::string empty_text_;
    std::vector<Card> cards_;
    int focus_ = 0;
    lv_obj_t* page_ = nullptr;
    lv_obj_t* overlay_ = nullptr;
    std::vector<lv_obj_t*> dots_;
    lv_obj_t* peeks_[2] = {};
    lv_obj_t* peek_icons_[2] = {};
    std::string peek_ids_[2];
    lv_timer_t* pulse_timer_ = nullptr;
};

}  // namespace

TemplateScreen* make_cards(JsonObjectConst data, TemplateSink sink) { return new CardsScreen(data, std::move(sink)); }
