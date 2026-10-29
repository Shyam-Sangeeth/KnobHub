// "tiles": up to three round tiles in a row (e.g. audio channels) with pills under them.
//   title, color (the pills' accent), items [{id, label, icon, color, value (0-100, the ring), detail (inside the tile
//   instead of the value; long text scrolls), tag (under the tile), tag_icon (an icon in a small badge on the ring, at
//   the bottom), busy (bool: an arc spins round the ring), muted (bool: the icon is crossed out)}],
//   selected (item id: where focus starts),
//   pills [{id, kind: "toggle", label, on, icon (optional), on_color / off_color (the icon's, when on / off; default
//          the accent / grey)} | {id, kind: "segment", options: [...], selected}], hint
// The rows (title, tiles, each pill, hint) are the same gap apart, and centred on the screen together.
// Turning moves the focus over the tiles and pills. Pressing a tile reports press (item); pressing a pill reports
// tap (item = pill id): a toggle flips (delta = 1 on, 0 off), a segment moves to its next option (delta = new index).
// Tapping on screen does the same.
#include <algorithm>
#include <string>
#include <vector>

#include "templates.h"
#include "theme.h"
#include "ui.h"

namespace {

constexpr uint32_t ArcTrack = 0x22252B;
constexpr uint32_t SegmentOnText = 0x101114;
constexpr uint32_t Unset = 0xFFFFFFFF;  // a colour the app didn't send
constexpr int TileSize = 58, PillHeight = 22, IconSize = 12;  // design px
constexpr int RowGap = 11;      // between the rows: title, tiles, each pill, hint (design px)
constexpr int BadgeBelow = 10;  // how far a badge hangs below its circle (design px)
constexpr int OutlineRoom = 2;  // a focus outline's reach outside its pill (design px)

struct Tile {
    std::string id, label, icon, tag, tag_icon, detail;
    uint32_t color = theme::Accent;
    int value = 0;
    bool busy = false;
    bool muted = false;
};

struct Pill {
    std::string id, kind, label, icon;
    uint32_t on_color = Unset, off_color = Unset;  // the icon's
    bool on = false;
    std::vector<std::string> options;
    int selected = 0;
    lv_obj_t* obj = nullptr;
    lv_obj_t* text = nullptr;           // toggle
    lv_obj_t* icon_obj = nullptr;       // toggle, optional
    std::vector<lv_obj_t*> parts;        // segment options
};

class TilesScreen : public TemplateScreen {
public:
    TilesScreen(JsonObjectConst data, TemplateSink sink) : TemplateScreen(ui::screen_root(screens::parent()), std::move(sink)) {
        read(data, true);
        build();
    }

    void update(JsonObjectConst data) override {
        read(data, false);
        build();
    }

    void input(KnobInput in) override {
        int n = count();
        switch (in) {
            case KnobInput::TurnRight:
                if (n) focus_ = (focus_ + 1) % n;
                show_focus();
                break;
            case KnobInput::TurnLeft:
                if (n) focus_ = (focus_ + n - 1) % n;
                show_focus();
                break;
            case KnobInput::Press:
                if (n) activate(focus_);
                break;
            case KnobInput::Back: emit("back"); break;
        }
    }

    // The tile in focus: its colour and value (none while muted). A pill: the accent, no level.
    lights::Glow glow() const override {
        if (focus_ < 0 || focus_ >= static_cast<int>(tiles_.size())) return {accent_, -1};
        const Tile& t = tiles_[focus_];
        return {t.color, t.muted ? 0 : t.value};
    }

    // A click per tile or pill, round and round.
    haptics::Feel feel() const override {
        haptics::Feel f;
        f.positions = std::max(1, count());
        f.position = focus_;
        f.wrap = true;
        f.detent = haptics::Detent::Medium;
        return f;
    }

private:
    int count() const { return static_cast<int>(tiles_.size() + pills_.size()); }

