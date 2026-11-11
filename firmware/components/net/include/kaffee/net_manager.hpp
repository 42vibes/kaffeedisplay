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
    // Called from a network/event task when availability changes. The initial
    // state may be delivered synchronously from set_listener().
    virtual void on_network_state_changed(const NetworkStatus& status) = 0;
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
