#pragma once

#include <atomic>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "kaffee/api.hpp"
#include "kaffee/net_manager.hpp"
#include "kaffee/service.hpp"
#include "platform_adapter.hpp"

namespace kaffee {

// Bridges network availability to the HTTP server. All listener callbacks run
// on the network timer task, so this class only enqueues work; the API server is
// started and stopped from its own worker task. That keeps httpd_stop() away
// from the Wi-Fi and HTTP tasks.
class ApiController final : public INetworkListener {
  public:
    ApiController(ApiServer& server, KaffeeService& service, PlatformAdapter& platform);

    bool start();
    void on_network_state_changed(const NetworkStatus& status) override;
    void on_ap_stopped() override;

  private:
    static void task_entry(void* arg);
    void loop();
    void apply_desired_state();

    ApiServer& server_;
    KaffeeService& service_;
    PlatformAdapter& platform_;
    TaskHandle_t task_ = nullptr;
    std::atomic<bool> ap_stopped_pending_{false};
};

}  // namespace kaffee
