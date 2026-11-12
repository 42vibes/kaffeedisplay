# ADR 0012 — Use esp_lcd_panel_io_additions for the ST7701 init bus

Status: accepted (2026-11-11)

## Context

The Guition ESP32-4848S040 panel uses an ST7701S controller. Pixel data runs
over the parallel RGB565 bus, but the controller is configured over a 3-wire
SPI link (CS 39, SCK 48, MOSI 47). ESP-IDF 5.5 has no public panel-IO driver
for that 3-wire protocol, and the ST7701 component explicitly recommends the
`esp_lcd_panel_io_additions` helper for it. The helper only bit-bangs GPIOs and
does not add a runtime service or network dependency.

## Decision

Pin `espressif/esp_lcd_panel_io_additions` at `1.0.1` (Apache-2.0) in the main
component manifest and use it for ST7701 command transfer. The panel IO is kept
alive after initialisation and releases CS with `del_keep_cs_inactive`, so the
shared RGB pins are not disturbed.

## Consequences

- One additional build-time managed component (plus its `esp_io_expander`
  dependency) is downloaded from the Espressif registry during a build.
- No additional runtime task, memory or flash-heavy code is added.
- The version is pinned and must be reviewed together with `esp_lcd_st7701`
  when the display driver is upgraded.
