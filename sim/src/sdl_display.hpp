#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "kaffee/ui_display.hpp"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace sim {

enum class SimKey {
    Page1,
    Page2,
    Page3,
    Rotate,
    BrightnessUp,
    BrightnessDown,
};

class SdlDisplay final : public kaffee::ui::DisplayPort {
  public:
    SdlDisplay();
    ~SdlDisplay() override;

    kaffee::Result<kaffee::Unit> init() override;
    lv_display_t* display() const override;
    kaffee::Result<kaffee::Unit> set_rotation(int degrees) override;
    kaffee::Result<kaffee::Unit> set_backlight(int percent) override;

    // Polls SDL events and updates pointer state. Returns false on quit.
    bool poll_events(std::vector<SimKey>* keys);
    bool save_screenshot(const std::string& path);
    uint32_t tick_ms() const;
    int rotation() const {
        return rotation_;
    }
    int brightness() const {
        return brightness_;
    }
    void request_quit() {
        quit_ = true;
    }

  private:
    static void flush_cb(lv_display_t* display, const lv_area_t* area, uint8_t* px_map);
    static void touch_read_cb(lv_indev_t* indev, lv_indev_data_t* data);

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    lv_display_t* display_ = nullptr;
    lv_indev_t* touch_ = nullptr;
    uint8_t* frame_buffer_ = nullptr;
    uint8_t* rotate_buffer_ = nullptr;
    int width_ = 480;
    int height_ = 480;
    int rotation_ = 0;
    int brightness_ = 100;
    int mouse_x_ = 240;
    int mouse_y_ = 240;
    bool mouse_down_ = false;
    bool quit_ = false;
};

}  // namespace sim