    // "selected" only sets where focus starts; an update (e.g. after a pill was tapped) keeps it where it is.
    void read(JsonObjectConst d, bool first) {
        if (d["title"].is<const char*>()) title_ = d["title"].as<const char*>();
        if (d["hint"].is<const char*>()) hint_ = d["hint"].as<const char*>();
        if (d["color"].is<const char*>()) accent_ = ui::parse_color(d["color"], accent_);
        if (d["items"].is<JsonArrayConst>()) {
            tiles_.clear();
            for (JsonObjectConst item : d["items"].as<JsonArrayConst>()) {
                Tile t;
                t.id = item["id"] | "";
                t.label = item["label"] | "";
                t.icon = item["icon"] | "";
                t.tag = item["tag"] | "";
                t.tag_icon = item["tag_icon"] | "";
                t.detail = item["detail"] | "";
                t.busy = item["busy"] | false;
                t.muted = item["muted"] | false;
                t.color = ui::parse_color(item["color"], theme::Accent);
                t.value = item["value"] | 0;
                tiles_.push_back(t);
                if (tiles_.size() == 3) break;
            }
        }
        if (d["pills"].is<JsonArrayConst>()) {
            pills_.clear();
            for (JsonObjectConst item : d["pills"].as<JsonArrayConst>()) {
                Pill p;
                p.id = item["id"] | "";
                p.kind = item["kind"] | "toggle";
                p.label = item["label"] | "";
                p.icon = item["icon"] | "";
                p.on_color = ui::parse_color(item["on_color"], Unset);
                p.off_color = ui::parse_color(item["off_color"], Unset);
                p.on = item["on"] | false;
                p.selected = item["selected"] | 0;
                for (JsonVariantConst option : item["options"].as<JsonArrayConst>()) p.options.push_back(option | "");
                pills_.push_back(p);
            }
        }
        if (first && d["selected"].is<const char*>()) {
            std::string selected = d["selected"].as<const char*>();
            for (size_t i = 0; i < tiles_.size(); i++)
                if (tiles_[i].id == selected) focus_ = static_cast<int>(i);
        }
        if (focus_ >= count()) focus_ = 0;
    }

    void build() {
        lv_obj_clean(root());
        targets_.clear();
        lv_obj_t* r = root();

        // The rows' heights, so they can be laid out the same gap apart and centred together.
        const int line = lv_font_get_line_height(ui::font(12));
        const int tile_size = ui::px(TileSize), pill_height = ui::px(PillHeight);
        const int row_gap = ui::px(RowGap), outline_room = ui::px(OutlineRoom);
        const int badge_below = ui::px(BadgeBelow);
        bool badges = false, tags = false;
        for (const Tile& t : tiles_) {
            badges |= !t.tag_icon.empty();
            tags |= !t.tag.empty();
        }
        const int below = tags ? (badges ? badge_below + ui::px(1) : ui::px(3)) + line : (badges ? badge_below : 0);  // under the circles
        int height = (title_.empty() ? 0 : line + row_gap) + tile_size + below;
        for (size_t i = 0; i < pills_.size(); i++) height += row_gap + pill_height;
        if (!hint_.empty()) height += row_gap + line;
        int y = (ui::size() - height) / 2;

        if (!title_.empty()) {
            lv_obj_t* title = ui::label(r, title_.c_str(), ui::font(12), theme::TextSecondary);
            lv_obj_set_style_text_letter_space(title, ui::px(2), 0);
            lv_obj_align(title, LV_ALIGN_TOP_MID, 0, y);
            y += line + row_gap;
        }

        const int X3[] = {ui::px(46), ui::px(120), ui::px(194)}, X2[] = {ui::px(74), ui::px(166)}, X1[] = {ui::px(120)};  // 16 px between tiles
        const int* xs = tiles_.size() == 3 ? X3 : tiles_.size() == 2 ? X2 : X1;
        for (size_t i = 0; i < tiles_.size(); i++) build_tile(tiles_[i], xs[i], y + tile_size / 2, static_cast<int>(i));
        y += tile_size + below;

        // One pill per line: the round screen is too narrow for two side by side.
        lv_obj_t* column = ui::plain(r);
        lv_obj_set_size(column, ui::px(220), LV_SIZE_CONTENT);
        lv_obj_set_pos(column, ui::px(10), y + row_gap - outline_room);
        lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(column, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(column, row_gap, 0);
        lv_obj_set_style_pad_ver(column, outline_room, 0);  // room for the focus outline, which the column would clip
        for (size_t i = 0; i < pills_.size(); i++) build_pill(column, pills_[i], static_cast<int>(tiles_.size() + i));
        y += static_cast<int>(pills_.size()) * (row_gap + pill_height);

        if (!hint_.empty()) {
            lv_obj_t* hint = ui::label(r, hint_.c_str(), ui::font(12), theme::TextMuted);
            ui::one_line(hint, ui::px(160));
            lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, y + row_gap);
        }

        show_focus();
    }

