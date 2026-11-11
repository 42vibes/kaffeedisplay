#include "sdl_display.hpp"

#include <algorithm>
#include <cstring>

#include <SDL2/SDL.h>

#include "png_writer.hpp"

namespace sim {
namespace {

lv_display_rotation_t to_lv_rotation(int degrees) {
    switch (degrees) {
        case 90:
            return LV_DISPLAY_ROTATION_90;
        case 180:
            return LV_DISPLAY_ROTATION_180;
        case 270:
            return LV_DISPLAY_ROTATION_270;
        case 0:
        default:
            return LV_DISPLAY_ROTATION_0;
    }
}

}  // namespace

SdlDisplay::SdlDisplay() = default;

SdlDisplay::~SdlDisplay() {
    if (display_ != nullptr) {
        lv_display_delete(display_);
        display_ = nullptr;
    }
    lv_deinit();
    delete[] frame_buffer_;
    frame_buffer_ = nullptr;
    delete[] rotate_buffer_;
    rotate_buffer_ = nullptr;
    if (texture_ != nullptr) SDL_DestroyTexture(texture_);
    if (renderer_ != nullptr) SDL_DestroyRenderer(renderer_);
    if (window_ != nullptr) SDL_DestroyWindow(window_);
    SDL_Quit();
}

kaffee::Result<kaffee::Unit> SdlDisplay::init() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        return kaffee::Result<kaffee::Unit>::fail("sdl", SDL_GetError());
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    window_ = SDL_CreateWindow("Kaffeekasse Simulator", SDL_WINDOWPOS_CENTERED,
                               SDL_WINDOWPOS_CENTERED, width_, height_, SDL_WINDOW_SHOWN);
    if (window_ == nullptr) {
        return kaffee::Result<kaffee::Unit>::fail("sdl", SDL_GetError());
    }
    renderer_ =
        SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer_ == nullptr) {
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
    }
    if (renderer_ == nullptr) {
        return kaffee::Result<kaffee::Unit>::fail("sdl", SDL_GetError());
    }
    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                 width_, height_);
    if (texture_ == nullptr) {
        return kaffee::Result<kaffee::Unit>::fail("sdl", SDL_GetError());
    }

    frame_buffer_ = new uint8_t[static_cast<size_t>(width_) * height_ * 4];
    std::memset(frame_buffer_, 0, static_cast<size_t>(width_) * height_ * 4);
    rotate_buffer_ = new uint8_t[static_cast<size_t>(width_) * height_ * 4];

    lv_init();
    display_ = lv_display_create(width_, height_);
    lv_display_set_user_data(display_, this);
    lv_display_set_color_format(display_, LV_COLOR_FORMAT_ARGB8888);
    lv_display_set_flush_cb(display_, &SdlDisplay::flush_cb);
    lv_display_set_buffers(display_, frame_buffer_, nullptr,
                           static_cast<uint32_t>(width_ * height_ * 4),
                           LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_rotation(display_, LV_DISPLAY_ROTATION_0);

    touch_ = lv_indev_create();
    lv_indev_set_type(touch_, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch_, &SdlDisplay::touch_read_cb);
    lv_indev_set_user_data(touch_, this);

    return kaffee::Result<kaffee::Unit>::ok(kaffee::Unit{});
}

lv_display_t* SdlDisplay::display() const {
    return display_;
}

kaffee::Result<kaffee::Unit> SdlDisplay::set_rotation(int degrees) {
    if (degrees != 0 && degrees != 90 && degrees != 180 && degrees != 270) {
        return kaffee::Result<kaffee::Unit>::fail("rotation", "invalid rotation");
    }
    rotation_ = degrees;
    if (display_ != nullptr) {
        lv_display_set_rotation(display_, to_lv_rotation(degrees));
    }
    return kaffee::Result<kaffee::Unit>::ok(kaffee::Unit{});
}

kaffee::Result<kaffee::Unit> SdlDisplay::set_backlight(int percent) {
    brightness_ = std::clamp(percent, 5, 100);
    return kaffee::Result<kaffee::Unit>::ok(kaffee::Unit{});
}

kaffee::Result<kaffee::Unit> SdlDisplay::set_stay_awake(bool enabled) {
    stay_awake_ = enabled;
    return kaffee::Result<kaffee::Unit>::ok(kaffee::Unit{});
}

