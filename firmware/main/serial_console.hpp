#pragma once

#include "kaffee/service.hpp"

namespace kaffee {

class EspDisplay;

// Line-based serial console on UART0 (CH340, 115200 8N1). It is a development,
// recovery and HIL interface; runtime operation never depends on it. The task
// never calls LVGL: every command goes through KaffeeService/IPlatform.
//
// Protocol and commands are documented in docs/serial-cli.md.
bool start_serial_console(KaffeeService& service, IPlatform& platform, EspDisplay& display);

}  // namespace kaffee
