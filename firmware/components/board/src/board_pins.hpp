#pragma once

#include <cstdint>

namespace kaffee::board {

// Guition ESP32-4848S040 / JC4848W540. See docs/hardware.md for the source of
// these values; they are also the pins verified on the unit in the repo.
constexpr int kLcdWidth = 480;
constexpr int kLcdHeight = 480;

constexpr int kPinLcdDe = 18;
constexpr int kPinLcdVsync = 17;
constexpr int kPinLcdHsync = 16;
constexpr int kPinLcdPclk = 21;

// RGB565 data lines, ordered B0..B4, G0..G5, R0..R4 as expected by esp_lcd.
constexpr int kLcdDataGpio[16] = {
    4,  5,  6,  7,  15,      // B0..B4
    8,  20, 3,  46, 9,  10,  // G0..G5
    11, 12, 13, 14, 0,       // R0..R4
};

// 3-wire SPI is only used to write the ST7701 init sequence.
constexpr int kPinLcdSpiCs = 39;
constexpr int kPinLcdSpiSck = 48;
constexpr int kPinLcdSpiMosi = 47;

constexpr int kPinBacklight = 38;

constexpr int kPinTouchSda = 19;
constexpr int kPinTouchScl = 45;
constexpr int kTouchI2cPort = 0;
constexpr uint8_t kTouchAddress = 0x5D;
constexpr uint8_t kTouchAddressBackup = 0x14;

// Known-good timing for the Guition 4848S040 / ST7701S RGB panel
// (Arduino_GFX / ESPHome): 12 MHz PCLK, 8/20/10 horizontal and 8/10/10
// vertical blanking. The previous 16 MHz timing produced a partial blue
// scanout on real hardware.
constexpr int kLcdPclkHz = 12 * 1000 * 1000;
constexpr int kLcdHsyncPulse = 8;
constexpr int kLcdHsyncBackPorch = 20;
constexpr int kLcdHsyncFrontPorch = 10;
constexpr int kLcdVsyncPulse = 8;
constexpr int kLcdVsyncBackPorch = 10;
constexpr int kLcdVsyncFrontPorch = 10;

}  // namespace kaffee::board
