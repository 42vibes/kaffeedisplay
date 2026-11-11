#include <cstdio>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "nvs_flash.h"

extern "C" void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_LOGI("kaffee", "Kaffeekasse booting: skeleton build");
    std::printf("Kaffeekasse skeleton\n");
    // A future health check runs before this point; for the skeleton, a clean
    // boot is enough to cancel rollback of the running image.
    esp_ota_mark_app_valid_cancel_rollback();
}
