#pragma once

#include <memory>
#include <string>

#include "kaffee/domain.hpp"
#include "kaffee/settings.hpp"

namespace kaffee {

struct DisplayInfo {
    int width = 480;
    int height = 480;
    int rotation = 0;
    int brightness_percent = 100;
    bool backlight_on = true;
};

// Display, backlight and board sensors. Implemented by the ESP-IDF board
// component and by the SDL simulator/fake on the host.
class IDisplay {
  public:
    virtual ~IDisplay() = default;
    virtual Result<Unit> init() = 0;
    virtual DisplayInfo info() const = 0;
    virtual DisplaySettings settings() const = 0;
    virtual Result<Unit> apply_settings(const DisplaySettings& settings) = 0;
    virtual Result<Unit> show_test_pattern() = 0;
    virtual void set_backlight_percent(int percent, bool fade) = 0;
    virtual void wake() = 0;
    virtual bool read_chip_temperature(float* celsius) = 0;
    virtual uint64_t uptime_seconds() const = 0;
};

}  // namespace kaffee
