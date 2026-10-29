#include "home.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "apps.h"
#include "hal.h"
#include "icons.h"
#include "settings.h"
#include "theme.h"
#include "ui.h"

namespace {

enum class PageKind { Clock, App, Settings };

struct Page {
    PageKind kind;
    AppInfo app;  // for App pages; name/icon/colour for the others too
};

constexpr int SlideMs = 220;
constexpr uint32_t PeekIcon = 0xC9CDD4;
constexpr int PageIconSize = 28, PeekIconSize = 12;  // design sizes

class HomeScreen;
HomeScreen* home = nullptr;

class HomeScreen : public Screen {
public:
    HomeScreen() : Screen(ui::screen_root(screens::parent())) {
        lv_obj_set_style_bg_opa(root(), LV_OPA_TRANSP, 0);
        build_overlay();
        rebuild_pages();
        show_page(0, 0);
        lv_obj_add_event_cb(root(), on_gesture, LV_EVENT_GESTURE, this);
        timer_ = lv_timer_create(on_tick, 500, this);
        home = this;
    }

    ~HomeScreen() override {
        lv_timer_delete(timer_);
        if (open_timer_) lv_timer_delete(open_timer_);
        home = nullptr;
    }

    void input(KnobInput in) override {
        switch (in) {
            case KnobInput::TurnRight: step(1); break;
            case KnobInput::TurnLeft: step(-1); break;
            case KnobInput::Press: open_soon(true); break;
            case KnobInput::Back: if (current_ != 0) show_page(0, -1); break;  // back to the first page
        }
    }

    // The page in focus's colour.
    lights::Glow glow() const override {
        lights::Glow g;
        if (!pages_.empty()) g.color = pages_[current_].app.color;
        return g;
    }

    // A firm click per page, round and round.
    haptics::Feel feel() const override {
        haptics::Feel f;
        f.positions = count();
        f.position = current_;
        f.wrap = true;
        f.detent = haptics::Detent::Firm;
        return f;
    }

    void refresh() {
        std::string id = current_id();
        rebuild_pages();
        rebuild_dots();
        select(id);
    }

    std::string current_id() const { return pages_.empty() ? "" : pages_[current_].app.id; }

    void select(const std::string& id) {
        cancel_open();
        int index = 0;
        for (size_t i = 0; i < pages_.size(); i++)
            if (pages_[i].app.id == id) index = static_cast<int>(i);
        show_page(index, 0);
    }

private:
    // In Knob Hub's order (Clock and Settings included); until it sends one, Clock first and Settings last. The first
    // page is where Home starts and where a long press goes back to.
    void rebuild_pages() {
        const Page clock{PageKind::Clock, {"clock", "Clock", "", "clock", theme::Text}};
        const Board& board = hal_board();
        const char* groups = board.leds > 0 ? "Display, Lights, Connection"
                           : board.vibration || board.motor ? "Display, Haptics, Connection" : "Display, Connection";
        const Page settings{PageKind::Settings, {"settings", "Settings", groups, "settings", theme::Settings}};
        std::vector<Page> rest;
        for (const AppInfo& app : apps::list()) rest.push_back({PageKind::App, app});

        pages_.clear();
        bool has_clock = false, has_settings = false;
        for (const std::string& id : apps::order()) {
            if (id == "clock" && !has_clock) {
                pages_.push_back(clock);
                has_clock = true;
            } else if (id == "settings" && !has_settings) {
                pages_.push_back(settings);
                has_settings = true;
            } else {
                auto app = std::find_if(rest.begin(), rest.end(), [&](const Page& p) { return p.app.id == id; });
                if (app == rest.end()) continue;
                pages_.push_back(*app);
                rest.erase(app);
            }
        }
        if (!has_clock) pages_.insert(pages_.begin(), clock);
        pages_.insert(pages_.end(), rest.begin(), rest.end());  // apps the order left out
        if (!has_settings) pages_.push_back(settings);
        if (!dots_.empty() && dots_.size() != pages_.size()) rebuild_dots();
    }

    int count() const { return static_cast<int>(pages_.size()); }

    // --- Clock ---

