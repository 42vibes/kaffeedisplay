#pragma once

#include <functional>
#include <utility>

#include "kaffee/board.hpp"
#include "kaffee/ui_display.hpp"

namespace kaffee::ui {

// Small firmware integration seam that adapts the board's IDisplay to the UI's
// DisplayPort without pulling board headers into the UI implementation.
//
// T-009 owns the boot wiring in firmware/main. Intended order:
//   1. EspDisplay::create(); display->init(); display->init_lvgl();
//   2. BoardDisplayPort port(*display, display->lv_display(), stay_awake_fn);
//      ui::attach_display_port(&port);
//   3. service = KaffeeService::create(...);
//   4. app = UiApp::create(*service);  // inside lvgl lock
//      app->start();
//
// The board may already manage LVGL through esp_lvgl_port; init() is therefore
// a no-op. Keep UiApp calls on the LVGL task (or inside display->lock()).
class BoardDisplayPort final : public DisplayPort {
  public:
    // Implemented by the board once PR #9 adds an explicit hold API. Until
    // then the fallback only calls wake() when enabled, which is not a hold.
    using StayAwakeFn = std::function<void(bool)>;

    BoardDisplayPort(IDisplay& display, lv_display_t* lv_display, StayAwakeFn stay_awake = {})
        : display_(display), display_handle_(lv_display), stay_awake_(std::move(stay_awake)) {}

    Result<Unit> init() override {
        // The board initialises LVGL itself (esp_lvgl_port); nothing to do.
        return Result<Unit>::ok(Unit{});
    }

    lv_display_t* display() const override {
        return display_handle_;
    }

    Result<Unit> set_rotation(int degrees) override {
        if (degrees != 0 && degrees != 90 && degrees != 180 && degrees != 270) {
            return Result<Unit>::fail("ui/rotation", "invalid rotation");
        }
        DisplaySettings settings = display_.settings();
        settings.rotation = degrees;
        return display_.apply_settings(settings);
    }

    Result<Unit> set_backlight(int percent) override {
        display_.set_backlight_percent(percent, true);
        return Result<Unit>::ok(Unit{});
    }

    Result<Unit> set_stay_awake(bool enabled) override {
        if (stay_awake_) {
            stay_awake_(enabled);
        } else if (enabled) {
            // Fallback only: keeps the panel from sleeping right now, but does
            // not hold the board's inactivity deadline. Wire StayAwakeFn.
            display_.wake();
        }
        return Result<Unit>::ok(Unit{});
    }

  private:
    IDisplay& display_;
    lv_display_t* display_handle_ = nullptr;
    StayAwakeFn stay_awake_;
};

}  // namespace kaffee::ui
