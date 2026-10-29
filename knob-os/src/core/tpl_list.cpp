// "list": a list of rows, e.g. output devices or a Settings page.
//   title, color, note (small pill under the title, optional), hint,
//   items [{id, label, detail, value, icon, selected (the current one, ticked), danger}], focus (item id, optional)
// Turning moves the highlight; pressing reports press (item). Tapping a row does the same. Three rows show at a time.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "templates.h"
#include "theme.h"
#include "ui.h"

namespace {

constexpr uint32_t RowIcon = 0xC9CDD4;
constexpr uint32_t DangerText = 0xFCA5A5;
constexpr int Visible = 3;
constexpr int RowWidth = 184, RowHeight = 38, NoteHeight = 18;  // design px
constexpr int Gap = 8;  // between everything: title, note, each row, position, hint (design px)

struct Row {
    std::string id, label, detail, value, icon;
    bool selected = false, danger = false;
};

class ListScreen : public TemplateScreen {
public:
    ListScreen(JsonObjectConst data, TemplateSink sink) : TemplateScreen(ui::screen_root(screens::parent()), std::move(sink)) {
        read(data);
        build();
    }

    void update(JsonObjectConst data) override {
        read(data);
        build();
    }

    void input(KnobInput in) override {
        int n = static_cast<int>(rows_.size());
        switch (in) {
            case KnobInput::TurnRight:
                if (focus_ < n - 1) { focus_++; build(); }
                break;
            case KnobInput::TurnLeft:
                if (focus_ > 0) { focus_--; build(); }
                break;
            case KnobInput::Press:
                if (n) emit("press", rows_[focus_].id);
                break;
            case KnobInput::Back: emit("back"); break;
        }
    }

    // A click per row, with end stops at the first and the last.
    haptics::Feel feel() const override {
        haptics::Feel f;
        f.positions = std::max<int>(1, static_cast<int>(rows_.size()));
        f.position = focus_;
        return f;
    }

private:
    void read(JsonObjectConst d) {
        if (d["title"].is<const char*>()) title_ = d["title"].as<const char*>();
        if (d["note"].is<const char*>()) note_ = d["note"].as<const char*>();
        if (d["hint"].is<const char*>()) hint_ = d["hint"].as<const char*>();
        if (d["color"].is<const char*>()) color_ = ui::parse_color(d["color"], color_);
        if (d["items"].is<JsonArrayConst>()) {
            rows_.clear();
            for (JsonObjectConst item : d["items"].as<JsonArrayConst>()) {
                Row row;
                row.id = item["id"] | "";
                row.label = item["label"] | "";
                row.detail = item["detail"] | "";
                row.value = item["value"] | "";
                row.icon = item["icon"] | "";
                row.selected = item["selected"] | false;
                row.danger = item["danger"] | false;
                rows_.push_back(row);
            }
            // Start on the current item unless told otherwise.
            for (size_t i = 0; i < rows_.size(); i++)
                if (rows_[i].selected) focus_ = static_cast<int>(i);
        }
        if (d["focus"].is<const char*>()) {
            std::string id = d["focus"].as<const char*>();
            for (size_t i = 0; i < rows_.size(); i++)
                if (rows_[i].id == id) focus_ = static_cast<int>(i);
        }
        focus_ = std::max(0, std::min(focus_, static_cast<int>(rows_.size()) - 1));
    }

    // Title, note, the rows, the position ("2 / 5") and the hint, the same gap apart and centred on the screen. Text
    // is spaced by its capitals' ink, not its line (which has room above and below).
    void build() {
        lv_obj_clean(root());
        lv_obj_t* r = root();
        const lv_font_t* small = ui::font(12);
        const int cap_top = ui::ink_top(small), cap_height = ui::ink_height(small);
        const int gap = ui::px(Gap), row_height = ui::px(RowHeight), note_height = ui::px(NoteHeight);
        const int n = static_cast<int>(rows_.size());
        const int shown = std::min(n, Visible);

        int height = cap_height;  // the title
        if (!note_.empty()) height += gap + note_height;
        if (shown) height += gap + shown * row_height + (shown - 1) * gap;
        if (n > Visible) height += gap + cap_height;
        if (!hint_.empty()) height += gap + cap_height;
        int y = (ui::size() - height) / 2;

        lv_obj_t* title = ui::label(r, title_.c_str(), small, color_);
        lv_obj_set_style_text_letter_space(title, ui::px(2), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, y - cap_top);
        y += cap_height + gap;

        if (!note_.empty()) {
            lv_obj_t* note = ui::plain(r);
            lv_obj_set_size(note, LV_SIZE_CONTENT, note_height);
            lv_obj_set_style_pad_hor(note, ui::px(8), 0);
            lv_obj_set_style_radius(note, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_color(note, theme::color(ui::tint(color_)), 0);
            lv_obj_set_style_bg_opa(note, LV_OPA_COVER, 0);
            lv_obj_t* text = ui::label(note, note_.c_str(), small, theme::Text);
            ui::one_line(text, ui::px(160));
            lv_obj_center(text);
            lv_obj_align(note, LV_ALIGN_TOP_MID, 0, y);
            y += note_height + gap;
        }

        int first = std::max(0, std::min(focus_ - 1, n - Visible));
        for (int i = first; i < first + shown; i++) {
            build_row(rows_[i], i, y);
            y += row_height + gap;
        }

        if (n > Visible) {
            lv_obj_t* position = ui::label(r, "", small, theme::TextMuted);
            lv_label_set_text_fmt(position, "%d / %d", focus_ + 1, n);
            lv_obj_align(position, LV_ALIGN_TOP_MID, 0, y - cap_top);
            y += cap_height + gap;
        }

        if (!hint_.empty()) {
            lv_obj_t* hint = ui::label(r, hint_.c_str(), small, theme::TextMuted);
            ui::one_line(hint, width_at(y + cap_height));  // as wide as the round screen is down there
            lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, y - cap_top);
        }
    }

