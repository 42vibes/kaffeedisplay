#include "serial_console.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

namespace kaffee {
namespace {

constexpr const char* kTag = "kaffee.serial";
constexpr uart_port_t kPort = UART_NUM_0;
constexpr int kRxBuffer = 4096;
constexpr size_t kMaxLine = 512;
constexpr size_t kMaxBackupBytes = 256 * 1024;
constexpr uint32_t kUnlockWindowMs = 10 * 60 * 1000;
constexpr int64_t kMinPlausibleEpoch = 1600000000LL;  // 2020-09-13
constexpr int64_t kMaxPlausibleEpoch = 4102444800LL;  // 2100-01-01

std::string lower_ascii(std::string text) {
    for (char& c : text) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return text;
}

std::string sanitize(std::string text, size_t limit = 160) {
    for (char& c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte == 0x7f) c = ' ';
    }
    if (text.size() > limit) text.resize(limit);
    return text;
}

std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> tokens;
    size_t pos = 0;
    while (pos < line.size()) {
        while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) ++pos;
        const size_t start = pos;
        while (pos < line.size() && line[pos] != ' ' && line[pos] != '\t') ++pos;
        if (pos > start) tokens.push_back(line.substr(start, pos - start));
    }
    return tokens;
}

std::string join(const std::vector<std::string>& tokens, size_t from) {
    std::string out;
    for (size_t i = from; i < tokens.size(); ++i) {
        if (!out.empty()) out.push_back(' ');
        out += tokens[i];
    }
    return out;
}

bool parse_i64(const std::string& text, int64_t& value) {
    if (text.empty()) return false;
    char* end = nullptr;
    errno = 0;
    const long long parsed = std::strtoll(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || *end != '\0') return false;
    value = static_cast<int64_t>(parsed);
    return true;
}

const char* time_source_name(const TimeInfo& time) {
    if (!time.last_source.empty()) return time.last_source.c_str();
    switch (time.source) {
        case TimeSource::Synced:
            return "synced";
        case TimeSource::Restored:
            return "restored";
        case TimeSource::Unset:
            return "unset";
    }
    return "unset";
}

}  // namespace

class SerialConsole {
  public:
    SerialConsole(KaffeeService& service, IPlatform& platform)
        : service_(service), platform_(platform) {}

    void run() {
        if (!install_uart()) return;
        ESP_LOGI(kTag, "serial console ready (115200 8N1, 'help' for commands)");
        handle_lines();
    }

