#pragma once

#include <functional>
#include <memory>
#include <string>

#include "kaffee/net.hpp"
#include "kaffee/net_pure.hpp"

namespace kaffee {

// Notified whenever an interface becomes available or unavailable. The HTTP
// server subscribes to start/stop listening without polling.
class INetworkListener {
  public:
    virtual ~INetworkListener() = default;
    // Called from the network timer task when availability changes. The initial
    // state may be delivered from that task shortly after set_listener().
    // Keep callbacks non-blocking; enqueue work onto the consumer's own task.
    virtual void on_network_state_changed(const NetworkStatus& status) = 0;
    // Called from the network timer task after the AP is confirmed stopped.
    // The HTTP server must be stopped from the consumer's task, not here.
    virtual void on_ap_stopped() {}
};

// ESP-IDF implementation of INetwork. Construction is cheap; all hardware is
// brought up by init(). The manager owns Wi-Fi, DHCP, captive DNS, mDNS, SNTP
// and the wall clock. It never serves HTTP.
class NetworkManager final : public INetwork {
  public:
    using SettingsProvider = std::function<Settings()>;

    NetworkManager();
    explicit NetworkManager(Settings settings);
    explicit NetworkManager(SettingsProvider provider);
    ~NetworkManager() override;

    NetworkManager(const NetworkManager&) = delete;
    NetworkManager& operator=(const NetworkManager&) = delete;

    // INetwork. start_ap()/stop_ap() return after asking the Wi-Fi driver; the
    // actual interface transition is reported through INetworkListener.
    Result<Unit> init() override;
    NetworkStatus status() const override;
    Result<Unit> start_ap() override;
    Result<Unit> stop_ap() override;
    Result<Unit> extend_ap(int seconds) override;
    TimeInfo time_info() const override;
    Result<Unit> sync_time(Timestamp epoch_seconds, const std::string& source) override;
    Result<Unit> apply_settings(const NetworkSettings& settings) override;
    uint64_t uptime_seconds() const override;

    // Extra wiring for main/api. Not part of the frozen INetwork contract.
    void set_listener(INetworkListener* listener);
    void set_settings_provider(SettingsProvider provider);
    Result<Unit> apply_time_settings(const TimeSettings& settings);
    // Erases the `kaffee_net` NVS namespace (AP credentials, stored wall clock)
    // and regenerates AP credentials. Storage factory reset must call this or
    // erase the namespace directly.
    Result<Unit> factory_reset_credentials();

    // Effective credentials and QR/URL helpers. The AP password is only
    // meaningful while the AP is active.
    std::string ap_ssid() const;
    std::string ap_password() const;
    std::string ap_url() const;
    std::string sta_url() const;
    std::string wifi_qr_payload() const;
    bool ap_active() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace kaffee
