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

// Smooth line art: cubic curves are sampled offline into static LVGL points.
// No geometry allocation or bezier evaluation occurs in the animation timer.
// The paths stay in flash and keep their lifetime for lv_line_set_points().
static lv_point_precise_t kSteamPath[] = {
    {14, 82}, {16, 80}, {16, 77}, {17, 75}, {17, 73}, {17, 71}, {17, 68}, {16, 66},
    {15, 64}, {14, 62}, {13, 59}, {11, 57}, {10, 55}, {10, 52}, {9, 50},  {9, 48},
    {8, 46},  {8, 43},  {9, 41},  {9, 39},  {9, 36},  {10, 34}, {10, 32}, {11, 30},
    {12, 27}, {12, 25}, {13, 23}, {14, 21}, {14, 18}, {15, 16}, {15, 14}, {16, 11},
    {16, 9},  {16, 7},  {16, 5},  {15, 2},  {14, 0},
};
constexpr uint32_t kSteamPathCount = 37;

static lv_point_precise_t kCupRim[] = {
    {211, 183}, {211, 184}, {209, 185}, {207, 186}, {204, 187}, {200, 188}, {196, 189}, {190, 190},
    {185, 190}, {178, 191}, {172, 191}, {165, 192}, {157, 192}, {150, 192}, {143, 192}, {135, 192},
    {128, 191}, {122, 191}, {115, 190}, {110, 190}, {104, 189}, {100, 188}, {96, 187},  {93, 186},
    {91, 185},  {89, 184},  {89, 183},  {89, 182},  {91, 181},  {93, 180},  {96, 179},  {100, 178},
    {104, 177}, {110, 176}, {115, 176}, {122, 175}, {128, 175}, {135, 174}, {143, 174}, {150, 174},
    {157, 174}, {165, 174}, {172, 175}, {178, 175}, {185, 176}, {190, 176}, {196, 177}, {200, 178},
    {204, 179}, {207, 180}, {209, 181}, {211, 182}, {211, 183},
};
constexpr uint32_t kCupRimCount = 53;

static lv_point_precise_t kCupBody[] = {
    {90, 186},  {90, 190},  {90, 193},  {90, 197},  {90, 201},  {91, 206},  {91, 210},  {92, 214},
    {93, 218},  {94, 222},  {95, 226},  {97, 229},  {98, 233},  {100, 236}, {102, 239}, {103, 241},
    {105, 244}, {107, 246}, {110, 248}, {112, 250}, {115, 252}, {118, 253}, {121, 255}, {124, 256},
    {127, 257}, {131, 258}, {135, 259}, {139, 259}, {143, 260}, {148, 260}, {153, 260}, {158, 260},
    {163, 259}, {167, 258}, {171, 257}, {175, 256}, {179, 255}, {182, 253}, {185, 251}, {188, 249},
    {191, 247}, {193, 245}, {195, 243}, {197, 240}, {199, 238}, {201, 235}, {203, 231}, {204, 227},
    {205, 223}, {206, 219}, {207, 215}, {208, 211}, {209, 207}, {209, 203}, {210, 199}, {210, 195},
    {210, 192}, {210, 188}, {210, 185},
};
constexpr uint32_t kCupBodyCount = 59;

static lv_point_precise_t kCupHighlight[] = {
    {106, 203}, {106, 206}, {106, 209}, {106, 211}, {106, 214}, {107, 217}, {107, 219}, {108, 221},
    {109, 223}, {109, 226}, {110, 228}, {111, 230}, {113, 231}, {114, 233}, {115, 235},
};
constexpr uint32_t kCupHighlightCount = 15;

