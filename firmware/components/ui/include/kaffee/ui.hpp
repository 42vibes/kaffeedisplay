#pragma once

#include <memory>

#include "kaffee/service.hpp"

namespace kaffee::ui {

// LVGL application. Header deliberately avoids exposing lvgl types so the host
// simulator and firmware can share it.
class UiApp {
  public:
    static Result<std::unique_ptr<UiApp>> create(IUiBackend& backend);
    virtual ~UiApp() = default;

    // Creates the three pages and switches to page 1. Must be called from the
    // LVGL task (or the simulator thread) after the display is initialised.
    virtual void start() = 0;

    // Reloads users, products, balances and leaderboard after a ledger change.
    virtual void refresh_data() = 0;

    // Shows the rotation/touch test pattern for 15 seconds.
    virtual void show_test_pattern() = 0;

    // Wakes the display and restores active brightness.
    virtual void wake() = 0;

    // Optional periodic hook for timeout handling; safe to call from lv_timer.
    virtual void tick() = 0;
};

}  // namespace kaffee::ui