bool SdlDisplay::poll_events(std::vector<SimKey>* keys) {
    if (keys != nullptr) keys->clear();
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                quit_ = true;
                break;
            case SDL_MOUSEMOTION:
                if ((event.motion.state & SDL_BUTTON_LMASK) != 0) {
                    mouse_x_ = std::clamp(event.motion.x, 0, width_ - 1);
                    mouse_y_ = std::clamp(event.motion.y, 0, height_ - 1);
                }
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    mouse_down_ = true;
                    mouse_x_ = std::clamp(event.button.x, 0, width_ - 1);
                    mouse_y_ = std::clamp(event.button.y, 0, height_ - 1);
                }
                break;
            case SDL_MOUSEBUTTONUP:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    mouse_down_ = false;
                    mouse_x_ = std::clamp(event.button.x, 0, width_ - 1);
                    mouse_y_ = std::clamp(event.button.y, 0, height_ - 1);
                }
                break;
            case SDL_KEYDOWN:
                if (event.key.repeat != 0 || keys == nullptr) break;
                switch (event.key.keysym.sym) {
                    case SDLK_1:
                        keys->push_back(SimKey::Page1);
                        break;
                    case SDLK_2:
                        keys->push_back(SimKey::Page2);
                        break;
                    case SDLK_3:
                        keys->push_back(SimKey::Page3);
                        break;
                    case SDLK_r:
                        keys->push_back(SimKey::Rotate);
                        break;
                    case SDLK_PLUS:
                    case SDLK_EQUALS:
                    case SDLK_KP_PLUS:
                        keys->push_back(SimKey::BrightnessUp);
                        break;
                    case SDLK_MINUS:
                    case SDLK_KP_MINUS:
                        keys->push_back(SimKey::BrightnessDown);
                        break;
                    case SDLK_q:
                    case SDLK_ESCAPE:
                        quit_ = true;
                        break;
                    default:
                        break;
                }
                break;
            default:
                break;
        }
    }
    return !quit_;
}

void SdlDisplay::inject_pointer(int x, int y, bool down) {
    mouse_x_ = std::clamp(x, 0, width_ - 1);
    mouse_y_ = std::clamp(y, 0, height_ - 1);
    mouse_down_ = down;
}

bool SdlDisplay::save_screenshot(const std::string& path) {
    if (renderer_ == nullptr) return false;
    std::vector<uint8_t> argb(static_cast<size_t>(width_) * height_ * 4);
    if (SDL_RenderReadPixels(renderer_, nullptr, SDL_PIXELFORMAT_ARGB8888, argb.data(),
                             width_ * 4) != 0) {
        return false;
    }
    std::vector<uint8_t> rgb(static_cast<size_t>(width_) * height_ * 3);
    if (SDL_ConvertPixels(width_, height_, SDL_PIXELFORMAT_ARGB8888, argb.data(), width_ * 4,
                          SDL_PIXELFORMAT_RGB24, rgb.data(), width_ * 3) != 0) {
        return false;
    }
    return write_png_rgb(path, width_, height_, rgb);
}

uint32_t SdlDisplay::tick_ms() const {
    return SDL_GetTicks();
}

void SdlDisplay::flush_cb(lv_display_t* display, const lv_area_t* area, uint8_t* px_map) {
    auto* self = static_cast<SdlDisplay*>(lv_display_get_user_data(display));
    if (self == nullptr || self->texture_ == nullptr) {
        lv_display_flush_ready(display);
        return;
    }

    int w = lv_area_get_width(area);
    int h = lv_area_get_height(area);
    if (self->brightness_ < 100 && px_map != nullptr) {
        const int factor = self->brightness_;
        const size_t pixels = static_cast<size_t>(w) * h;
        for (size_t i = 0; i < pixels; ++i) {
            uint8_t* pixel = px_map + i * 4;
            pixel[0] = static_cast<uint8_t>(pixel[0] * factor / 100);
            pixel[1] = static_cast<uint8_t>(pixel[1] * factor / 100);
            pixel[2] = static_cast<uint8_t>(pixel[2] * factor / 100);
        }
    }

    SDL_Rect rect{area->x1, area->y1, w, h};
    const uint8_t* texture_pixels = px_map;
    int texture_pitch = w * 4;
    lv_display_rotation_t rotation = lv_display_get_rotation(display);
    if (rotation != LV_DISPLAY_ROTATION_0 && w == self->width_ && h == self->height_ &&
        self->rotate_buffer_ != nullptr) {
        const int W = self->width_;
        const int H = self->height_;
        uint8_t* dst = self->rotate_buffer_;
        for (int py = 0; py < H; ++py) {
            for (int px = 0; px < W; ++px) {
                int sx = px;
                int sy = py;
                switch (rotation) {
                    case LV_DISPLAY_ROTATION_90:
                        sx = py;
                        sy = H - 1 - px;
                        break;
                    case LV_DISPLAY_ROTATION_180:
                        sx = W - 1 - px;
                        sy = H - 1 - py;
                        break;
                    case LV_DISPLAY_ROTATION_270:
                        sx = W - 1 - py;
                        sy = px;
                        break;
                    default:
                        break;
                }
                std::memcpy(dst + (static_cast<size_t>(py) * W + px) * 4,
                            px_map + (static_cast<size_t>(sy) * W + sx) * 4, 4);
            }
        }
        texture_pixels = dst;
        texture_pitch = W * 4;
        rect = SDL_Rect{0, 0, W, H};
    }
    SDL_UpdateTexture(self->texture_, &rect, texture_pixels, texture_pitch);
    SDL_RenderClear(self->renderer_);
    SDL_RenderCopy(self->renderer_, self->texture_, nullptr, nullptr);
    SDL_RenderPresent(self->renderer_);
    lv_display_flush_ready(display);
}

void SdlDisplay::touch_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* self = static_cast<SdlDisplay*>(lv_indev_get_user_data(indev));
    if (self == nullptr) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    // LVGL applies the display rotation to raw input points itself.
    data->point.x = static_cast<int32_t>(self->mouse_x_);
    data->point.y = static_cast<int32_t>(self->mouse_y_);
    data->state = self->mouse_down_ ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->continue_reading = false;
}

}  // namespace sim
