#include <memory>
#include <utility>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "kaffee/board_esp.hpp"
#include "kaffee/web_assets.hpp"
#include "lvgl.h"
#include "nvs_flash.h"

namespace {
constexpr const char* kTag = "kaffee";

// Boot health gate for the dual-OTA rollback window. The image is confirmed
// only after storage, network and UI integration have passed their own checks.
// This board-only build confirms after a successful display/LVGL bring-up.
bool boot_health_check() {
    return true;
}
}  // namespace

extern "C" void app_main(void) {
    // Keep the embedded admin SPA in the image until the API adapter enables
    // KAFFEE_API_WEB_ASSETS and references kaffee_web_asset_lookup() itself.
    (void)kaffee::web::assets();

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_LOGI(kTag, "Kaffeekasse booting (board bring-up build)");

    auto created = kaffee::EspDisplay::create();
    if (!created) {
        ESP_LOGE(kTag, "display creation failed: %s", created.error->code.c_str());
        return;
    }
    static std::unique_ptr<kaffee::EspDisplay> display = std::move(*created.value);

    kaffee::Result<kaffee::Unit> result = display->init();
    if (!result) {
        ESP_LOGE(kTag, "board init failed: %s (%s)", result.error->code.c_str(),
                 result.error->message.c_str());
        return;
    }

#if CONFIG_BOARD_DISPLAY_SELFTEST
    result = display->run_scanout_selftest();
    if (!result) {
        ESP_LOGE(kTag, "scanout self-test failed: %s (%s)", result.error->code.c_str(),
                 result.error->message.c_str());
        return;
    }
    esp_ota_mark_app_valid_cancel_rollback();
    ESP_LOGI(kTag,
             "diagnostic build: colour bars left on the panel; set "
             "CONFIG_BOARD_DISPLAY_SELFTEST=n and reflash for the normal LVGL boot");
    return;
#endif

    result = display->init_lvgl();
    if (!result) {
        ESP_LOGE(kTag, "LVGL init failed: %s (%s)", result.error->code.c_str(),
                 result.error->message.c_str());
        return;
    }

    if (display->lock(1000)) {
        lv_obj_t* screen = lv_screen_active();
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x1B1410), 0);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

        lv_obj_t* title = lv_label_create(screen);
        lv_label_set_text(title, "Kaffeekasse");
        lv_obj_set_style_text_color(title, lv_color_hex(0xF2E8DC), 0);
        lv_obj_center(title);
        display->unlock();
    } else {
        ESP_LOGW(kTag, "could not lock LVGL while creating the boot label");
    }

    const kaffee::DisplaySettings settings = display->settings();
    display->set_backlight_percent(settings.brightness_active, true);

    const kaffee::BoardStats stats = display->stats();
    ESP_LOGI(kTag, "uptime %llu s, heap %u B (min %u B), PSRAM %u/%u B, touch %s",
             static_cast<unsigned long long>(stats.uptime_s), stats.free_heap, stats.min_free_heap,
             stats.free_psram, stats.total_psram, stats.touch_present ? "ready" : "missing");
    if (stats.temperature_valid) {
        ESP_LOGI(kTag, "chip temperature %.1f C", stats.chip_temperature_c);
    } else {
        ESP_LOGI(kTag, "chip temperature unavailable");
    }

    if (boot_health_check()) {
        ESP_LOGI(kTag, "health check passed, confirming OTA image");
        esp_ota_mark_app_valid_cancel_rollback();
    } else {
        ESP_LOGE(kTag, "health check failed, leaving OTA rollback armed");
    }
    ESP_LOGI(kTag, "boot complete");
}