    // How wide the round screen is at height y, less a margin.
    static int width_at(int y) {
        const int radius = ui::size() / 2, dy = std::abs(y - radius);
        return dy >= radius ? 0 : 2 * static_cast<int>(std::sqrt(static_cast<float>(radius * radius - dy * dy))) - ui::px(24);
    }

    void build_row(const Row& row, int index, int y) {
        bool focused = index == focus_;
        lv_obj_t* obj = ui::plain(root());
        const int row_width = ui::px(RowWidth), row_height = ui::px(RowHeight);
        lv_obj_set_size(obj, row_width, row_height);
        lv_obj_set_pos(obj, (ui::size() - row_width) / 2, y);
        lv_obj_set_style_radius(obj, ui::px(10), 0);
        lv_obj_set_style_bg_color(obj, theme::color(focused ? theme::SurfaceRaised : theme::Surface), 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
        if (focused) {
            lv_obj_set_style_border_width(obj, ui::px(2), 0);
            lv_obj_set_style_border_color(obj, theme::color(color_), 0);
        }
        lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
        ui::press_feedback(obj, 97);
        lv_obj_set_user_data(obj, reinterpret_cast<void*>(static_cast<intptr_t>(index)));
        lv_obj_add_event_cb(obj, on_click, LV_EVENT_CLICKED, this);

        int text_x = ui::px(10);
        if (!row.icon.empty()) {
            lv_obj_t* icon = ui::icon(obj, row.icon, ui::px(12), row.danger ? DangerText : RowIcon);
            lv_obj_align(icon, LV_ALIGN_LEFT_MID, ui::px(9), 0);
            text_x = ui::px(28);
        }
        // The label comes first: the tick or value on the right gets what the label leaves (shortened with "..." if
        // that's not enough, never below a short minimum), and the detail line goes under the label.
        const int RightPad = ui::px(10), ValueGap = ui::px(6), MinValue = ui::px(36);
        const lv_font_t* label_font = ui::font(14);
        const lv_font_t* small = ui::font(12);
        const int space = row_width - text_x - RightPad;
        lv_obj_t* label = ui::label(obj, row.label.c_str(), label_font, row.danger ? DangerText : theme::Text);
        lv_obj_update_layout(label);
        int text_width = space;
        if (row.selected || !row.value.empty()) {
            lv_obj_t* end = row.selected ? ui::label(obj, LV_SYMBOL_OK, small, color_)
                                         : ui::label(obj, row.value.c_str(), small, theme::TextSecondary);
            lv_obj_update_layout(end);
            int end_width = lv_obj_get_width(end);
            int label_room = std::min<int>(lv_obj_get_width(label), space - ValueGap - std::min(end_width, MinValue));
            ui::one_line(end, space - ValueGap - label_room);
            lv_obj_align(end, LV_ALIGN_RIGHT_MID, -RightPad, 0);
            lv_obj_update_layout(end);
            text_width = space - ValueGap - lv_obj_get_width(end);
        }
        ui::one_line(label, text_width);

        // Placed by their letters, so one line sits in the middle and two sit evenly with a fixed gap.
        const int LineGap = ui::px(5);
        const int label_top = ui::ink_top(label_font), label_height = ui::ink_height(label_font);
        if (row.detail.empty()) {
            lv_obj_set_pos(label, text_x, (row_height - label_height) / 2 - label_top);
            return;
        }
        const int detail_height = ui::ink_height(small);
        const int top = (row_height - (label_height + LineGap + detail_height)) / 2 - ui::px(1);  // a little up: room for g, p, y
        lv_obj_set_pos(label, text_x, top - label_top);
        lv_obj_t* detail = ui::label(obj, row.detail.c_str(), small, theme::TextSecondary);
        ui::one_line(detail, text_width);
        lv_obj_set_pos(detail, text_x, top + label_height + LineGap - ui::ink_top(small));
    }

    static void on_click(lv_event_t* e) {
        auto* self = static_cast<ListScreen*>(lv_event_get_user_data(e));
        auto* obj = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
        int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_obj_get_user_data(obj)));
        self->focus_ = index;
        // Report it; rebuilding happens on the next turn or update (never while this row's click is being handled).
        self->emit("press", self->rows_[index].id);
    }

    std::string title_, note_, hint_;
    uint32_t color_ = theme::Accent;
    std::vector<Row> rows_;
    int focus_ = 0;
};

}  // namespace

TemplateScreen* make_list(JsonObjectConst data, TemplateSink sink) { return new ListScreen(data, std::move(sink)); }
