#include <memory>
#include <utility>

#include "api_controller.hpp"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "kaffee/api.hpp"
#include "kaffee/board_esp.hpp"
#include "kaffee/littlefs_store.hpp"
#include "kaffee/net_manager.hpp"
#include "kaffee/nvs_settings_store.hpp"
#include "kaffee/service.hpp"
#include "kaffee/ui.hpp"
#include "kaffee/ui_board_adapter.hpp"
#include "kaffee/web_assets.hpp"
#include "lvgl.h"
#include "nvs_flash.h"
#include "platform_adapter.hpp"

namespace {

constexpr const char* kTag = "kaffee";
constexpr const char* kMountPoint = "/littlefs";

struct BootState {
    bool nvs = false;
    bool storage = false;
    bool display = false;
    bool lvgl = false;
    bool service = false;
    bool ui = false;
    bool network = false;
};

// Boot health gate for the dual-OTA rollback window. The image is confirmed
// only after storage, service, display/LVGL, the three-page UI and the network
// manager have all come up. A failed component leaves rollback armed.
bool boot_health_check(const BootState& state) {
    const bool healthy = state.nvs && state.storage && state.display && state.lvgl &&
                         state.service && state.ui && state.network;
    ESP_LOGI(kTag,
             "boot health: nvs=%d storage=%d display=%d lvgl=%d service=%d ui=%d network=%d => %s",
             state.nvs, state.storage, state.display, state.lvgl, state.service, state.ui,
             state.network, healthy ? "ok" : "failed");
    return healthy;
}

void log_failure(const char* what, const kaffee::Error& error) {
    ESP_LOGE(kTag, "%s failed: %s (%s)", what, error.code.c_str(), error.message.c_str());
}

}  // namespace

extern "C" void app_main(void) {
    // Keep the embedded admin SPA linked. The API serves the same table through
    // kaffee_web_asset_lookup(); this reference also proves the table is part of
    // the image even before the HTTP server starts.
    (void)kaffee::web::assets();

    BootState state;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    state.nvs = true;

    ESP_LOGI(kTag, "Kaffeekasse booting");

    // Mount the LittleFS data partition declared in partitions.csv. The journal
    // store mounts/loads the ledger through KaffeeService later.
    esp_vfs_littlefs_conf_t fs_conf = {};
    fs_conf.base_path = kMountPoint;
    fs_conf.partition_label = "storage";
    fs_conf.format_if_mount_failed = true;
    fs_conf.dont_mount = false;
    err = esp_vfs_littlefs_register(&fs_conf);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "LittleFS mount failed: %s", esp_err_to_name(err));
    } else {
        size_t total = 0;
        size_t used = 0;
        if (esp_littlefs_info("storage", &total, &used) == ESP_OK) {
            ESP_LOGI(kTag, "storage mounted at %s: %u/%u bytes used", kMountPoint,
                     static_cast<unsigned>(used), static_cast<unsigned>(total));
        } else {
            ESP_LOGI(kTag, "storage mounted at %s", kMountPoint);
        }
        state.storage = true;
    }

    static kaffee::NvsSettingsStore settings_store;
    static kaffee::LittleFsEventStore event_store(kMountPoint);

    auto created_display = kaffee::EspDisplay::create();
    if (!created_display) {
        log_failure("display creation", *created_display.error);
        return;
    }
    static std::unique_ptr<kaffee::EspDisplay> display = std::move(*created_display.value);

    kaffee::Result<kaffee::Unit> result = display->init();
    if (!result) {
        log_failure("board init", *result.error);
        return;
    }
    state.display = true;

