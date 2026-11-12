#include <cstdio>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "kaffee/web_assets.hpp"
#include "nvs_flash.h"

namespace {

// Boot health gate for the dual-OTA rollback window.
//
// The OTA API writes the inactive slot, calls esp_ota_set_boot_partition() and
// reboots with rollback armed. The image is only confirmed once this check has
// passed; a crash or a failure here leaves rollback armed so the bootloader can
// return to the previous slot.
//
// TODO(main): once the real boot sequence is wired, also verify that storage
// mounted, KaffeeService was created, the display/UI task runs and the network
// manager reached a stable state. Keep this check cheap and bounded (no long
// network waits) so a broken image cannot confirm itself.
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
    ESP_LOGI("kaffee", "Kaffeekasse booting: skeleton build");
    std::printf("Kaffeekasse skeleton\n");

    if (boot_health_check()) {
        ESP_LOGI("kaffee", "health check passed, confirming OTA image");
        esp_ota_mark_app_valid_cancel_rollback();
    } else {
        ESP_LOGE("kaffee", "health check failed, leaving OTA rollback armed");
    }
}