static lv_point_precise_t kHandleOuter[] = {
    {207, 194}, {210, 192}, {213, 191}, {217, 190}, {221, 189}, {224, 188}, {228, 188}, {232, 189},
    {236, 189}, {239, 190}, {243, 191}, {247, 192}, {250, 194}, {253, 196}, {256, 198}, {258, 201},
    {260, 204}, {262, 207}, {263, 210}, {264, 213}, {265, 216}, {265, 219}, {264, 223}, {264, 226},
    {263, 229}, {262, 232}, {260, 235}, {258, 237}, {256, 240}, {254, 242}, {251, 244}, {249, 246},
    {246, 248}, {242, 249}, {239, 249}, {235, 250}, {231, 250}, {227, 249}, {223, 249}, {219, 247},
    {215, 246}, {210, 244}, {206, 242},
};
constexpr uint32_t kHandleOuterCount = 43;

static lv_point_precise_t kHandleInner[] = {
    {210, 204}, {214, 203}, {218, 202}, {222, 201}, {225, 201}, {229, 202}, {233, 202},
    {236, 203}, {239, 205}, {242, 206}, {244, 208}, {246, 210}, {247, 212}, {248, 216},
    {249, 219}, {249, 222}, {249, 225}, {248, 228}, {247, 230}, {245, 233}, {243, 234},
    {241, 236}, {238, 237}, {235, 238}, {232, 239}, {230, 239}, {227, 239}, {225, 239},
    {223, 239}, {221, 239}, {219, 238}, {217, 237}, {215, 236}, {213, 235}, {211, 233},
};
constexpr uint32_t kHandleInnerCount = 35;

static lv_point_precise_t kSaucerTop[] = {
    {83, 272},  {88, 271},  {93, 271},  {98, 270},  {104, 269}, {110, 269}, {116, 268}, {122, 268},
    {129, 268}, {135, 268}, {142, 268}, {149, 268}, {156, 268}, {162, 268}, {169, 268}, {176, 268},
    {182, 268}, {189, 269}, {195, 269}, {201, 270}, {207, 271}, {212, 271}, {217, 272},
};
constexpr uint32_t kSaucerTopCount = 23;

static lv_point_precise_t kSaucerFront[] = {
    {83, 272},  {87, 274},  {91, 275},  {97, 276},  {102, 277}, {108, 278}, {115, 279}, {121, 280},
    {128, 280}, {136, 281}, {143, 281}, {150, 281}, {158, 281}, {165, 281}, {172, 280}, {179, 280},
    {186, 279}, {192, 278}, {198, 277}, {204, 276}, {209, 275}, {213, 274}, {217, 272},
};
constexpr uint32_t kSaucerFrontCount = 23;

static lv_point_precise_t kSaucerFoot[] = {
    {116, 290}, {120, 291}, {125, 291}, {130, 292}, {135, 292}, {140, 292}, {145, 292}, {150, 292},
    {155, 292}, {160, 292}, {165, 292}, {170, 292}, {175, 291}, {179, 291}, {184, 290},
};
constexpr uint32_t kSaucerFootCount = 15;

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

    // The refined demitasse is symmetrical, tapered and gently rounded.
    // Draw the handle first so the porcelain silhouette conceals its joins.
    stroke(scene, kHandleOuter, kHandleOuterCount, 0xCBAB89, 3);
    stroke(scene, kHandleInner, kHandleInnerCount, 0x876E55, 2, LV_OPA_80);
    stroke(scene, kCupBody, kCupBodyCount, 0xDDC7AB, 3);
    stroke(scene, kCupRim, kCupRimCount, 0xEADBC6, 2);
    stroke(scene, kCupHighlight, kCupHighlightCount, 0x866C57, 2, LV_OPA_60);
    stroke(scene, kSaucerTop, kSaucerTopCount, 0xC2A17C, 2);
    stroke(scene, kSaucerFront, kSaucerFrontCount, 0xAE8B6C, 2);
    stroke(scene, kSaucerFoot, kSaucerFootCount, 0x6D5847, 2, LV_OPA_70);

    for (int i = 0; i < 3; ++i) {
        steam_[i] = stroke(scene, kSteamPath, kSteamPathCount, 0xE9DFD1, 2, LV_OPA_TRANSP);
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
