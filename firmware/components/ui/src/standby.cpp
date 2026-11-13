#include "ui_internal.hpp"

#include "strings.hpp"
#include "theme.hpp"

namespace kaffee::ui {

namespace {

// A nine-second cycle with three strokes offset by one third of the period.
// Each stroke gently fades in, rises, fades out, then stays invisible until
// its next turn. One LVGL timer is shared by all three; no bitmap frames.
constexpr uint32_t kSteamFrameMs = 80;
constexpr uint32_t kSteamCycleFrames = 120;
constexpr uint32_t kSteamStaggerFrames = kSteamCycleFrames / 3;
constexpr uint32_t kSteamVisibleFrames = 68;
constexpr int kSteamPeakOpacity = 194;

// Thin, gently winding steam, rather than the previous short zig-zag marks.
// LVGL retains the point pointer, so all paths have static storage duration.
static lv_point_precise_t kSteamPath[] = {
    {12, 76}, {8, 63}, {10, 53}, {15, 42}, {14, 31}, {9, 20}, {10, 9}, {14, 1},
};
static lv_point_precise_t kCupRim[] = {
    {91, 186}, {99, 180}, {118, 177}, {144, 176}, {171, 177}, {192, 180}, {203, 186},
};
static lv_point_precise_t kCupBody[] = {
    {96, 188},  {97, 207},  {101, 226}, {109, 242}, {120, 249}, {139, 253},
    {167, 253}, {185, 249}, {195, 242}, {203, 227}, {207, 208}, {208, 188},
};
static lv_point_precise_t kCupHighlight[] = {
    {109, 207},
    {113, 226},
    {119, 235},
};
static lv_point_precise_t kHandleOuter[] = {
    {207, 192}, {220, 188}, {235, 190}, {245, 201}, {248, 213},
    {246, 226}, {239, 236}, {227, 240}, {207, 238},
};
static lv_point_precise_t kSaucer[] = {
    {88, 265},
    {101, 269},
    {201, 269},
    {218, 264},
};
static lv_point_precise_t kSaucerHighlight[] = {
    {121, 275},
    {185, 275},
};

lv_obj_t* stroke(lv_obj_t* parent, lv_point_precise_t* points, uint32_t count, uint32_t color,
                 int thickness, lv_opa_t opacity = LV_OPA_COVER) {
    lv_obj_t* line = lv_line_create(parent);
    lv_line_set_points(line, points, count);
    lv_obj_set_style_line_width(line, thickness, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    lv_obj_set_style_line_color(line, theme::color(color), 0);
    lv_obj_set_style_line_opa(line, opacity, 0);
    lv_obj_set_clickable(line, false);
    return line;
}

}  // namespace

void UiAppImpl::build_standby() {
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
    lv_obj_set_size(scene, 300, 340);
    lv_obj_center(scene);
    lv_obj_set_clickable(scene, false);
    lv_obj_set_scrollable(scene, false);

    lv_obj_t* title = theme::make_label(scene, strings::STANDBY_TITLE, theme::font_body(),
                                        theme::color(theme::COLOR_ACCENT));
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 24);

    // Restrained line-art porcelain: no chunky solid rectangle for the cup.
    // The handle is drawn first so its start/end disappear behind the rim.
    stroke(scene, kHandleOuter, 9, 0xC9AA87, 4);
    stroke(scene, kCupBody, 12, 0xDCC4A6, 3);
    stroke(scene, kCupRim, 7, 0xE4D1B8, 3);
    stroke(scene, kCupHighlight, 3, 0x7F6350, 2, LV_OPA_70);
    stroke(scene, kSaucer, 4, 0xB89A78, 3);
    stroke(scene, kSaucerHighlight, 2, 0x745E4D, 2, LV_OPA_70);

    for (int i = 0; i < 3; ++i) {
        steam_[i] = stroke(scene, kSteamPath, 8, 0xE9DFD1, 3, LV_OPA_TRANSP);
    }

    lv_obj_t* hint = theme::make_label(scene, strings::STANDBY_TOUCH_HINT, theme::font_small(),
                                       theme::color(theme::COLOR_MUTED));
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -12);

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

    // The board remains the sole owner of actual dim/off backlight levels.
    const DisplaySettings settings = backend_.display_settings();
    const uint32_t idle_ms = lv_display_get_inactive_time(lv_display_get_default());
    const bool dimmed = settings.dim_timeout_s > 0 &&
                        idle_ms >= static_cast<uint32_t>(settings.dim_timeout_s) * 1000U;
    const bool off = settings.screen_off_timeout_s > 0 &&
                     idle_ms >= static_cast<uint32_t>(settings.screen_off_timeout_s) * 1000U;

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
        // No more framebuffer updates while the physical display is off.
        // The overlay still consumes the first touch (no accidental booking).
        standby_animating_ = false;
        lv_timer_pause(standby_timer_);
    }
}

void UiAppImpl::animate_standby() {
    if (!standby_active_ || !standby_animating_) return;

    const uint32_t tick = lv_tick_get() / kSteamFrameMs;
    for (uint32_t i = 0; i < 3; ++i) {
        const uint32_t phase = (tick + i * kSteamStaggerFrames) % kSteamCycleFrames;
        lv_opa_t opacity = LV_OPA_TRANSP;
        if (phase < 18U) {
            opacity = static_cast<lv_opa_t>(phase * kSteamPeakOpacity / 18U);
        } else if (phase < 30U) {
            opacity = static_cast<lv_opa_t>(kSteamPeakOpacity);
        } else if (phase < kSteamVisibleFrames) {
            opacity = static_cast<lv_opa_t>((kSteamVisibleFrames - phase) * kSteamPeakOpacity /
                                            (kSteamVisibleFrames - 30U));
        }

        // Each strand rises ~56px and drifts side-to-side by only 3px.
        // Gradual opacity makes the start/end blend into the dark background.
        const int rise =
            static_cast<int>((phase < kSteamVisibleFrames ? phase : kSteamVisibleFrames) * 56U /
                             kSteamVisibleFrames);
        const int wobble_phase = static_cast<int>(phase % 32U);
        const int wobble = wobble_phase < 16 ? wobble_phase / 5 - 2 : (32 - wobble_phase) / 5 - 2;
        lv_obj_set_pos(steam_[i], 108 + static_cast<int>(i) * 36 + wobble, 107 - rise);
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
