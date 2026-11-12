#include "api_controller.hpp"

#include "esp_log.h"

namespace kaffee {
namespace {
constexpr const char* kTag = "kaffee.api";
constexpr uint32_t kStopGraceMs = 250;
constexpr uint32_t kRetryIntervalMs = 5000;
}  // namespace

ApiController::ApiController(ApiServer& server, KaffeeService& service, PlatformAdapter& platform)
    : server_(server), service_(service), platform_(platform) {}

bool ApiController::start() {
    if (task_ != nullptr) return true;
    return xTaskCreate(&ApiController::task_entry, "api_ctrl", 4096, this, 5, &task_) == pdPASS;
}

void ApiController::on_network_state_changed(const NetworkStatus& status) {
    platform_.set_ap_active(status.ap_active);
    if (task_ != nullptr) xTaskNotifyGive(task_);
}

void ApiController::on_ap_stopped() {
    // Deferred: the worker clears sessions through the service callback and
    // stops the server without blocking the network timer task.
    ap_stopped_pending_.store(true);
    if (task_ != nullptr) xTaskNotifyGive(task_);
}

void ApiController::task_entry(void* arg) {
    static_cast<ApiController*>(arg)->loop();
}

void ApiController::loop() {
    for (;;) {
        // Wake on network changes or every few seconds so a failed HTTP start
        // is retried while an interface is up.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kRetryIntervalMs));

        if (ap_stopped_pending_.exchange(false)) {
            platform_.notify_ap_stopped();
        }
        apply_desired_state();
    }
}

void ApiController::apply_desired_state() {
    const NetworkStatus status = service_.network_status();
    const bool should_run = status.ap_active || status.sta_connected;
    const bool running = server_.running();

    if (should_run && !running) {
        auto started = server_.start();
        if (!started) {
            ESP_LOGE(kTag, "HTTP server start failed: %s",
                     started.error ? started.error->message.c_str() : "unknown");
        } else {
            ESP_LOGI(kTag, "HTTP server started (AP %s, STA %s)", status.ap_active ? "up" : "down",
                     status.sta_connected ? "up" : "down");
        }
        return;
    }

    if (!should_run && running) {
        // Give the response that changed the network state time to flush before
        // tearing the listener down from this worker.
        vTaskDelay(pdMS_TO_TICKS(kStopGraceMs));
        const NetworkStatus latest = service_.network_status();
        if (latest.ap_active || latest.sta_connected) return;
        auto stopped = server_.stop();
        if (!stopped) {
            ESP_LOGW(kTag, "HTTP server stop failed: %s",
                     stopped.error ? stopped.error->message.c_str() : "unknown");
        } else {
            ESP_LOGI(kTag, "HTTP server stopped (no interface up)");
        }
    }
}

}  // namespace kaffee