  private:
    bool install_uart() {
        const esp_err_t err = uart_driver_install(kPort, kRxBuffer, 0, 0, nullptr, 0);
        if (err == ESP_ERR_INVALID_STATE) {
            // The console/log path may already own the driver; just reuse it.
            ESP_LOGW(kTag, "UART0 driver already installed; reusing it");
            return true;
        }
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "cannot install UART0 driver: %s", esp_err_to_name(err));
            return false;
        }
        uart_config_t config = {};
        config.baud_rate = 115200;
        config.data_bits = UART_DATA_8_BITS;
        config.parity = UART_PARITY_DISABLE;
        config.stop_bits = UART_STOP_BITS_1;
        config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
        config.source_clk = UART_SCLK_DEFAULT;
        const esp_err_t configured = uart_param_config(kPort, &config);
        if (configured != ESP_OK) {
            ESP_LOGE(kTag, "cannot configure UART0: %s", esp_err_to_name(configured));
            return false;
        }
        return true;
    }

    void handle_lines() {
        std::string line;
        bool discard = false;
        uint8_t byte = 0;
        for (;;) {
            const int read = uart_read_bytes(kPort, &byte, 1, pdMS_TO_TICKS(100));
            if (read != 1) {
                if (reset_pending_ &&
                    static_cast<int32_t>(reset_deadline_ - xTaskGetTickCount()) <= 0) {
                    reset_pending_ = false;
                    reply_error("factory-reset", "timeout; nothing erased");
                }
                continue;
            }

            if (byte == '\r') continue;
            if (byte == '\n') {
                if (!discard) {
                    if (reset_pending_) {
                        reset_pending_ = false;
                        if (lower_ascii(line) == "yes") {
                            perform_factory_reset();
                            return;
                        }
                        reply_error("factory-reset", "cancelled");
                    } else if (!line.empty()) {
                        handle(line);
                    }
                }
                line.clear();
                discard = false;
                continue;
            }
            if (discard) continue;
            if (line.size() >= kMaxLine) {
                // Bounded reader: drop an overlong line up to the next newline
                // instead of growing the buffer.
                discard = true;
                line.clear();
                continue;
            }
            line.push_back(static_cast<char>(byte));
        }
    }

    void write_raw(const std::string& text) {
        size_t offset = 0;
        while (offset < text.size()) {
            const size_t chunk = std::min<size_t>(512, text.size() - offset);
            const int written =
                uart_write_bytes(kPort, text.data() + offset, static_cast<size_t>(chunk));
            if (written <= 0) return;
            offset += static_cast<size_t>(written);
        }
    }

    void reply(const char* format, ...) {
        char buffer[1024];
        va_list args;
        va_start(args, format);
        const int length = std::vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        if (length < 0) return;
        const size_t size = std::min(static_cast<size_t>(length), sizeof(buffer) - 1);
        write_raw(std::string(buffer, size));
        write_raw("\r\n");
    }

    void reply_error(const std::string& code, const std::string& message) {
        reply("ERR %s %s", sanitize(code, 40).c_str(), sanitize(message).c_str());
    }

    std::string new_request_id() {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "cli_%016llx",
                      static_cast<unsigned long long>(platform_.random_u64()));
        return buffer;
    }

    // Verifies the current admin password through the normal auth path. The
    // password is never logged and the temporary session is dropped at once.
    bool verify_admin_password(const std::string& password, std::string& error_code,
                               std::string& error_message) {
        if (password.empty()) {
            error_code = "auth";
            error_message = "admin password required";
            return false;
        }
        auto session = service_.login("admin", password, "serial-cli");
        if (!session) {
            error_code = session.error ? session.error->code : "auth";
            error_message = session.error ? session.error->message : "invalid credentials";
            return false;
        }
        service_.logout(session.value->token);
        return true;
    }

    bool writes_allowed() const {
#if defined(KAFFEE_SERIAL_ALLOW_WRITES) && KAFFEE_SERIAL_ALLOW_WRITES
        return true;
#else
        return writes_until_ != 0 && static_cast<int32_t>(writes_until_ - xTaskGetTickCount()) > 0;
#endif
    }

    void handle(const std::string& line) {
        const std::vector<std::string> tokens = tokenize(line);
        if (tokens.empty()) return;

        // Only the command word is case-insensitive; arguments (passwords,
        // names, reasons) must keep their exact case.
        const std::string command = lower_ascii(tokens[0]);
        if (command == "help") return cmd_help();
        if (command == "status") return cmd_status();
        if (command == "ap") return cmd_ap(tokens);
        if (command == "setup-password") return cmd_setup_password();
        if (command == "time") return cmd_time(tokens);
        if (command == "book") return cmd_book(tokens);
        if (command == "void") return cmd_void(tokens);
        if (command == "unlock-writes") return cmd_unlock_writes(tokens);
        if (command == "backup") return cmd_backup();
        if (command == "factory-reset") return cmd_factory_reset(tokens);
        if (command == "reboot") return cmd_reboot();
        reply_error("unknown-command", command);
    }

    void cmd_help() {
        reply(
            "OK help commands=help,status,ap,setup-password,time,book,void,unlock-writes,"
            "backup,factory-reset,reboot");
    }

    void cmd_status() {
        const SystemStats stats = service_.stats();
        const std::vector<Event> events = service_.events();
        const std::vector<User> users = service_.users();
        const std::vector<Product> products = service_.products();
        const uint64_t seq = events.empty() ? 0 : events.back().seq;
        int active_users = 0;
        for (const User& user : users) {
            if (user.active) ++active_users;
        }
        int active_products = 0;
        for (const Product& product : products) {
            if (product.active) ++active_products;
        }
        const NetworkStatus& network = stats.network;
        const char* ap = network.ap_active ? "on" : (network.ap_starting ? "starting" : "off");
        const char* sta = network.sta_connected ? "on" : "off";
        const char* firmware =
            stats.firmware_version.empty() ? "unknown" : stats.firmware_version.c_str();
        const uint32_t psram_total =
            static_cast<uint32_t>(heap_caps_get_total_size(MALLOC_CAP_SPIRAM));

        reply(
            "OK status seq=%llu users=%u users_active=%d products=%u products_active=%d "
            "time=%lld time_source=%s time_trusted=%d ap=%s ap_ip=%s sta=%s sta_ip=%s "
            "uptime_s=%llu heap_free=%u heap_min=%u psram_free=%u psram_total=%u "
            "storage_used=%llu storage_total=%llu storage_mounted=%d firmware=%s "
            "drinks_today=%d",
            static_cast<unsigned long long>(seq), static_cast<unsigned>(users.size()), active_users,
            static_cast<unsigned>(products.size()), active_products,
            static_cast<long long>(stats.time.now), time_source_name(stats.time),
            stats.time.trusted ? 1 : 0, ap, network.ap_ip.c_str(), sta, network.sta_ip.c_str(),
            static_cast<unsigned long long>(stats.uptime_s), static_cast<unsigned>(stats.free_heap),
            static_cast<unsigned>(stats.min_free_heap), static_cast<unsigned>(stats.free_psram),
            static_cast<unsigned>(psram_total),
            static_cast<unsigned long long>(stats.storage.used_bytes),
            static_cast<unsigned long long>(stats.storage.total_bytes),
            stats.storage.mounted ? 1 : 0, firmware, stats.drinks_today);
    }

    void cmd_ap(const std::vector<std::string>& tokens) {
        if (tokens.size() == 1) {
            const NetworkStatus status = service_.network_status();
            const char* state = status.ap_active ? "on" : (status.ap_starting ? "starting" : "off");
            if (status.ap_active) {
                // The password is only exposed while the AP is actually up.
                reply("OK ap state=%s ssid=%s password=%s ip=%s remaining_s=%d sta=%s", state,
                      status.ap_ssid.c_str(), status.ap_password.c_str(), status.ap_ip.c_str(),
                      status.ap_remaining_s, status.sta_connected ? "on" : "off");
            } else {
                reply("OK ap state=%s ssid=%s ip=%s remaining_s=%d sta=%s", state,
                      status.ap_ssid.c_str(), status.ap_ip.c_str(), status.ap_remaining_s,
                      status.sta_connected ? "on" : "off");
            }
            return;
        }

        if (tokens[1] == "start") {
            auto started = service_.start_ap();
            if (!started) return reply_error(started.error->code, started.error->message);
            int ttl = service_.settings().network.ap_window_s;
            if (ttl <= 0) ttl = 900;
            reply("OK ap start ttl=%d", ttl);
            return;
        }
        if (tokens[1] == "stop") {
            auto stopped = service_.stop_ap();
            if (!stopped) return reply_error(stopped.error->code, stopped.error->message);
            reply("OK ap stop");
            return;
        }
        if (tokens[1] == "extend") {
            int64_t seconds = 900;
            if (tokens.size() >= 3 && !parse_i64(tokens[2], seconds)) {
                return reply_error("ap-usage", "extend needs whole seconds");
            }
            if (seconds < 1 || seconds > 86400) {
                return reply_error("ap-usage", "extend seconds must be 1..86400");
            }
            auto extended = service_.extend_ap(static_cast<int>(seconds));
            if (!extended) return reply_error(extended.error->code, extended.error->message);
            reply("OK ap extend ttl=%lld", static_cast<long long>(seconds));
            return;
        }
        reply_error("ap-usage", "ap [start|stop|extend <seconds>]");
    }

    void cmd_setup_password() {
        if (!service_.must_change_admin_password()) {
            return reply_error("setup-password", "already changed");
        }
        const std::string password = service_.setup_password();
        if (password.empty()) return reply_error("setup-password", "not available");
        reply("OK setup-password %s", password.c_str());
    }

    void cmd_time(const std::vector<std::string>& tokens) {
        if (tokens.size() == 1) {
            const TimeInfo time = service_.time_info();
            reply("OK time %lld source=%s trusted=%d last_sync=%lld",
                  static_cast<long long>(time.now), time_source_name(time), time.trusted ? 1 : 0,
                  static_cast<long long>(time.last_sync));
            return;
        }
        if (tokens[1] != "set" || tokens.size() < 3 || tokens.size() > 4) {
            return reply_error("time-usage", "time [set <epoch> [manual|browser|sntp]]");
        }
        int64_t epoch = 0;
        if (!parse_i64(tokens[2], epoch))
            return reply_error("time-usage", "epoch must be whole seconds");
        if (epoch < kMinPlausibleEpoch || epoch > kMaxPlausibleEpoch) {
            return reply_error("time-usage", "epoch outside plausible range");
        }
        std::string source = "manual";
        if (tokens.size() == 4) {
            source = tokens[3];
            if (source != "manual" && source != "browser" && source != "sntp") {
                return reply_error("time-usage", "source must be manual, browser or sntp");
            }
        }
        auto synced = service_.sync_time(epoch, source);
        if (!synced) return reply_error(synced.error->code, synced.error->message);
        reply("OK time set %lld", static_cast<long long>(epoch));
    }

    void cmd_book(const std::vector<std::string>& tokens) {
        if (!writes_allowed()) {
            return reply_error("writes-disabled",
                               "run 'unlock-writes <admin_password>' first (HIL only)");
        }
        if (tokens.size() < 3 || tokens.size() > 4) {
            return reply_error("book-usage", "book <user_id> <product_id> [request_id]");
        }
        const std::string request_id = tokens.size() == 4 ? tokens[3] : new_request_id();
        auto booked = service_.purchase(tokens[1], tokens[2], request_id);
        if (!booked) return reply_error(booked.error->code, booked.error->message);
        reply("OK booking %s %lld", booked.value->id.c_str(),
              static_cast<long long>(booked.value->amount_cents));
    }

    void cmd_void(const std::vector<std::string>& tokens) {
        if (!writes_allowed()) {
            return reply_error("writes-disabled",
                               "run 'unlock-writes <admin_password>' first (HIL only)");
        }
        if (tokens.size() < 2) return reply_error("void-usage", "void <event_id> [reason]");
        std::string reason = join(tokens, 2);
        if (reason.empty()) reason = "serial";
        auto voided = service_.void_event(tokens[1], reason, new_request_id());
        if (!voided) return reply_error(voided.error->code, voided.error->message);
        reply("OK void %s", tokens[1].c_str());
    }

    void cmd_unlock_writes(const std::vector<std::string>& tokens) {
        if (tokens.size() < 2) {
            return reply_error("auth", "usage: unlock-writes <admin_password>");
        }
        std::string code;
        std::string message;
        if (!verify_admin_password(join(tokens, 1), code, message)) {
            return reply_error(code, message);
        }
        writes_until_ = xTaskGetTickCount() + pdMS_TO_TICKS(kUnlockWindowMs);
        reply("OK unlock-writes ttl=%u", static_cast<unsigned>(kUnlockWindowMs / 1000));
    }

    void cmd_backup() {
        auto backup = service_.export_backup();
        if (!backup) return reply_error(backup.error->code, backup.error->message);
        if (backup.value->size() > kMaxBackupBytes) {
            return reply_error("backup", "too large for serial; use the admin API");
        }
        reply("OK backup %u", static_cast<unsigned>(backup.value->size()));
        write_raw(*backup.value);
        write_raw("\r\nEND backup\r\n");
        uart_wait_tx_done(kPort, pdMS_TO_TICKS(5000));
    }

    void cmd_factory_reset(const std::vector<std::string>& tokens) {
        if (tokens.size() < 2) {
            return reply_error("auth", "usage: factory-reset <admin_password>");
        }
        std::string code;
        std::string message;
        if (!verify_admin_password(join(tokens, 1), code, message)) {
            return reply_error(code, message);
        }
        reset_pending_ = true;
        reset_deadline_ = xTaskGetTickCount() + pdMS_TO_TICKS(30000);
        reply("OK factory-reset confirm=YES timeout_s=30");
    }

    void perform_factory_reset() {
        auto erased = service_.factory_reset();
        if (!erased) return reply_error(erased.error->code, erased.error->message);
        // Do not call NetworkManager::factory_reset_credentials() here: its
        // synchronous AP stop can deadlock against the Wi-Fi event task on this
        // board revision (tracked as a separate net bug). Erase the namespace
        // directly; the immediate reboot stops the AP and first boot generates
        // fresh AP credentials.
        if (!erase_network_credentials()) {
            ESP_LOGW(kTag, "network NVS erase failed; rebooting anyway");
        }
        reply("OK factory-reset");
        reboot_now();
    }

    bool erase_network_credentials() {
        nvs_handle_t handle = 0;
        esp_err_t err = nvs_open("kaffee_net", NVS_READWRITE, &handle);
        if (err == ESP_ERR_NVS_NOT_FOUND) return true;
        if (err != ESP_OK) {
            ESP_LOGW(kTag, "nvs_open(kaffee_net) failed: %s", esp_err_to_name(err));
            return false;
        }
        err = nvs_erase_all(handle);
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
        if (err != ESP_OK) {
            ESP_LOGW(kTag, "nvs erase(kaffee_net) failed: %s", esp_err_to_name(err));
            return false;
        }
        return true;
    }

    void cmd_reboot() {
        reply("OK reboot");
        reboot_now();
    }

    void reboot_now() {
        uart_wait_tx_done(kPort, pdMS_TO_TICKS(2000));
        vTaskDelay(pdMS_TO_TICKS(250));
        esp_restart();
    }

    KaffeeService& service_;
    IPlatform& platform_;
    TickType_t writes_until_ = 0;
    bool reset_pending_ = false;
    TickType_t reset_deadline_ = 0;
};

namespace {

void serial_task(void* argument) {
    static_cast<SerialConsole*>(argument)->run();
}

}  // namespace

bool start_serial_console(KaffeeService& service, IPlatform& platform) {
    static SerialConsole console(service, platform);
    if (xTaskCreate(serial_task, "serial_cli", 12288, &console, 3, nullptr) != pdPASS) {
        ESP_LOGE(kTag, "cannot create serial console task");
        return false;
    }
    return true;
}

}  // namespace kaffee
