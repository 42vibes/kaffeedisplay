#pragma once

#include "kaffee/domain.hpp"
#include "lvgl.h"

namespace kaffee::ui {

// Platform boundary for the LVGL display and pointer input. The simulator
// implements it with SDL2; the firmware board component can later implement it
// with esp_lvgl_port. The UI itself only uses the global LVGL display, so this
// header keeps the host build independent of ESP-IDF.
class DisplayPort {
  public:
    virtual ~DisplayPort() = default;

    // Calls lv_init() and registers display + pointer input.
    virtual Result<Unit> init() = 0;

    // Display created by init(); used by the platform loop for refresh.
    virtual lv_display_t* display() const = 0;

    // Applies a logical rotation (0/90/180/270).
    virtual Result<Unit> set_rotation(int degrees) = 0;

    // Changes the backlight. The simulator approximates it visually; the board
    // implementation drives the LEDC channel.
    virtual Result<Unit> set_backlight(int percent) = 0;
};

}  // namespace kaffee::ui
