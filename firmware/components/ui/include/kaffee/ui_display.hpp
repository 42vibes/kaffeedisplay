#pragma once

#include "kaffee/domain.hpp"
#include "lvgl.h"

namespace kaffee::ui {

// Platform boundary for the LVGL display and pointer input. The simulator
// implements it with SDL2; the firmware board component can later implement it
// with esp_lvgl_port (or use BoardDisplayPort in ui_board_adapter.hpp).
class DisplayPort {
  public:
    virtual ~DisplayPort() = default;

    // Calls lv_init() and registers display + pointer input. Firmware boards
    // that already initialised LVGL through esp_lvgl_port return ok().
    virtual Result<Unit> init() = 0;

    // Display created by init(); used by the platform loop for refresh.
    virtual lv_display_t* display() const = 0;

    // Applies a logical rotation (0/90/180/270).
    virtual Result<Unit> set_rotation(int degrees) = 0;

    // Changes the backlight. The simulator approximates it visually; the board
    // implementation drives the LEDC channel.
    virtual Result<Unit> set_backlight(int percent) = 0;

    // Holds the panel awake while the admin AP is active. The board adapter
    // must feed this into its own activity/deadline tracking; LVGL's
    // lv_display_trigger_activity() alone does not update the board timer.
    virtual Result<Unit> set_stay_awake(bool enabled) = 0;
};

// The UI has no DisplayPort constructor parameter (ui.hpp is frozen), so the
// integration layer attaches the active port once during boot. The UI calls
// set_stay_awake() on it when the AP state changes.
void attach_display_port(DisplayPort* port);
void detach_display_port(DisplayPort* port);
DisplayPort* active_display_port();

}  // namespace kaffee::ui
