#pragma once

#include <memory>

#include "kaffee/service.hpp"

namespace kaffee {

// HTTP server: admin SPA, REST API, CSV exports, backup/restore and OTA.
// The server is started only while a network interface is up (AP or STA).
class ApiServer {
  public:
    static Result<std::unique_ptr<ApiServer>> create(KaffeeService& service, IPlatform& platform);
    virtual ~ApiServer() = default;
    virtual Result<Unit> start() = 0;
    virtual Result<Unit> stop() = 0;
    virtual bool running() const = 0;
};

}  // namespace kaffee
