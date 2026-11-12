#pragma once

#include <cstdint>

namespace kaffee::touch {

struct Point {
    int32_t x = 0;
    int32_t y = 0;

    constexpr bool operator==(const Point& other) const {
        return x == other.x && y == other.y;
    }
};

// Rotates a raw panel coordinate into the logical LVGL coordinate space.
//
// The mapping mirrors LVGL's lv_display_rotate_point() and esp_lvgl_port's
// software rotation (lvgl_port_rotate_area):
//   logical(x, y) -> panel(x, y)              at   0
//   logical(x, y) -> panel(y, H - 1 - x)      at  90
//   logical(x, y) -> panel(W - 1 - x, H - 1 - y) at 180
//   logical(x, y) -> panel(W - 1 - y, x)      at 270
// Inverting those gives the raw-panel -> logical mapping below. The panel is
// square (480x480), but width/height are parameters so the helper stays honest.
constexpr Point transform(int32_t raw_x, int32_t raw_y, int32_t width, int32_t height,
                          int rotation) {
    switch (rotation) {
        case 90:
            return {height - 1 - raw_y, raw_x};
        case 180:
            return {width - 1 - raw_x, height - 1 - raw_y};
        case 270:
            return {raw_y, width - 1 - raw_x};
        case 0:
        default:
            return {raw_x, raw_y};
    }
}

// Inverts the mapping: logical LVGL coordinates back to raw panel coordinates.
// Used by tests and by any code that has to place panel-space markers.
constexpr Point inverse(Point logical, int32_t width, int32_t height, int rotation) {
    switch (rotation) {
        case 90:
            return {logical.y, height - 1 - logical.x};
        case 180:
            return {width - 1 - logical.x, height - 1 - logical.y};
        case 270:
            return {width - 1 - logical.y, logical.x};
        case 0:
        default:
            return {logical.x, logical.y};
    }
}

// Clamps a point to the drawable area. GT911 occasionally reports coordinates
// one pixel outside the panel at the edges.
constexpr Point clamp(Point p, int32_t width, int32_t height) {
    if (p.x < 0) {
        p.x = 0;
    } else if (p.x >= width) {
        p.x = width - 1;
    }
    if (p.y < 0) {
        p.y = 0;
    } else if (p.y >= height) {
        p.y = height - 1;
    }
    return p;
}

// Converts a DisplaySettings-style rotation into the canonical value.
constexpr int normalize_rotation(int rotation) {
    switch (rotation) {
        case 90:
        case 180:
        case 270:
            return rotation;
        default:
            return 0;
    }
}

}  // namespace kaffee::touch