    void build_tile(const Tile& t, int cx, int Cy, int index) {
        const int D = ui::px(TileSize);
        const int DetailWidth = ui::px(34);  // what fits inside the ring at that height
        const int StackGap = ui::px(-1);     // the labels' line height already leaves room above the letters
        const int icon_size = ui::px(IconSize);
        const int badge_below = ui::px(BadgeBelow);
        lv_obj_t* tile = ui::circle(root(), D, theme::Surface, 0, 0);
        lv_obj_set_pos(tile, cx - D / 2, Cy - D / 2);
        make_target(tile, index);

        // A thin ring in the tile's colour, filled by a thicker arc as far as the value.
        lv_obj_t* ring = ui::arc(tile, D, ui::px(5), 270, 360, t.color, t.color);
        lv_obj_set_style_arc_width(ring, ui::px(2), LV_PART_MAIN);
        lv_obj_set_style_arc_opa(ring, LV_OPA_50, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(ring, true, LV_PART_INDICATOR);
        lv_arc_set_value(ring, t.value);
        lv_obj_center(ring);
        if (t.busy) {
            // Busy (e.g. switching device): a white quarter arc goes round and round the ring.
            lv_obj_t* spin = ui::arc(tile, D, ui::px(5), 270, 360, theme::Text, theme::Text);
            lv_obj_set_style_arc_opa(spin, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_arc_rounded(spin, true, LV_PART_INDICATOR);
            lv_arc_set_angles(spin, 0, 90);
            lv_obj_center(spin);
            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, spin);
            lv_anim_set_values(&a, 0, 360);
            lv_anim_set_duration(&a, 900);
            lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
            lv_anim_set_exec_cb(&a, [](void* obj, int32_t angle) { lv_arc_set_rotation(static_cast<lv_obj_t*>(obj), angle); });
            lv_anim_start(&a);
        }
        // Icon, name and detail (or the value) in a column centred in the circle, a fixed gap apart. The icon has a
        // box of its own so a picture and a font symbol take the same room.
        lv_obj_t* stack = ui::plain(tile);
        lv_obj_set_size(stack, D, D);
        lv_obj_center(stack);
        lv_obj_set_flex_flow(stack, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(stack, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(stack, StackGap, 0);
        lv_obj_t* icon_box = ui::plain(stack);
        lv_obj_set_size(icon_box, icon_size, icon_size);
        lv_obj_add_flag(icon_box, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        lv_obj_center(ui::icon(icon_box, t.icon, icon_size, t.color));
        if (t.muted) ui::cross_out(icon_box, icon_size, t.color, theme::Surface);
        ui::label(stack, t.label.c_str(), ui::font(12), theme::Text);
        if (t.detail.empty()) {
            lv_obj_t* value = ui::label(stack, "", ui::font(12), theme::TextSecondary);
            lv_label_set_text_fmt(value, "%d%%", t.value);
        } else {
            lv_obj_t* detail = ui::label(stack, t.detail.c_str(), ui::font(8), theme::Text);  // small and white
            ui::marquee(detail, DetailWidth);
        }

        bool badge = !t.tag_icon.empty();
        if (badge) ui::badge(root(), t.tag_icon, cx, Cy + D / 2 + ui::px(2), t.color);  // on the ring, at the bottom
        if (t.tag.empty()) return;
        lv_obj_t* tag = ui::label(root(), t.tag.c_str(), ui::font(12), theme::TextSecondary);
        ui::one_line(tag, ui::px(66));
        lv_obj_set_pos(tag, cx - lv_obj_get_width(tag) / 2, Cy + D / 2 + (badge ? badge_below + ui::px(1) : ui::px(3)));
    }

    void build_pill(lv_obj_t* row, Pill& p, int index) {
        p.obj = ui::plain(row);
        p.parts.clear();
        lv_obj_set_size(p.obj, LV_SIZE_CONTENT, ui::px(PillHeight));
        lv_obj_set_style_radius(p.obj, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(p.obj, ui::px(1), 0);
        lv_obj_set_style_bg_opa(p.obj, LV_OPA_COVER, 0);
        lv_obj_set_flex_flow(p.obj, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(p.obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        make_target(p.obj, index);
        if (p.kind == "segment") {
            lv_obj_set_style_pad_all(p.obj, ui::px(2), 0);
            for (const std::string& option : p.options) {
                lv_obj_t* part = ui::plain(p.obj);
                lv_obj_set_size(part, LV_SIZE_CONTENT, ui::px(18));
                lv_obj_set_style_pad_hor(part, ui::px(8), 0);
                lv_obj_set_style_radius(part, LV_RADIUS_CIRCLE, 0);
                lv_obj_t* text = ui::label(part, option.c_str(), ui::font(12), theme::TextSecondary);
                lv_obj_center(text);
                p.parts.push_back(part);
            }
        } else {
            lv_obj_set_style_pad_hor(p.obj, ui::px(11), 0);
            lv_obj_set_style_pad_column(p.obj, ui::px(5), 0);
            p.icon_obj = p.icon.empty() ? nullptr : ui::icon(p.obj, p.icon, ui::px(12), theme::TextMuted);
            p.text = ui::label(p.obj, p.label.c_str(), ui::font(12), theme::TextSecondary);
        }
        style_pill(p);
    }

    void style_pill(Pill& p) {
        if (p.kind == "segment") {
            lv_obj_set_style_bg_color(p.obj, theme::color(theme::Surface), 0);
            lv_obj_set_style_border_color(p.obj, theme::color(theme::Border), 0);
            for (size_t i = 0; i < p.parts.size(); i++) {
                bool on = static_cast<int>(i) == p.selected;
                lv_obj_set_style_bg_color(p.parts[i], theme::color(theme::Text), 0);
                lv_obj_set_style_bg_opa(p.parts[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
                lv_obj_set_style_text_color(lv_obj_get_child(p.parts[i], 0), theme::color(on ? SegmentOnText : theme::TextSecondary), 0);
            }
        } else {
            lv_obj_set_style_bg_color(p.obj, theme::color(p.on ? ui::tint(accent_) : theme::Surface), 0);
            lv_obj_set_style_border_color(p.obj, theme::color(p.on ? accent_ : theme::Border), 0);
            lv_obj_set_style_text_color(p.text, theme::color(p.on ? ui::light(accent_) : theme::TextSecondary), 0);
            uint32_t icon = p.on ? (p.on_color != Unset ? p.on_color : accent_) : (p.off_color != Unset ? p.off_color : theme::TextMuted);
            if (p.icon_obj) ui::set_icon_color(p.icon_obj, icon);
        }
    }

    void make_target(lv_obj_t* obj, int index) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
        ui::press_feedback(obj);
        lv_obj_set_user_data(obj, reinterpret_cast<void*>(static_cast<intptr_t>(index)));
        lv_obj_add_event_cb(obj, on_click, LV_EVENT_CLICKED, this);
        targets_.push_back(obj);
    }

    static void on_click(lv_event_t* e) {
        auto* self = static_cast<TilesScreen*>(lv_event_get_user_data(e));
        auto* obj = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
        int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(obj)));
        self->focus_ = index;
        self->show_focus();
        self->activate(index);
    }

    void show_focus() {
        for (int i = 0; i < static_cast<int>(targets_.size()); i++) {
            bool on = i == focus_;
            lv_obj_set_style_outline_width(targets_[i], on ? ui::px(2) : 0, 0);
            lv_obj_set_style_outline_color(targets_[i], theme::color(theme::Text), 0);
            lv_obj_set_style_outline_pad(targets_[i], 0, 0);  // close round it
        }
    }

    // Pills change on screen right away (restyled in place, so this is safe inside a click); the app confirms with an
    // update.
    void activate(int index) {
        int tiles = static_cast<int>(tiles_.size());
        if (index < tiles) {
            emit("press", tiles_[index].id);
            return;
        }
        Pill& p = pills_[index - tiles];
        if (p.kind == "segment") {
            if (p.options.empty()) return;
            p.selected = (p.selected + 1) % static_cast<int>(p.options.size());
            style_pill(p);
            emit("tap", p.id, p.selected);
        } else {
            p.on = !p.on;
            style_pill(p);
            emit("tap", p.id, p.on ? 1 : 0);
        }
    }

    std::string title_, hint_;
    uint32_t accent_ = theme::Accent;
    std::vector<Tile> tiles_;
    std::vector<Pill> pills_;
    std::vector<lv_obj_t*> targets_;  // tiles then pills, in focus order
    int focus_ = 0;
};

}  // namespace

TemplateScreen* make_tiles(JsonObjectConst data, TemplateSink sink) { return new TilesScreen(data, std::move(sink)); }