    void update_clock() {
        if (!clock_page_) return;
        std::tm now{};
        if (!hal_local_time(now)) {
            lv_label_set_text(clock_time_, "--:--");
            lv_obj_add_flag(clock_ampm_, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(clock_date_, "");
            lv_arc_set_value(clock_seconds_, 0);
            return;
        }
        if (settings::get().clock_24h) {
            lv_label_set_text_fmt(clock_time_, "%02d:%02d", now.tm_hour, now.tm_min);
            lv_obj_add_flag(clock_ampm_, LV_OBJ_FLAG_HIDDEN);
        } else {
            int hour = now.tm_hour % 12;
            lv_label_set_text_fmt(clock_time_, "%d:%02d", hour == 0 ? 12 : hour, now.tm_min);
            lv_label_set_text(clock_ampm_, now.tm_hour < 12 ? "AM" : "PM");
            lv_obj_remove_flag(clock_ampm_, LV_OBJ_FLAG_HIDDEN);
        }
        char weekday[16], month[16];
        std::strftime(weekday, sizeof weekday, "%A", &now);
        std::strftime(month, sizeof month, "%B", &now);
        lv_label_set_text_fmt(clock_date_, "%s, %d %s", weekday, now.tm_mday, month);
        lv_arc_set_value(clock_seconds_, now.tm_sec * 100 / 60);
    }

    void build_clock(lv_obj_t* parent) {
        lv_obj_t* arc = ui::arc(parent, ui::px(232), ui::px(2), 270, 360, theme::Track, theme::Text);
        lv_obj_set_style_arc_opa(arc, LV_OPA_80, LV_PART_INDICATOR);
        lv_obj_center(arc);
        // The time, and AM/PM after it in 12-hour mode; centred together.
        lv_obj_t* row = ui::plain(parent);
        lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
        lv_obj_set_style_pad_column(row, ui::px(5), 0);
        lv_obj_align(row, LV_ALIGN_CENTER, 0, ui::px(-14));
        lv_obj_t* time = ui::label(row, "", ui::font(48), theme::Text);
        clock_ampm_ = ui::label(row, "", ui::font(16), theme::TextSecondary);
        lv_obj_set_style_pad_bottom(clock_ampm_, ui::px(12), 0);  // onto the time's baseline
        lv_obj_t* date = ui::label(parent, "", ui::font(14), theme::TextSecondary);
        lv_obj_align(date, LV_ALIGN_CENTER, 0, ui::px(22));
        clock_page_ = parent;
        clock_time_ = time;
        clock_date_ = date;
        clock_seconds_ = arc;
        lv_obj_add_event_cb(parent, on_clock_deleted, LV_EVENT_DELETE, this);
        update_clock();
    }

    static void on_clock_deleted(lv_event_t* e) {
        auto* self = static_cast<HomeScreen*>(lv_event_get_user_data(e));
        // A page sliding out can be deleted after a new clock page appeared; only forget our own widgets.
        if (lv_event_get_target(e) != self->clock_page_) return;
        self->clock_page_ = self->clock_time_ = self->clock_ampm_ = self->clock_date_ = self->clock_seconds_ = nullptr;
    }

    static void on_tick(lv_timer_t* t) { static_cast<HomeScreen*>(lv_timer_get_user_data(t))->update_clock(); }

    // --- App and Settings pages ---

    void build_app(lv_obj_t* parent, const AppInfo& app) {
        lv_obj_t* ring = ui::plain(parent);
        lv_obj_set_size(ring, ui::px(226), ui::px(226));
        lv_obj_center(ring);
        lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(ring, ui::px(2), 0);
        lv_obj_set_style_border_color(ring, theme::color(app.color), 0);
        lv_obj_set_style_border_opa(ring, LV_OPA_40, 0);

        // The icon, the name and the subtitle, a fixed gap apart (by the letters, not their lines) and centred.
        const int Circle = ui::px(76), Gap = ui::px(14), SubtitleGap = ui::px(10);
        const lv_font_t* name_font = ui::font(24);
        const lv_font_t* subtitle_font = ui::font(12);
        int height = Circle + Gap + ui::ink_height(name_font) + (app.subtitle.empty() ? 0 : SubtitleGap + ui::ink_height(subtitle_font));
        int y = (ui::size() - height) / 2;
        lv_obj_t* icon = ui::circle(parent, Circle, ui::tint(app.color), app.color, ui::px(2));
        lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, y);
        y += Circle + Gap;
        ui::press_feedback(icon, 88);  // pressed along with its page (see on_page_touch)
        lv_obj_set_user_data(parent, icon);
        lv_obj_t* glyph = ui::icon(icon, app.icon, ui::px(PageIconSize), theme::Text);
        lv_obj_center(glyph);

        lv_obj_t* name = ui::label(parent, app.name.c_str(), name_font, theme::Text);
        lv_obj_align(name, LV_ALIGN_TOP_MID, 0, y - ui::ink_top(name_font));
        y += ui::ink_height(name_font) + SubtitleGap;  // the subtitle belongs with the name: a bit closer
        lv_obj_t* subtitle = ui::label(parent, app.subtitle.c_str(), subtitle_font, theme::TextSecondary);
        lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, y - ui::ink_top(subtitle_font));
    }

