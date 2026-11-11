# ADR 0002 — ESP-IDF firmware stack

Status: accepted (2026-11-11)

## Context

We need native Wi-Fi AP+STA, an HTTP server, OTA, NVS, LittleFS, precise display
timing and a clean module structure for parallel contributors.

## Decision

Use **ESP-IDF 5.x** (pinned via PlatformIO or the `espressif/idf` Docker image),
C++17, `esp_lcd` for the ST7701S RGB panel, `esp_lcd_touch_gt911`,
`esp_lvgl_port` + LVGL 9, `esp_http_server`, `esp_littlefs` and `esp_ota_ops`.
Arduino-ESP32 is not used.

## Consequences

- More setup than Arduino, but native APIs for the appliance features.
- `idf.py` or Docker is required for firmware builds.
- Domain and UI code stay framework-independent and build on the host/simulator.
