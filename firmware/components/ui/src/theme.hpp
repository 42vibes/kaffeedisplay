#pragma once

#include "lvgl.h"

extern "C" {
extern const lv_font_t kaffee_font_14;
extern const lv_font_t kaffee_font_18;
extern const lv_font_t kaffee_font_22;
extern const lv_font_t kaffee_font_18_bold;
extern const lv_font_t kaffee_font_24_bold;
extern const lv_font_t kaffee_font_34_bold;
}

namespace kaffee::ui::theme {

inline constexpr uint32_t COLOR_BG = 0x1A1412;
inline constexpr uint32_t COLOR_SURFACE = 0x241B18;
inline constexpr uint32_t COLOR_SURFACE_ALT = 0x2E2320;
inline constexpr uint32_t COLOR_BORDER = 0x3B2D28;
inline constexpr uint32_t COLOR_TEXT = 0xF5EFE6;
inline constexpr uint32_t COLOR_MUTED = 0xA89890;
inline constexpr uint32_t COLOR_DIM = 0x7A6A62;
inline constexpr uint32_t COLOR_ACCENT = 0xD4A373;
inline constexpr uint32_t COLOR_POSITIVE = 0x8FBC8F;
inline constexpr uint32_t COLOR_NEGATIVE = 0xE07A5F;
inline constexpr uint32_t COLOR_GOLD = 0xE0B15C;
inline constexpr uint32_t COLOR_SILVER = 0xB9C0C8;
inline constexpr uint32_t COLOR_BRONZE = 0xC07A48;

inline lv_color_t color(uint32_t rgb) {
    return lv_color_hex(rgb);
}

inline const lv_font_t* font_small() {
    return &kaffee_font_14;
}
inline const lv_font_t* font_body() {
    return &kaffee_font_18;
}
inline const lv_font_t* font_title() {
    return &kaffee_font_22;
}
inline const lv_font_t* font_body_bold() {
    return &kaffee_font_18_bold;
}
inline const lv_font_t* font_title_bold() {
    return &kaffee_font_24_bold;
}
inline const lv_font_t* font_huge_bold() {
    return &kaffee_font_34_bold;
}

void apply_screen(lv_obj_t* obj);
void apply_tile(lv_obj_t* obj);
void make_transparent(lv_obj_t* obj);
void hide_scrollbars(lv_obj_t* obj);

lv_obj_t* make_card(lv_obj_t* parent);
lv_obj_t* make_label(lv_obj_t* parent, const char* text, const lv_font_t* font, lv_color_t color);
lv_obj_t* make_button(lv_obj_t* parent, const char* text, bool primary);
lv_obj_t* make_row(lv_obj_t* parent, int gap);
lv_obj_t* make_column(lv_obj_t* parent, int gap);
lv_obj_t* make_circle(lv_obj_t* parent, int size, lv_color_t color);
void set_pressed_feedback(lv_obj_t* obj, lv_color_t color);
void set_wrapped(lv_obj_t* label);
void set_ellipsis(lv_obj_t* label);

}  // namespace kaffee::ui::theme
