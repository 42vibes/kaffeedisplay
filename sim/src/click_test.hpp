#pragma once

#include <cstdint>

#include "fake_backend.hpp"
#include "kaffee/ui.hpp"
#include "kaffee/ui_control.hpp"
#include "sdl_display.hpp"

namespace sim {

// Drives the real LVGL input path (pointer indev -> event callbacks) at fixed
// coordinates. It does not call UiControl debug_* state injection methods.
bool run_click_test(FakeBackend& backend, kaffee::ui::UiApp& app, kaffee::ui::UiControl& control,
                    SdlDisplay& display, uint32_t& tick);

}  // namespace sim