#if CONFIG_BOARD_DISPLAY_SELFTEST
    result = display->run_scanout_selftest();
    if (!result) {
        log_failure("scanout self-test", *result.error);
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
        log_failure("LVGL init", *result.error);
        return;
    }
    state.lvgl = true;

    // NetworkManager is cheap to construct; init() happens after the service so
    // the settings provider can see generated AP credentials.
    static kaffee::NetworkManager network;
    network.set_settings_provider([]() {
        auto loaded = settings_store.load();
        return loaded ? *loaded.value : kaffee::Settings{};
    });

    static kaffee::PlatformAdapter platform(*display, network);

    auto created_service = kaffee::KaffeeService::create(event_store, settings_store, platform);
    if (!created_service) {
        log_failure("service creation", *created_service.error);
        return;
    }
    static std::unique_ptr<kaffee::KaffeeService> service = std::move(*created_service.value);
    state.service = true;
    state.storage = event_store.info().mounted;

    // Apply the persisted display settings before the UI builds its pages.
    kaffee::Settings boot_settings;
    auto loaded_settings = settings_store.load();
    if (loaded_settings) {
        boot_settings = *loaded_settings.value;
    } else {
        log_failure("settings load", *loaded_settings.error);
    }
    if (auto applied = display->apply_settings(boot_settings.display); !applied) {
        log_failure("display settings apply", *applied.error);
    }

    // The UI is created and started under the LVGL lock; all subsequent LVGL
    // calls happen on the esp_lvgl_port task through its timers.
    static kaffee::ui::BoardDisplayPort display_port(
        *display, display->lv_display(),
        [](bool stay_awake) { display->set_stay_awake(stay_awake); });
    kaffee::ui::attach_display_port(&display_port);

    static std::unique_ptr<kaffee::ui::UiApp> ui_app;
    if (display->lock(2000)) {
        auto created_ui = kaffee::ui::UiApp::create(*service);
        if (created_ui) {
            ui_app = std::move(*created_ui.value);
            ui_app->start();
            state.ui = true;
            ESP_LOGI(kTag, "UI started on page 1");
        } else {
            log_failure("UI creation", *created_ui.error);
        }
        display->unlock();
    } else {
        ESP_LOGE(kTag, "cannot lock LVGL to start the UI");
    }

    auto created_api = kaffee::ApiServer::create(*service, platform);
    if (!created_api) {
        log_failure("API creation", *created_api.error);
    }
    static std::unique_ptr<kaffee::ApiServer> api_server =
        created_api ? std::move(*created_api.value) : nullptr;
    static kaffee::ApiController* api_controller = nullptr;
    if (api_server) {
        static kaffee::ApiController controller(*api_server, *service, platform);
        if (!controller.start()) {
            ESP_LOGE(kTag, "API controller task creation failed");
        } else {
            api_controller = &controller;
        }
    }

    if (auto initialized = network.init(); !initialized) {
        log_failure("network init", *initialized.error);
    } else {
        state.network = true;
        if (auto applied = network.apply_settings(boot_settings.network); !applied) {
            log_failure("network settings apply", *applied.error);
        }
        if (auto applied = network.apply_time_settings(boot_settings.time); !applied) {
            log_failure("time settings apply", *applied.error);
        }

        // Set the listener after init so the network timer task delivers the
        // initial availability notification to the API worker.
        if (api_controller != nullptr) network.set_listener(api_controller);

        // Provisioning convenience: a device with no users starts the AP for
        // the configured window. Otherwise the AP stays off until page 3 or the
        // admin API asks for it.
        if (service->users().empty()) {
            ESP_LOGI(
                kTag, "no users yet, starting provisioning AP for %ds",
                boot_settings.network.ap_window_s > 0 ? boot_settings.network.ap_window_s : 900);
            if (auto started = service->start_ap(); !started) {
                log_failure("provisioning AP start", *started.error);
            }
        }
    }

    const kaffee::BoardStats stats = display->stats();
    ESP_LOGI(kTag, "uptime %llu s, heap %u B (min %u B), PSRAM %u/%u B, touch %s",
             static_cast<unsigned long long>(stats.uptime_s), stats.free_heap, stats.min_free_heap,
             stats.free_psram, stats.total_psram, stats.touch_present ? "ready" : "missing");
    if (stats.temperature_valid) {
        ESP_LOGI(kTag, "chip temperature %.1f C", stats.chip_temperature_c);
    } else {
        ESP_LOGI(kTag, "chip temperature unavailable");
    }

    if (boot_health_check(state)) {
        ESP_LOGI(kTag, "health check passed, confirming OTA image");
        esp_ota_mark_app_valid_cancel_rollback();
    } else {
        ESP_LOGE(kTag, "health check failed, leaving OTA rollback armed");
    }
    ESP_LOGI(kTag, "boot complete");
}
