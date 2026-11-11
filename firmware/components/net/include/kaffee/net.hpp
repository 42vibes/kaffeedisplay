#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "kaffee/domain.hpp"
#include "kaffee/service.hpp"
#include "kaffee/settings.hpp"

namespace kaffee {

// Wi-Fi AP/STA, captive DNS, mDNS, SNTP and the wall clock.
class INetwork {
  public:
    virtual ~INetwork() = default;
    virtual Result<Unit> init() = 0;
    virtual NetworkStatus status() const = 0;
    virtual Result<Unit> start_ap() = 0;
    virtual Result<Unit> stop_ap() = 0;
    virtual Result<Unit> extend_ap(int seconds) = 0;
    virtual TimeInfo time_info() const = 0;
    virtual Result<Unit> sync_time(Timestamp epoch_seconds, const std::string& source) = 0;
    virtual Result<Unit> apply_settings(const NetworkSettings& settings) = 0;
    virtual uint64_t uptime_seconds() const = 0;
};

}  // namespace kaffee
