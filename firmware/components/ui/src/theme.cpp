#include "theme.hpp"

namespace kaffee::ui::theme {

void apply_screen(lv_obj_t* obj) {
    lv_obj_set_style_bg_color(obj, color(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_scrollable(obj, false);
    hide_scrollbars(obj);
}

void apply_tile(lv_obj_t* obj) {
    apply_screen(obj);
}

void make_transparent(lv_obj_t* obj) {
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
}

void hide_scrollbars(lv_obj_t* obj) {
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

lv_obj_t* make_card(lv_obj_t* parent) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_set_scrollable(obj, false);
    lv_obj_set_style_bg_color(obj, color(COLOR_SURFACE), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 20, 0);
    lv_obj_set_style_pad_all(obj, 14, 0);
    hide_scrollbars(obj);
    return obj;
}

lv_obj_t* make_label(lv_obj_t* parent, const char* text, const lv_font_t* font,
                     lv_color_t text_color) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, text_color, 0);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(label, 0, 0);
    lv_obj_set_style_pad_all(label, 0, 0);
    return label;
}

lv_obj_t* make_button(lv_obj_t* parent, const char* text, bool primary) {
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_set_height(button, 46);
    lv_obj_set_style_radius(button, 14, 0);
    lv_obj_set_style_pad_hor(button, 18, 0);
    lv_obj_set_style_pad_ver(button, 8, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    if (primary) {
        lv_obj_set_style_bg_color(button, color(COLOR_ACCENT), 0);
        lv_obj_set_style_bg_color(button, color(COLOR_GOLD), LV_STATE_PRESSED);
        lv_obj_set_style_text_color(button, color(COLOR_BG), 0);
        lv_obj_set_style_border_width(button, 0, 0);
    } else {
        lv_obj_set_style_bg_color(button, color(COLOR_SURFACE_ALT), 0);
        lv_obj_set_style_bg_color(button, color(COLOR_BORDER), LV_STATE_PRESSED);
        lv_obj_set_style_text_color(button, color(COLOR_TEXT), 0);
        lv_obj_set_style_border_width(button, 1, 0);
        lv_obj_set_style_border_color(button, color(COLOR_BORDER), 0);
    }
    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font_body_bold(), 0);
    lv_obj_set_style_text_color(label, primary ? color(COLOR_BG) : color(COLOR_TEXT), 0);
    lv_obj_center(label);
    return button;
}

lv_obj_t* make_row(lv_obj_t* parent, int gap) {
    lv_obj_t* row = lv_obj_create(parent);
    make_transparent(row);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, gap, 0);
    lv_obj_set_style_pad_row(row, 0, 0);
    hide_scrollbars(row);
    return row;
}

lv_obj_t* make_column(lv_obj_t* parent, int gap) {
    lv_obj_t* column = lv_obj_create(parent);
    make_transparent(column);
    lv_obj_set_width(column, lv_pct(100));
    lv_obj_set_height(column, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(column, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(column, gap, 0);
    lv_obj_set_style_pad_column(column, 0, 0);
    hide_scrollbars(column);
    return column;
}

lv_obj_t* make_circle(lv_obj_t* parent, int size, lv_color_t fill) {
    lv_obj_t* circle = lv_obj_create(parent);
    lv_obj_set_scrollable(circle, false);
    lv_obj_set_size(circle, size, size);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(circle, fill, 0);
    lv_obj_set_style_bg_opa(circle, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(circle, 0, 0);
    lv_obj_set_style_pad_all(circle, 0, 0);
    lv_obj_set_style_shadow_width(circle, 0, 0);
    hide_scrollbars(circle);
    return circle;
}

void set_pressed_feedback(lv_obj_t* obj, lv_color_t fill) {
    lv_obj_set_style_bg_color(obj, fill, LV_STATE_PRESSED);
}

void set_wrapped(lv_obj_t* label) {
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_width(label, lv_pct(100));
}

void set_ellipsis(lv_obj_t* label) {
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(label, lv_pct(100));
}

}  // namespace kaffee::ui::theme
