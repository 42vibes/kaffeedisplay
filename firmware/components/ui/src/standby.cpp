#include "ui_internal.hpp"

#include "strings.hpp"
#include "theme.hpp"

namespace kaffee::ui {

namespace {

constexpr uint32_t kSteamFrameMs = 90;
constexpr uint32_t kSteamCycle = 36;

// Four points are enough for a gently curved rising steam stroke. LVGL draws
// the line directly; the firmware carries no GIF, PNG frames or decoder.
static lv_point_precise_t kSteamPoints[] = {{2, 32}, {9, 23}, {2, 13}, {8, 1}};

lv_obj_t* shape(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color, int radius) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_style_bg_color(obj, theme::color(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_clickable(obj, false);
    lv_obj_set_scrollable(obj, false);
    return obj;
}

}  // namespace

void UiAppImpl::build_standby() {
    // Put the overlay above page dots/toasts, but create it only once.
    standby_overlay_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(standby_overlay_);
    lv_obj_set_size(standby_overlay_, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(standby_overlay_, 0, 0);
    lv_obj_set_style_bg_color(standby_overlay_, theme::color(0x100D0C), 0);
    lv_obj_set_style_bg_opa(standby_overlay_, LV_OPA_COVER, 0);
    lv_obj_set_clickable(standby_overlay_, true);
    lv_obj_set_scrollable(standby_overlay_, false);
    lv_obj_add_event_cb(standby_overlay_, &UiAppImpl::standby_wake_cb, LV_EVENT_CLICKED, this);

    lv_obj_t* scene = lv_obj_create(standby_overlay_);
    lv_obj_remove_style_all(scene);
    lv_obj_set_size(scene, 300, 320);
    lv_obj_center(scene);
    lv_obj_set_clickable(scene, false);
    lv_obj_set_scrollable(scene, false);

    lv_obj_t* title =
        theme::make_label(scene, strings::STANDBY_TITLE, theme::font_title_bold(),
                          theme::color(theme::COLOR_ACCENT));
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    // Handle behind the cup, painted with an outline and a hollow centre.
    lv_obj_t* handle = shape(scene, 194, 173, 62, 64, theme::COLOR_ACCENT, 30);
    lv_obj_set_style_bg_opa(handle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(handle, 12, 0);
    lv_obj_set_style_border_color(handle, theme::color(theme::COLOR_ACCENT), 0);
    lv_obj_set_style_border_opa(handle, LV_OPA_COVER, 0);

    // A simple cup, dark coffee surface and saucer drawn entirely by LVGL.
    shape(scene, 72, 251, 190, 10, theme::COLOR_SURFACE_ALT, LV_RADIUS_CIRCLE);
    shape(scene, 90, 245, 154, 10, theme::COLOR_GOLD, LV_RADIUS_CIRCLE);
    shape(scene, 82, 168, 141, 79, theme::COLOR_ACCENT, 20);
    shape(scene, 91, 160, 121, 21, 0x6B3E2B, LV_RADIUS_CIRCLE);
    shape(scene, 102, 159, 99, 8, 0x9A6042, LV_RADIUS_CIRCLE);
    shape(scene, 105, 183, 95, 5, 0xE9C9A6, LV_RADIUS_CIRCLE);

    // A tiny coffee-bean emblem makes the untextured cup recognisable.
    shape(scene, 145, 203, 16, 24, theme::COLOR_SURFACE_ALT, LV_RADIUS_CIRCLE);

    for (int i = 0; i < 3; ++i) {
        steam_[i] = lv_line_create(scene);
        lv_line_set_points(steam_[i], kSteamPoints, 4);
        lv_obj_set_style_line_width(steam_[i], 5, 0);
        lv_obj_set_style_line_rounded(steam_[i], true, 0);
        lv_obj_set_style_line_color(steam_[i], theme::color(theme::COLOR_TEXT), 0);
        lv_obj_set_style_line_opa(steam_[i], LV_OPA_TRANSP, 0);
        lv_obj_set_clickable(steam_[i], false);
    }

    lv_obj_t* hint =
        theme::make_label(scene, strings::STANDBY_TOUCH_HINT, theme::font_small(),
                          theme::color(theme::COLOR_MUTED));
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -8);

    standby_timer_ = lv_timer_create(&UiAppImpl::standby_anim_cb, kSteamFrameMs, this);
    lv_timer_pause(standby_timer_);
    lv_obj_set_hidden(standby_overlay_, true);
}

void UiAppImpl::hide_standby() {
    if (standby_overlay_ == nullptr) return;
    standby_active_ = false;
    standby_animating_ = false;
    lv_timer_pause(standby_timer_);
    lv_obj_set_hidden(standby_overlay_, true);
}

void UiAppImpl::update_standby() {
    if (standby_overlay_ == nullptr) return;

    // The board's power task already owns brightness/dim/off control, using
    // these same persisted settings. This view only reflects inactivity and
    // never changes the backlight from within LVGL.
    const DisplaySettings settings = backend_.display_settings();
    const uint32_t idle_ms = lv_display_get_inactive_time(lv_display_get_default());
    const bool dimmed = settings.dim_timeout_s > 0 &&
                        idle_ms >= static_cast<uint32_t>(settings.dim_timeout_s) * 1000U;
    const bool off = settings.screen_off_timeout_s > 0 &&
                     idle_ms >= static_cast<uint32_t>(settings.screen_off_timeout_s) * 1000U;

    // Stay out of the way during admin Wi-Fi, display test patterns, or when
    // the admin disabled both idle transitions.
    if (last_ap_active_ || test_overlay_ != nullptr || (!dimmed && !off)) {
        if (standby_active_) hide_standby();
        return;
    }

    if (!standby_active_) {
        standby_active_ = true;
        lv_obj_set_hidden(standby_overlay_, false);
        lv_obj_move_foreground(standby_overlay_);
    }
    if (dimmed && !off) {
        if (!standby_animating_) {
            standby_animating_ = true;
            lv_timer_resume(standby_timer_);
            animate_standby();
        }
    } else if (standby_animating_) {
        // When the backlight is off, stop repainting moving steam. Keep the
        // full-screen touch target in place: first tap wakes, not purchases.
        standby_animating_ = false;
        lv_timer_pause(standby_timer_);
    }
}

void UiAppImpl::animate_standby() {
    if (!standby_active_ || !standby_animating_) return;
    const uint32_t tick = lv_tick_get() / kSteamFrameMs;
    for (uint32_t i = 0; i < 3; ++i) {
        const uint32_t phase = (tick + i * 12U) % kSteamCycle;
        const int drift = phase < 18U ? static_cast<int>(phase / 6U)
                                     : static_cast<int>((kSteamCycle - phase) / 6U);
        const int x = 111 + static_cast<int>(i) * 35 + drift;
        const int y = 139 - static_cast<int>(phase) * 2;
        const uint32_t fade_in = phase < 6U ? phase * 22U : 132U;
        const uint32_t fade_out = phase > 25U ? (kSteamCycle - phase) * 12U : 132U;
        const lv_opa_t opacity = static_cast<lv_opa_t>(fade_in < fade_out ? fade_in : fade_out);
        lv_obj_set_pos(steam_[i], x, y);
        lv_obj_set_style_line_opa(steam_[i], opacity, 0);
    }
}

void UiAppImpl::standby_anim_cb(lv_timer_t* timer) {
    auto* app = static_cast<UiAppImpl*>(lv_timer_get_user_data(timer));
    if (app != nullptr) app->animate_standby();
}

void UiAppImpl::standby_wake_cb(lv_event_t* event) {
    auto* app = static_cast<UiAppImpl*>(lv_event_get_user_data(event));
    if (app != nullptr) app->wake();
}

}  // namespace kaffee::ui