    // --- Overlay: page dots and the neighbours peeking in at the edges ---

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

    // The icon of the page on that side (0 left, 1 right), made again only when that page changes.
    void show_peek(int side, const AppInfo& app) {
        if (peek_ids_[side] == app.id && peek_icons_[side]) return;
        if (peek_icons_[side]) lv_obj_delete(peek_icons_[side]);
        peek_icons_[side] = ui::icon(peeks_[side], app.icon, ui::px(PeekIconSize), PeekIcon);
        lv_obj_center(peek_icons_[side]);
        peek_ids_[side] = app.id;
    }

    void rebuild_dots() {
        for (lv_obj_t* dot : dots_) lv_obj_delete(dot);
        dots_.clear();
        for (int i = 0; i < count(); i++) {
            lv_obj_t* dot = ui::plain(overlay_);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            dots_.push_back(dot);
        }
    }

    void update_overlay() {
        if (dots_.size() != pages_.size()) rebuild_dots();
        // Dots along the bottom arc, first page on the left.
        constexpr double StepDegrees = 8.5;
        const double radius = 98.0 * ui::size() / ui::DesignSize;
        for (int i = 0; i < count(); i++) {
            bool on = i == current_;
            int size = ui::px(on ? 7 : 5);
            double degrees = 180 - (i - (count() - 1) / 2.0) * StepDegrees;
            double rad = degrees * 3.14159265 / 180;
            int cx = static_cast<int>(std::lround(ui::size() / 2 + radius * std::sin(rad)));
            int cy = static_cast<int>(std::lround(ui::size() / 2 - radius * std::cos(rad)));
            lv_obj_set_size(dots_[i], size, size);
            lv_obj_set_pos(dots_[i], cx - size / 2, cy - size / 2);
            lv_obj_set_style_bg_color(dots_[i], theme::color(on ? theme::Text : theme::DotOff), 0);
        }
        show_peek(0, pages_[(current_ + count() - 1) % count()].app);
        show_peek(1, pages_[(current_ + 1) % count()].app);
    }

    // --- Changing and opening pages ---

    void cancel_open() {
        if (!open_timer_) return;
        lv_timer_delete(open_timer_);
        open_timer_ = nullptr;
        set_icon_pressed(false);
    }

    // Opens the page on show a moment later, so the icon's touch animation is seen first. From the knob (no finger on
    // the screen) the icon gets a quick press of its own.
    void open_soon(bool pulse) {
        if (open_timer_) return;
        if (pulse) set_icon_pressed(true);
        open_step_ = 0;
        open_timer_ = lv_timer_create(on_open_timer, 110, this);
        lv_timer_set_repeat_count(open_timer_, 2);  // 1: let the icon go, 2: open
    }

    static void on_open_timer(lv_timer_t* t) {
        auto* self = static_cast<HomeScreen*>(lv_timer_get_user_data(t));
        if (++self->open_step_ == 1) {
            self->set_icon_pressed(false);
            return;
        }
        self->open_timer_ = nullptr;  // the last run: the timer deletes itself
        self->open_current();
    }

