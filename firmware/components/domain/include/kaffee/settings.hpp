#pragma once

#include <cstdint>
#include <string>

namespace kaffee {

struct DisplaySettings {
    int rotation = 0;                // 0, 90, 180, 270
    int brightness_active = 100;     // percent, 5..100
    int brightness_idle = 20;        // percent, 1..100
    int dim_timeout_s = 60;          // 0 = never dim
    int screen_off_timeout_s = 600;  // 0 = never turn off
};

struct NetworkSettings {
    std::string ap_ssid;      // empty => Kaffeekasse-XXXX derived from MAC
    std::string ap_password;  // empty => generated on first boot
    int ap_window_s = 900;    // 15 minutes
    std::string hostname = "kaffeekasse";
    bool sta_enabled = false;
    std::string sta_ssid;
    std::string sta_password;
    bool napt_enabled = false;  // share STA uplink with AP clients
};

struct TimeSettings {
    std::string timezone = "Europe/Berlin";
    bool sntp_enabled = true;
    std::string sntp_server = "pool.ntp.org";
};

struct AppSettings {
    std::string currency = "EUR";
    std::string locale = "de-DE";
    bool guest_enabled = true;
    int max_open_debt_cents = 0;            // 0 = no hard limit
    int low_balance_warning_cents = -1000;  // warn below -10 EUR
};

struct AdminCredentials {
    std::string username = "admin";
    std::string password_hash;  // PBKDF2-HMAC-SHA256, hex
    std::string salt;           // 16 random bytes, hex
    uint32_t iterations = 50000;
};

struct Settings {
    DisplaySettings display;
    NetworkSettings network;
    TimeSettings time;
    AppSettings app;
    AdminCredentials admin;
};

}  // namespace kaffee