    void set_icon_pressed(bool pressed) {
        auto* icon = page_ ? static_cast<lv_obj_t*>(lv_obj_get_user_data(page_)) : nullptr;
        if (!icon) return;  // the clock has none
        if (pressed) lv_obj_add_state(icon, LV_STATE_PRESSED);
        else lv_obj_remove_state(icon, LV_STATE_PRESSED);
    }

    void open_current() {
        const Page& p = pages_[current_];
        if (p.kind == PageKind::Clock) return;
        if (p.kind == PageKind::Settings) settings::open();
        else apps::open(p.app);
    }

    lv_obj_t* make_page(int index) {
        lv_obj_t* obj = ui::plain(root());
        lv_obj_set_size(obj, ui::size(), ui::size());
        if (pages_[index].kind != PageKind::Clock) {
            // The whole page is the button; its icon shows the touch.
            lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(obj, on_page_clicked, LV_EVENT_CLICKED, this);
            lv_obj_add_event_cb(obj, on_page_touch, LV_EVENT_PRESSED, this);
            lv_obj_add_event_cb(obj, on_page_touch, LV_EVENT_RELEASED, this);
            lv_obj_add_event_cb(obj, on_page_touch, LV_EVENT_PRESS_LOST, this);
        }
        if (pages_[index].kind == PageKind::Clock) build_clock(obj);
        else build_app(obj, pages_[index].app);
        return obj;
    }

    static void on_page_clicked(lv_event_t* e) { static_cast<HomeScreen*>(lv_event_get_user_data(e))->open_soon(false); }

    static void on_page_touch(lv_event_t* e) {
        auto* page = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
        auto* icon = static_cast<lv_obj_t*>(lv_obj_get_user_data(page));
        if (!icon) return;
        if (lv_event_get_code(e) == LV_EVENT_PRESSED) lv_obj_add_state(icon, LV_STATE_PRESSED);
        else lv_obj_remove_state(icon, LV_STATE_PRESSED);
    }

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

    // direction: +1 = forward (the new page comes in from the right), -1 = back, 0 = no animation.
    void show_page(int index, int direction) {
        cancel_open();
        lv_obj_t* old = page_;
        current_ = index;
        page_ = make_page(index);
        lv_obj_move_foreground(overlay_);
        update_overlay();
        if (old && direction != 0) {
            slide(page_, direction * ui::size(), 0, false);
            slide(old, lv_obj_get_x(old), -direction * ui::size(), true);
        } else if (old) {
            lv_obj_delete(old);
        }
    }

    void step(int delta) { show_page((current_ + delta + count()) % count(), delta > 0 ? 1 : -1); }

    static void on_gesture(lv_event_t* e) {
        auto* self = static_cast<HomeScreen*>(lv_event_get_user_data(e));
        lv_indev_t* indev = lv_indev_active();
        if (!indev) return;
        lv_dir_t dir = lv_indev_get_gesture_dir(indev);
        if (dir == LV_DIR_LEFT) self->step(1);
        else if (dir == LV_DIR_RIGHT) self->step(-1);
        else return;
        lv_indev_wait_release(indev);
    }

    std::vector<Page> pages_;
    int current_ = 0;
    lv_obj_t* page_ = nullptr;
    lv_obj_t* overlay_ = nullptr;
    std::vector<lv_obj_t*> dots_;
    lv_obj_t* peeks_[2] = {};
    lv_obj_t* peek_icons_[2] = {};
    std::string peek_ids_[2];
    lv_timer_t* timer_ = nullptr;
    lv_timer_t* open_timer_ = nullptr;
    int open_step_ = 0;
    lv_obj_t* clock_page_ = nullptr;
    lv_obj_t* clock_time_ = nullptr;
    lv_obj_t* clock_ampm_ = nullptr;
    lv_obj_t* clock_date_ = nullptr;
    lv_obj_t* clock_seconds_ = nullptr;
};

}  // namespace

Screen* home_create() { return new HomeScreen(); }

void home_refresh() {
    if (home) home->refresh();
}

void home_fetch_icons() {
    for (const AppInfo& app : apps::list()) {
        icons::fetch(app.icon, ui::px(PageIconSize));
        icons::fetch(app.icon, ui::px(PeekIconSize));
    }
}

std::string home_page() { return home ? home->current_id() : ""; }

void home_show(const std::string& id) {
    if (home) home->select(id);
}
