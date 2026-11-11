#include "kaffee/net_manager.hpp"

#include <sys/time.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <string_view>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "mdns.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "kaffee/net_pure.hpp"

namespace kaffee {
namespace {

constexpr const char* kTag = "kaffee.net";
constexpr const char* kNvsNamespace = "kaffee_net";
constexpr uint8_t kApMaxClients = 4;
constexpr uint8_t kDhcpOfferDns = 0x02;  // DHCPS_OFFER_DNS
constexpr int64_t kTimePersistIntervalS = 5 * 60;
constexpr BaseType_t kNetTaskPriority = 5;
constexpr uint32_t kNetTaskStack = 3072;
constexpr uint32_t kDnsTaskStack = 4096;
constexpr uint32_t kDnsPort = 53;

void copy_cstr(char* destination, size_t capacity, std::string_view source) {
    if (capacity == 0) {
        return;
    }
    const size_t length = std::min(source.size(), capacity - 1);
    std::memcpy(destination, source.data(), length);
    destination[length] = '\0';
}

// Wi-Fi SSID fields are fixed-size arrays and are not necessarily terminated.
void copy_ssid(uint8_t* destination, size_t capacity, std::string_view source) {
    if (capacity == 0) {
        return;
    }
    const size_t length = std::min(source.size(), capacity);
    std::memcpy(destination, source.data(), length);
    if (length < capacity) {
        std::memset(destination + length, 0, capacity - length);
    }
}

std::string nvs_get_string(nvs_handle_t handle, const char* key) {
    size_t length = 0;
    if (nvs_get_str(handle, key, nullptr, &length) != ESP_OK || length == 0) {
        return {};
    }
    std::string value(length, '\0');
    if (nvs_get_str(handle, key, value.data(), &length) != ESP_OK) {
        return {};
    }
    if (!value.empty() && value.back() == '\0') {
        value.pop_back();
    }
    return value;
}

bool nvs_get_i64(nvs_handle_t handle, const char* key, int64_t& value) {
    int64_t stored = 0;
    size_t size = sizeof(stored);
    if (nvs_get_blob(handle, key, &stored, &size) != ESP_OK || size != sizeof(stored)) {
        return false;
    }
    value = stored;
    return true;
}

std::string normalize_hostname(const std::string& requested) {
    std::string hostname = requested.empty() ? std::string(kDefaultHostname) : requested;
    constexpr std::string_view kLocalSuffix = ".local";
    if (hostname.size() > kLocalSuffix.size() &&
        hostname.compare(hostname.size() - kLocalSuffix.size(), kLocalSuffix.size(),
                         kLocalSuffix) == 0) {
        hostname.erase(hostname.size() - kLocalSuffix.size());
    }
    return hostname;
}

std::string default_ap_ssid() {
    uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK &&
        esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP) != ESP_OK) {
        const uint32_t random = esp_random();
        mac[4] = static_cast<uint8_t>(random >> 8);
        mac[5] = static_cast<uint8_t>(random);
    }
    char ssid[32] = {};
    std::snprintf(ssid, sizeof(ssid), "Kaffeekasse-%02X%02X", mac[4], mac[5]);
    return ssid;
}

std::string generate_ap_password() {
    static constexpr char kAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    constexpr size_t kLength = 12;
    std::string password;
    password.reserve(kLength);
    for (size_t i = 0; i < kLength; ++i) {
        password.push_back(kAlphabet[esp_random() % (sizeof(kAlphabet) - 1)]);
    }
    return password;
}

Timestamp uptime_now() {
    return static_cast<Timestamp>(esp_timer_get_time() / 1000000LL);
}

// Minimal captive DNS responder. A queries get the portal address, AAAA gets an
// empty NOERROR answer (so clients fall back to A), everything else NXDOMAIN.
bool build_dns_response(const uint8_t* query, size_t query_length, uint8_t* response,
                        size_t capacity, size_t& response_length) {
    if (query_length < 12 || capacity < 12) {
        return false;
    }
    if ((query[2] & 0x80) != 0) {  // ignore responses
        return false;
    }
    if ((query[2] & 0x78) != 0) {  // only standard queries
        return false;
    }
    const uint16_t question_count = static_cast<uint16_t>((query[4] << 8) | query[5]);
    if (question_count != 1) {
        return false;
    }

    size_t offset = 12;
    bool terminated = false;
    while (offset < query_length) {
        const uint8_t label_length = query[offset];
        if (label_length == 0) {
            ++offset;
            terminated = true;
            break;
        }
        if ((label_length & 0xC0) != 0) {  // compression is not valid in a question
            return false;
        }
        offset += 1 + label_length;
        if (offset > query_length) {
            return false;
        }
    }
    if (!terminated || offset + 4 > query_length) {
        return false;
    }

    const uint16_t qtype = static_cast<uint16_t>((query[offset] << 8) | query[offset + 1]);
    const uint16_t qclass = static_cast<uint16_t>((query[offset + 2] << 8) | query[offset + 3]);
    const bool is_a = qtype == 1 && qclass == 1;
    const bool is_aaaa = qtype == 28 && qclass == 1;
    const size_t question_length = offset + 4 - 12;
    const size_t answer_length = is_a ? 16 : 0;
    const size_t total = 12 + question_length + answer_length;
    if (total > capacity) {
        return false;
    }

    std::memset(response, 0, total);
    std::memcpy(response, query, 12);
    // QR + AA + copied RD; RA set; NOERROR or NXDOMAIN.
    response[2] = static_cast<uint8_t>(0x84 | (query[2] & 0x01));
    response[3] = (is_a || is_aaaa) ? 0x80 : 0x83;
    response[4] = 0;
    response[5] = 1;
    response[6] = 0;
    response[7] = is_a ? 1 : 0;
    std::memcpy(response + 12, query + 12, question_length);

    if (is_a) {
        uint8_t* answer = response + 12 + question_length;
        answer[0] = 0xC0;
        answer[1] = 0x0C;  // pointer to the question name
        answer[2] = 0;
        answer[3] = 1;  // TYPE A
        answer[4] = 0;
        answer[5] = 1;  // CLASS IN
        answer[6] = 0;
        answer[7] = 0;
        answer[8] = 0;
        answer[9] = 60;  // TTL
        answer[10] = 0;
        answer[11] = 4;
        answer[12] = 192;
        answer[13] = 168;
        answer[14] = 4;
        answer[15] = 1;
    }

    response_length = total;
    return true;
}

}  // namespace

struct NetworkManager::Impl {
    mutable std::mutex mutex;
    NetworkManager* owner = nullptr;
    Settings settings;
    NetworkManager::SettingsProvider provider;
    INetworkListener* listener = nullptr;

    bool initialized = false;
    bool wifi_initialized = false;
    bool wifi_started = false;
    bool sta_connected = false;
    bool ap_requested = false;
    bool ap_active = false;

    esp_netif_t* sta_netif = nullptr;
    esp_netif_t* ap_netif = nullptr;

    esp_event_handler_instance_t wifi_handler = nullptr;
    esp_event_handler_instance_t ip_handler = nullptr;

    std::string hostname = kDefaultHostname;
    std::string ap_ssid;
    std::string ap_password;
    std::string sta_ip;
    int sta_rssi = 0;

    ApTimer ap_timer{kApDefaultWindowSeconds};

    bool time_valid = false;
    TimeSource time_source = TimeSource::Unset;
    Timestamp last_sync = 0;
    std::string last_source = "unset";
    bool trusted = false;
    Timestamp base_epoch = 0;
    int64_t base_uptime_us = 0;
    Timestamp persisted_epoch = 0;

    bool sntp_started = false;
    bool mdns_started = false;
    bool napt_running = false;

    std::atomic<bool> component_running{true};
    std::atomic<bool> dns_requested{false};
    std::atomic<bool> dns_task_exited{false};
    std::atomic<bool> timer_task_exited{false};
    TaskHandle_t dns_task = nullptr;
    TaskHandle_t timer_task = nullptr;

    esp_timer_handle_t reconnect_timer = nullptr;
    int reconnect_attempts = 0;

    bool nvs_ready = false;
    nvs_handle_t nvs = 0;

    static std::atomic<Impl*> sntp_instance;

    bool sta_configured_locked() const {
        return settings.network.sta_enabled && !settings.network.sta_ssid.empty() &&
               settings.network.sta_password.size() >= 8;
    }

    bool sta_ready_locked() const {
        return sta_configured_locked() && sta_netif != nullptr;
    }

    Timestamp now_locked() const {
        if (!time_valid) {
            return 0;
        }
        const int64_t elapsed_us = esp_timer_get_time() - base_uptime_us;
        return base_epoch + (elapsed_us > 0 ? elapsed_us / 1000000LL : 0);
    }

    NetworkStatus build_status_locked() const {
        NetworkStatus status;
        status.ap_active = ap_active;
        status.ap_starting = ap_requested && !ap_active;
        status.ap_remaining_s = ap_timer.remaining(uptime_now());
        status.ap_ssid = ap_ssid;
        // The password is only exposed while the AP is up (security.md).
        status.ap_password = (ap_active || ap_requested) ? ap_password : std::string{};
        status.ap_ip = ap_active ? std::string(kCaptivePortalIp) : std::string{};
        status.sta_connected = sta_connected;
        status.sta_ip = sta_ip;
        status.sta_rssi = sta_rssi;
        return status;
    }

    TimeInfo build_time_info_locked() const {
        TimeInfo info;
        info.now = now_locked();
        info.source = time_source;
        info.last_sync = last_sync;
        info.last_source = last_source;
        info.trusted = trusted;
        return info;
    }

    void emit_state() {
        INetworkListener* callback = nullptr;
        NetworkStatus status;
        {
            std::lock_guard<std::mutex> lock(mutex);
            callback = listener;
            status = build_status_locked();
        }
        if (callback != nullptr) {
            callback->on_network_state_changed(status);
        }
    }

    void open_nvs() {
        esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &nvs);
        if (err == ESP_ERR_NVS_NOT_INITIALIZED) {
            if (nvs_flash_init() == ESP_OK) {
                err = nvs_open(kNvsNamespace, NVS_READWRITE, &nvs);
            }
        }
        if (err != ESP_OK) {
            ESP_LOGW(kTag, "NVS unavailable (%s); AP/time persistence disabled",
                     esp_err_to_name(err));
            nvs_ready = false;
            return;
        }
        nvs_ready = true;
    }

    void persist_ap_credentials_locked() {
        if (!nvs_ready) {
            return;
        }
        const esp_err_t err = nvs_set_str(nvs, "ap_ssid", ap_ssid.c_str()) == ESP_OK &&
                                      nvs_set_str(nvs, "ap_password", ap_password.c_str()) == ESP_OK
                                  ? nvs_commit(nvs)
                                  : ESP_FAIL;
        if (err != ESP_OK) {
            ESP_LOGW(kTag, "failed to persist AP credentials (%s)", esp_err_to_name(err));
        }
    }

    void load_or_create_ap_credentials_locked() {
        std::string stored_ssid;
        std::string stored_password;
        if (nvs_ready) {
            stored_ssid = nvs_get_string(nvs, "ap_ssid");
            stored_password = nvs_get_string(nvs, "ap_password");
        }

        std::string ssid =
            settings.network.ap_ssid.empty() ? stored_ssid : settings.network.ap_ssid;
        if (ssid.empty() || ssid.size() > 32) {
            ssid = default_ap_ssid();
        }
        std::string password =
            settings.network.ap_password.empty() ? stored_password : settings.network.ap_password;
        if (password.size() < 8 || password.size() > 63) {
            if (!password.empty()) {
                ESP_LOGW(kTag, "stored AP password has an invalid length; generating a new one");
            }
            password = generate_ap_password();
        }

        ap_ssid = ssid;
        ap_password = password;
        if (stored_ssid != ap_ssid || stored_password != ap_password) {
            persist_ap_credentials_locked();
        }
    }

    void restore_time_locked() {
        Timestamp epoch = 0;
        Timestamp last = 0;
        if (nvs_ready && nvs_get_i64(nvs, "time_epoch", epoch) && is_time_sane(epoch)) {
            time_valid = true;
            base_epoch = epoch;
            base_uptime_us = esp_timer_get_time();
            time_source = TimeSource::Restored;
            trusted = false;
            last_sync = nvs_get_i64(nvs, "time_last_sync", last) ? last : epoch;
            last_source = "restored";
            persisted_epoch = epoch;
            ESP_LOGI(kTag, "restored wall clock at %lld", static_cast<long long>(epoch));
        } else {
            time_valid = false;
            base_epoch = 0;
            time_source = TimeSource::Unset;
            trusted = false;
            last_sync = 0;
            last_source = "unset";
            persisted_epoch = 0;
            ESP_LOGI(kTag, "no usable stored wall clock (unset)");
        }
    }

    void persist_time_locked(Timestamp epoch) {
        persisted_epoch = epoch;
        if (!nvs_ready) {
            return;
        }
        const int64_t stored_epoch = epoch;
        const int64_t stored_last_sync = last_sync;
        const uint8_t quality = static_cast<uint8_t>(time_source);
        esp_err_t err = nvs_set_blob(nvs, "time_epoch", &stored_epoch, sizeof(stored_epoch));
        if (err == ESP_OK) {
            err = nvs_set_blob(nvs, "time_last_sync", &stored_last_sync, sizeof(stored_last_sync));
        }
        if (err == ESP_OK) {
            err = nvs_set_u8(nvs, "time_quality", quality);
        }
        if (err == ESP_OK) {
            err = nvs_set_str(nvs, "time_source", last_source.c_str());
        }
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
        if (err != ESP_OK) {
            ESP_LOGW(kTag, "failed to persist time (%s)", esp_err_to_name(err));
        }
    }

    Result<Unit> set_time_locked(Timestamp epoch_seconds, const std::string& source) {
        if (epoch_seconds <= 0) {
            return Result<Unit>::fail("time_invalid", "timestamp must be positive");
        }
        const Timestamp reference = trusted ? now_locked() : 0;
        if (!is_time_sane(epoch_seconds, reference)) {
            return Result<Unit>::fail("time_invalid", "timestamp outside plausible range");
        }
        base_epoch = epoch_seconds;
        base_uptime_us = esp_timer_get_time();
        time_valid = true;
        time_source = TimeSource::Synced;
        last_sync = epoch_seconds;
        last_source = source.empty() ? "manual" : source;
        trusted = true;
        struct timeval tv = {};
        tv.tv_sec = epoch_seconds;
        settimeofday(&tv, nullptr);
        persist_time_locked(epoch_seconds);
        return Result<Unit>::ok(Unit{});
    }

    void apply_timezone_locked() {
        // Europe/Berlin with EU DST rules; the only timezone supported in v1.
        if (!settings.time.timezone.empty() && settings.time.timezone != "Europe/Berlin") {
            ESP_LOGW(kTag, "timezone '%s' is not supported; using Europe/Berlin",
                     settings.time.timezone.c_str());
        }
        setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
        tzset();
    }

    void start_sntp_locked() {
        if (sntp_started || !settings.time.sntp_enabled) {
            return;
        }
        if (!sta_connected || settings.time.sntp_server.empty()) {
            return;
        }
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, settings.time.sntp_server.c_str());
        esp_sntp_set_time_sync_notification_cb(&Impl::sntp_sync_cb);
        esp_sntp_init();
        sntp_started = true;
        ESP_LOGI(kTag, "SNTP started (%s)", settings.time.sntp_server.c_str());
    }

    void stop_sntp_locked() {
        if (!sntp_started) {
            return;
        }
        esp_sntp_stop();
        sntp_started = false;
    }

    void ensure_mdns_locked() {
        if (mdns_started || (!ap_active && !sta_connected)) {
            return;
        }
        esp_err_t err = mdns_init();
        if (err != ESP_OK) {
            ESP_LOGW(kTag, "mDNS init failed (%s)", esp_err_to_name(err));
            return;
        }
        const std::string mdns_host = normalize_hostname(hostname);
        err = mdns_hostname_set(mdns_host.c_str());
        if (err == ESP_OK) {
            mdns_instance_name_set("Kaffeekasse");
            mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
            mdns_started = true;
            ESP_LOGI(kTag, "mDNS started: %s.local", mdns_host.c_str());
        } else {
            ESP_LOGW(kTag, "mDNS hostname failed (%s)", esp_err_to_name(err));
            mdns_free();
        }
    }

    void stop_mdns_locked() {
        if (!mdns_started) {
            return;
        }
        mdns_free();
        mdns_started = false;
    }

    void enable_napt_locked() {
        if (!settings.network.napt_enabled || !ap_active || !sta_connected || napt_running ||
            ap_netif == nullptr || sta_netif == nullptr) {
            return;
        }
#if defined(CONFIG_LWIP_IPV4_NAPT) && CONFIG_LWIP_IPV4_NAPT
        const esp_err_t err = esp_netif_napt_enable(ap_netif);
        if (err == ESP_OK) {
            napt_running = true;
            ESP_LOGI(kTag, "NAPT enabled for AP clients");
        } else {
            ESP_LOGW(kTag, "NAPT enable failed (%s)", esp_err_to_name(err));
        }
#else
        ESP_LOGW(kTag, "NAPT requested but CONFIG_LWIP_IPV4_NAPT is disabled");
#endif
    }

    void disable_napt_locked() {
#if defined(CONFIG_LWIP_IPV4_NAPT) && CONFIG_LWIP_IPV4_NAPT
        if (napt_running && ap_netif != nullptr) {
            esp_netif_napt_disable(ap_netif);
        }
#endif
        napt_running = false;
    }

    void start_dns_locked() {
        dns_requested.store(true);
        if (dns_task != nullptr) {
            xTaskNotifyGive(dns_task);
        }
    }

    void stop_dns_locked() {
        dns_requested.store(false);
    }

    void schedule_reconnect_locked() {
        if (!sta_ready_locked() || !wifi_started || reconnect_timer == nullptr) {
            return;
        }
        esp_timer_stop(reconnect_timer);
        const int shift = std::min(reconnect_attempts, 5);
        int delay_s = 1 << shift;
        if (delay_s > 60) {
            delay_s = 60;
        }
        ++reconnect_attempts;
        esp_timer_start_once(reconnect_timer, static_cast<uint64_t>(delay_s) * 1000000ULL);
    }

    Result<Unit> ensure_wifi_started_locked() {
        if (wifi_started) {
            return Result<Unit>::ok(Unit{});
        }
        const esp_err_t err = esp_wifi_start();
        if (err == ESP_OK || err == ESP_ERR_WIFI_STATE) {
            wifi_started = true;
            return Result<Unit>::ok(Unit{});
        }
        return Result<Unit>::fail("wifi_start", esp_err_to_name(err));
    }

    void ensure_sta_netif_locked() {
        if (sta_netif != nullptr) {
            return;
        }
        sta_netif = esp_netif_create_default_wifi_sta();
        if (sta_netif != nullptr) {
            esp_netif_set_hostname(sta_netif, hostname.c_str());
        }
    }

    Result<Unit> apply_sta_locked() {
        if (!sta_configured_locked()) {
            return Result<Unit>::ok(Unit{});
        }
        ensure_sta_netif_locked();
        if (sta_netif == nullptr) {
            return Result<Unit>::fail("sta_netif", "could not create STA netif");
        }
        if (wifi_started) {
            esp_wifi_disconnect();
        }
        wifi_config_t config = {};
        copy_ssid(config.sta.ssid, sizeof(config.sta.ssid), settings.network.sta_ssid);
        copy_cstr(reinterpret_cast<char*>(config.sta.password), sizeof(config.sta.password),
                  settings.network.sta_password);
        config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        config.sta.pmf_cfg.capable = true;
        config.sta.pmf_cfg.required = false;

        esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &config);
        if (err != ESP_OK) {
            return Result<Unit>::fail("sta_config", esp_err_to_name(err));
        }
        const wifi_mode_t mode = (ap_requested || ap_active) ? WIFI_MODE_APSTA : WIFI_MODE_STA;
        err = esp_wifi_set_mode(mode);
        if (err != ESP_OK && err != ESP_ERR_WIFI_STATE) {
            return Result<Unit>::fail("sta_mode", esp_err_to_name(err));
        }
        auto started = ensure_wifi_started_locked();
        if (!started) {
            return started;
        }
        err = esp_wifi_connect();
        if (err != ESP_OK && err != ESP_ERR_WIFI_CONN && err != ESP_ERR_WIFI_STATE) {
            ESP_LOGW(kTag, "Wi-Fi connect failed (%s)", esp_err_to_name(err));
            schedule_reconnect_locked();
        }
        return Result<Unit>::ok(Unit{});
    }

    void disable_sta_locked() {
        if (reconnect_timer != nullptr) {
            esp_timer_stop(reconnect_timer);
        }
        reconnect_attempts = 0;
        if (wifi_started) {
            esp_wifi_disconnect();
        }
        sta_connected = false;
        sta_ip.clear();
        sta_rssi = 0;
        stop_sntp_locked();
        disable_napt_locked();
        if (!wifi_started) {
            return;
        }
        if (ap_requested || ap_active) {
            esp_wifi_set_mode(WIFI_MODE_AP);
        } else {
            esp_wifi_set_mode(WIFI_MODE_NULL);
            esp_wifi_stop();
            wifi_started = false;
        }
    }

    Result<Unit> configure_ap_locked() {
        if (ap_netif == nullptr) {
            return Result<Unit>::fail("ap_netif", "AP netif is not available");
        }
        esp_netif_dhcps_stop(ap_netif);
        esp_netif_ip_info_t ip = {};
        ip.ip.addr = htonl(0xC0A80401);  // 192.168.4.1
        ip.gw.addr = htonl(0xC0A80401);
        ip.netmask.addr = htonl(0xFFFFFF00);  // 255.255.255.0
        esp_err_t err = esp_netif_set_ip_info(ap_netif, &ip);
        if (err != ESP_OK) {
            return Result<Unit>::fail("ap_ip", esp_err_to_name(err));
        }

        wifi_config_t config = {};
        const size_t ssid_length = std::min(ap_ssid.size(), sizeof(config.ap.ssid));
        copy_ssid(config.ap.ssid, sizeof(config.ap.ssid), ap_ssid);
        config.ap.ssid_len = static_cast<uint8_t>(ssid_length);
        copy_cstr(reinterpret_cast<char*>(config.ap.password), sizeof(config.ap.password),
                  ap_password);
        config.ap.channel = 6;
        config.ap.max_connection = kApMaxClients;
        config.ap.authmode = WIFI_AUTH_WPA2_PSK;
        config.ap.pmf_cfg.capable = true;
        config.ap.pmf_cfg.required = false;
        config.ap.beacon_interval = 100;
        err = esp_wifi_set_config(WIFI_IF_AP, &config);
        if (err != ESP_OK) {
            return Result<Unit>::fail("ap_config", esp_err_to_name(err));
        }
        return Result<Unit>::ok(Unit{});
    }

    void start_ap_dhcp_locked() {
        if (ap_netif == nullptr) {
            return;
        }
        // The AP netif is up now. Reconfigure its DNS offer so captive clients
        // always resolve through the local responder.
        esp_netif_dhcps_stop(ap_netif);
        esp_netif_dns_info_t dns = {};
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        dns.ip.u_addr.ip4.addr = htonl(0xC0A80401);
        esp_netif_set_dns_info(ap_netif, ESP_NETIF_DNS_MAIN, &dns);
        uint8_t offer_dns = kDhcpOfferDns;
        esp_netif_dhcps_option(ap_netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer_dns,
                               sizeof(offer_dns));
        const esp_err_t err = esp_netif_dhcps_start(ap_netif);
        if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) {
            ESP_LOGW(kTag, "DHCP server start failed (%s)", esp_err_to_name(err));
        }
    }

    void handle_wifi_event(int32_t event_id, void* event_data) {
        bool notify = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            switch (event_id) {
                case WIFI_EVENT_STA_START:
                    if (sta_ready_locked()) {
                        esp_wifi_connect();
                    }
                    break;
                case WIFI_EVENT_STA_CONNECTED:
                    ESP_LOGI(kTag, "STA connected");
                    break;
                case WIFI_EVENT_STA_DISCONNECTED: {
                    const auto* event =
                        static_cast<const wifi_event_sta_disconnected_t*>(event_data);
                    ESP_LOGW(kTag, "STA disconnected (reason %d)",
                             event != nullptr ? event->reason : -1);
                    sta_connected = false;
                    sta_ip.clear();
                    sta_rssi = 0;
                    stop_sntp_locked();
                    disable_napt_locked();
                    if (!ap_active) {
                        stop_mdns_locked();
                    }
                    schedule_reconnect_locked();
                    notify = true;
                    break;
                }
                case WIFI_EVENT_AP_START:
                    ap_active = true;
                    ap_requested = true;
                    if (!ap_timer.active()) {
                        ap_timer.start(uptime_now(), settings.network.ap_window_s);
                    }
                    start_ap_dhcp_locked();
                    start_dns_locked();
                    ensure_mdns_locked();
                    enable_napt_locked();
                    ESP_LOGI(kTag, "AP started: %s", ap_ssid.c_str());
                    notify = true;
                    break;
                case WIFI_EVENT_AP_STOP:
                    ap_active = false;
                    ap_requested = false;
                    ap_timer.stop();
                    stop_dns_locked();
                    disable_napt_locked();
                    if (ap_netif != nullptr) {
                        esp_netif_dhcps_stop(ap_netif);
                    }
                    if (!sta_connected) {
                        stop_mdns_locked();
                    }
                    ESP_LOGI(kTag, "AP stopped");
                    notify = true;
                    break;
                case WIFI_EVENT_AP_STACONNECTED:
                    ESP_LOGI(kTag, "AP client connected");
                    break;
                case WIFI_EVENT_AP_STADISCONNECTED:
                    ESP_LOGI(kTag, "AP client disconnected");
                    break;
                default:
                    break;
            }
        }
        if (notify) {
            emit_state();
        }
    }

    void handle_ip_event(int32_t event_id, void* event_data) {
        bool notify = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (event_id == IP_EVENT_STA_GOT_IP) {
                const auto* event = static_cast<const ip_event_got_ip_t*>(event_data);
                char address[16] = {};
                esp_ip4addr_ntoa(&event->ip_info.ip, address, sizeof(address));
                sta_ip = address;
                sta_connected = true;
                reconnect_attempts = 0;
                wifi_ap_record_t record = {};
                if (esp_wifi_sta_get_ap_info(&record) == ESP_OK) {
                    sta_rssi = record.rssi;
                }
                if (sta_netif != nullptr) {
                    esp_netif_set_default_netif(sta_netif);
                }
                start_sntp_locked();
                ensure_mdns_locked();
                enable_napt_locked();
                ESP_LOGI(kTag, "STA got IP %s", sta_ip.c_str());
                notify = true;
            } else if (event_id == IP_EVENT_STA_LOST_IP) {
                sta_connected = false;
                sta_ip.clear();
                sta_rssi = 0;
                stop_sntp_locked();
                disable_napt_locked();
                if (!ap_active) {
                    stop_mdns_locked();
                }
                ESP_LOGW(kTag, "STA lost IP");
                notify = true;
            }
        }
        if (notify) {
            emit_state();
        }
    }

    void on_sntp_sync(Timestamp epoch_seconds) {
        std::lock_guard<std::mutex> lock(mutex);
        auto result = set_time_locked(epoch_seconds, "sntp");
        if (!result) {
            ESP_LOGW(kTag, "SNTP timestamp rejected");
        }
    }

    static void wifi_event_handler(void* arg, esp_event_base_t, int32_t event_id,
                                   void* event_data) {
        static_cast<Impl*>(arg)->handle_wifi_event(event_id, event_data);
    }

    static void ip_event_handler(void* arg, esp_event_base_t, int32_t event_id, void* event_data) {
        static_cast<Impl*>(arg)->handle_ip_event(event_id, event_data);
    }

    static void reconnect_timer_cb(void* arg) {
        auto* self = static_cast<Impl*>(arg);
        std::lock_guard<std::mutex> lock(self->mutex);
        if (self->sta_ready_locked() && self->wifi_started) {
            esp_wifi_connect();
        }
    }

    static void sntp_sync_cb(struct timeval* tv) {
        Impl* self = Impl::sntp_instance.load();
        if (self != nullptr && tv != nullptr) {
            self->on_sntp_sync(static_cast<Timestamp>(tv->tv_sec));
        }
    }

    static void timer_task_main(void* arg) {
        auto* self = static_cast<Impl*>(arg);
        while (self->component_running.load()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            if (!self->component_running.load()) {
                break;
            }
            bool expired = false;
            {
                std::lock_guard<std::mutex> lock(self->mutex);
                expired = self->ap_timer.tick(uptime_now());
                if (self->time_valid && self->trusted) {
                    const Timestamp current = self->now_locked();
                    if (current >= self->persisted_epoch + kTimePersistIntervalS) {
                        self->persist_time_locked(current);
                    }
                }
            }
            if (expired) {
                ESP_LOGI(kTag, "AP window expired");
                if (self->owner != nullptr) {
                    self->owner->stop_ap();
                }
            }
        }
        self->timer_task_exited.store(true);
        vTaskDelete(nullptr);
    }

    static void dns_task_main(void* arg) {
        auto* self = static_cast<Impl*>(arg);
        while (self->component_running.load()) {
            if (!self->dns_requested.load()) {
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
                continue;
            }

            const int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (socket_fd < 0) {
                ESP_LOGW(kTag, "captive DNS socket failed");
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            int reuse = 1;
            setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
            struct timeval timeout = {};
            timeout.tv_usec = 300000;
            setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

            struct sockaddr_in address = {};
            address.sin_family = AF_INET;
            address.sin_port = htons(kDnsPort);
            address.sin_addr.s_addr = htonl(0xC0A80401);  // only the AP address
            if (bind(socket_fd, reinterpret_cast<struct sockaddr*>(&address), sizeof(address)) !=
                0) {
                ESP_LOGW(kTag, "captive DNS bind failed");
                close(socket_fd);
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }
            ESP_LOGI(kTag, "captive DNS listening on %s:%u", kCaptivePortalIp, kDnsPort);

            while (self->component_running.load() && self->dns_requested.load()) {
                uint8_t query[512] = {};
                struct sockaddr_in from = {};
                socklen_t from_length = sizeof(from);
                const int received =
                    recvfrom(socket_fd, query, sizeof(query), 0,
                             reinterpret_cast<struct sockaddr*>(&from), &from_length);
                if (received <= 0) {
                    continue;
                }
                uint8_t response[512] = {};
                size_t response_length = 0;
                if (build_dns_response(query, static_cast<size_t>(received), response,
                                       sizeof(response), response_length)) {
                    sendto(socket_fd, response, response_length, 0,
                           reinterpret_cast<struct sockaddr*>(&from), from_length);
                }
            }
            close(socket_fd);
        }
        self->dns_task_exited.store(true);
        vTaskDelete(nullptr);
    }
};

std::atomic<NetworkManager::Impl*> NetworkManager::Impl::sntp_instance{nullptr};

NetworkManager::NetworkManager() : NetworkManager(Settings{}) {}

NetworkManager::NetworkManager(Settings settings) : impl_(std::make_unique<Impl>()) {
    impl_->owner = this;
    impl_->settings = std::move(settings);
}

NetworkManager::NetworkManager(SettingsProvider provider) : impl_(std::make_unique<Impl>()) {
    impl_->owner = this;
    impl_->provider = std::move(provider);
}

NetworkManager::~NetworkManager() {
    if (!impl_) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->component_running.store(false);
        impl_->dns_requested.store(false);
        impl_->ap_timer.stop();
        impl_->ap_active = false;
        impl_->ap_requested = false;
        impl_->stop_sntp_locked();
        impl_->stop_mdns_locked();
        impl_->disable_napt_locked();
        if (impl_->reconnect_timer != nullptr) {
            esp_timer_stop(impl_->reconnect_timer);
        }
        if (impl_->sntp_instance.load() == impl_.get()) {
            impl_->sntp_instance.store(nullptr);
        }
    }

    // Give the worker tasks a chance to observe the stop flag. They do not hold
    // the mutex while blocking, so this cannot deadlock.
    const bool wait_for_dns = impl_->dns_task != nullptr;
    const bool wait_for_timer = impl_->timer_task != nullptr;
    for (int i = 0; i < 100; ++i) {
        const bool dns_done = !wait_for_dns || impl_->dns_task_exited.load();
        const bool timer_done = !wait_for_timer || impl_->timer_task_exited.load();
        if (dns_done && timer_done) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->wifi_handler != nullptr) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, impl_->wifi_handler);
        impl_->wifi_handler = nullptr;
    }
    if (impl_->ip_handler != nullptr) {
        esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, impl_->ip_handler);
        impl_->ip_handler = nullptr;
    }
    if (impl_->wifi_started) {
        esp_wifi_stop();
        impl_->wifi_started = false;
    }
    if (impl_->wifi_initialized) {
        esp_wifi_deinit();
        impl_->wifi_initialized = false;
    }
    if (impl_->ap_netif != nullptr) {
        esp_netif_destroy(impl_->ap_netif);
        impl_->ap_netif = nullptr;
    }
    if (impl_->sta_netif != nullptr) {
        esp_netif_destroy(impl_->sta_netif);
        impl_->sta_netif = nullptr;
    }
    if (impl_->reconnect_timer != nullptr) {
        esp_timer_delete(impl_->reconnect_timer);
        impl_->reconnect_timer = nullptr;
    }
    if (impl_->nvs_ready) {
        nvs_close(impl_->nvs);
        impl_->nvs_ready = false;
    }
}

Result<Unit> NetworkManager::init() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->initialized) {
        return Result<Unit>::ok(Unit{});
    }
    if (impl_->provider) {
        impl_->settings = impl_->provider();
    }

    impl_->hostname = normalize_hostname(impl_->settings.network.hostname);
    impl_->ap_timer.set_default_window(impl_->settings.network.ap_window_s);
    impl_->open_nvs();
    impl_->load_or_create_ap_credentials_locked();
    impl_->restore_time_locked();
    impl_->apply_timezone_locked();

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return Result<Unit>::fail("netif_init", esp_err_to_name(err));
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return Result<Unit>::fail("event_loop", esp_err_to_name(err));
    }

    impl_->ap_netif = esp_netif_create_default_wifi_ap();
    if (impl_->ap_netif == nullptr) {
        return Result<Unit>::fail("ap_netif", "could not create AP netif");
    }
    esp_netif_set_hostname(impl_->ap_netif, impl_->hostname.c_str());

    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&wifi_config);
    if (err != ESP_OK) {
        return Result<Unit>::fail("wifi_init", esp_err_to_name(err));
    }
    esp_wifi_set_storage(WIFI_STORAGE_RAM);  // credentials live in our NVS namespace
    impl_->wifi_initialized = true;

    err = esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &Impl::wifi_event_handler, impl_.get(), &impl_->wifi_handler);
    if (err != ESP_OK) {
        return Result<Unit>::fail("wifi_handler", esp_err_to_name(err));
    }
    err = esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &Impl::ip_event_handler,
                                              impl_.get(), &impl_->ip_handler);
    if (err != ESP_OK) {
        return Result<Unit>::fail("ip_handler", esp_err_to_name(err));
    }

    esp_timer_create_args_t timer_args = {};
    timer_args.callback = &Impl::reconnect_timer_cb;
    timer_args.arg = impl_.get();
    timer_args.dispatch_method = ESP_TIMER_TASK;
    timer_args.name = "net_reconnect";
    err = esp_timer_create(&timer_args, &impl_->reconnect_timer);
    if (err != ESP_OK) {
        return Result<Unit>::fail("reconnect_timer", esp_err_to_name(err));
    }

    Impl::sntp_instance.store(impl_.get());

    if (xTaskCreate(&Impl::timer_task_main, "net_timer", kNetTaskStack, impl_.get(),
                    kNetTaskPriority, &impl_->timer_task) != pdPASS) {
        return Result<Unit>::fail("net_task", "could not create timer task");
    }
    if (xTaskCreate(&Impl::dns_task_main, "net_dns", kDnsTaskStack, impl_.get(), kNetTaskPriority,
                    &impl_->dns_task) != pdPASS) {
        return Result<Unit>::fail("dns_task", "could not create DNS task");
    }

    if (impl_->sta_configured_locked()) {
        auto result = impl_->apply_sta_locked();
        if (!result) {
            ESP_LOGW(kTag, "STA setup failed: %s",
                     result.error ? result.error->message.c_str() : "unknown");
        }
    } else if (impl_->settings.network.sta_enabled) {
        ESP_LOGW(kTag, "STA enabled but credentials are incomplete; staying offline");
    }

    impl_->initialized = true;
    ESP_LOGI(kTag, "network manager ready (hostname %s, AP %s)", impl_->hostname.c_str(),
             impl_->ap_ssid.c_str());
    return Result<Unit>::ok(Unit{});
}

NetworkStatus NetworkManager::status() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->build_status_locked();
}

Result<Unit> NetworkManager::start_ap() {
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (!impl_->initialized) {
            return Result<Unit>::fail("not_initialized", "network manager not initialized");
        }
        if (impl_->ap_active || impl_->ap_requested) {
            impl_->ap_timer.extend(uptime_now(), impl_->settings.network.ap_window_s);
        } else {
            if (impl_->ap_ssid.empty() || impl_->ap_password.size() < 8) {
                impl_->load_or_create_ap_credentials_locked();
            }
            auto configured = impl_->configure_ap_locked();
            if (!configured) {
                return configured;
            }
            impl_->ap_requested = true;
            impl_->ap_timer.start(uptime_now(), impl_->settings.network.ap_window_s);
            const wifi_mode_t mode = impl_->sta_ready_locked() ? WIFI_MODE_APSTA : WIFI_MODE_AP;
            esp_err_t err = esp_wifi_set_mode(mode);
            if (err != ESP_OK && err != ESP_ERR_WIFI_STATE) {
                impl_->ap_requested = false;
                impl_->ap_timer.stop();
                return Result<Unit>::fail("ap_mode", esp_err_to_name(err));
            }
            auto started = impl_->ensure_wifi_started_locked();
            if (!started) {
                impl_->ap_requested = false;
                impl_->ap_timer.stop();
                return started;
            }
            if (impl_->sta_ready_locked()) {
                esp_wifi_connect();
            }
        }
    }
    return Result<Unit>::ok(Unit{});
}

Result<Unit> NetworkManager::stop_ap() {
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (!impl_->initialized) {
            return Result<Unit>::fail("not_initialized", "network manager not initialized");
        }
        if (!impl_->ap_requested && !impl_->ap_active) {
            impl_->ap_timer.stop();
            return Result<Unit>::ok(Unit{});
        }
        impl_->ap_timer.stop();
        impl_->ap_requested = false;
        impl_->ap_active = false;
        impl_->stop_dns_locked();
        impl_->disable_napt_locked();
        if (impl_->ap_netif != nullptr) {
            esp_netif_dhcps_stop(impl_->ap_netif);
        }
        esp_err_t err = ESP_OK;
        if (impl_->wifi_started) {
            if (impl_->sta_ready_locked()) {
                err = esp_wifi_set_mode(WIFI_MODE_STA);
            } else {
                err = esp_wifi_set_mode(WIFI_MODE_NULL);
                if (err == ESP_OK || err == ESP_ERR_WIFI_STATE) {
                    err = esp_wifi_stop();
                    impl_->wifi_started = false;
                }
            }
        }
        if (!impl_->sta_connected) {
            impl_->stop_mdns_locked();
        }
        if (err != ESP_OK && err != ESP_ERR_WIFI_STATE) {
            return Result<Unit>::fail("ap_stop", esp_err_to_name(err));
        }
    }
    return Result<Unit>::ok(Unit{});
}

Result<Unit> NetworkManager::extend_ap(int seconds) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->initialized) {
        return Result<Unit>::fail("not_initialized", "network manager not initialized");
    }
    if (!impl_->ap_active && !impl_->ap_requested) {
        return Result<Unit>::fail("ap_inactive", "AP is not active");
    }
    if (seconds <= 0) {
        return Result<Unit>::fail("invalid_window", "extend window must be positive");
    }
    if (!impl_->ap_timer.extend(uptime_now(), seconds)) {
        return Result<Unit>::fail("ap_inactive", "AP is not active");
    }
    return Result<Unit>::ok(Unit{});
}

TimeInfo NetworkManager::time_info() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->build_time_info_locked();
}

Result<Unit> NetworkManager::sync_time(Timestamp epoch_seconds, const std::string& source) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->initialized) {
        return Result<Unit>::fail("not_initialized", "network manager not initialized");
    }
    return impl_->set_time_locked(epoch_seconds, source);
}

Result<Unit> NetworkManager::apply_settings(const NetworkSettings& settings) {
    bool restart_ap = false;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (!impl_->initialized) {
            return Result<Unit>::fail("not_initialized", "network manager not initialized");
        }
        if (!settings.ap_ssid.empty() && settings.ap_ssid.size() > 32) {
            return Result<Unit>::fail("invalid_ap_ssid", "AP SSID must be 1..32 bytes");
        }
        if (!settings.ap_password.empty() &&
            (settings.ap_password.size() < 8 || settings.ap_password.size() > 63)) {
            return Result<Unit>::fail("invalid_ap_password", "AP password must be 8..63 bytes");
        }
        if (settings.ap_window_s < 0) {
            return Result<Unit>::fail("invalid_window", "AP window must not be negative");
        }
        if (settings.sta_enabled) {
            if (settings.sta_ssid.empty()) {
                return Result<Unit>::fail("invalid_sta_ssid", "STA SSID is required");
            }
            if (settings.sta_password.size() < 8 || settings.sta_password.size() > 63) {
                return Result<Unit>::fail("invalid_sta_password",
                                          "STA password must be 8..63 bytes");
            }
        }

        bool credentials_changed = false;
        if (!settings.ap_ssid.empty() && settings.ap_ssid != impl_->ap_ssid) {
            impl_->ap_ssid = settings.ap_ssid;
            credentials_changed = true;
        }
        if (!settings.ap_password.empty() && settings.ap_password != impl_->ap_password) {
            impl_->ap_password = settings.ap_password;
            credentials_changed = true;
        }
        if (credentials_changed) {
            impl_->persist_ap_credentials_locked();
        }

        impl_->settings.network = settings;
        const std::string new_hostname = normalize_hostname(settings.hostname);
        const bool hostname_changed = new_hostname != impl_->hostname;
        impl_->hostname = new_hostname;
        if (impl_->ap_netif != nullptr) {
            esp_netif_set_hostname(impl_->ap_netif, impl_->hostname.c_str());
        }
        if (impl_->sta_netif != nullptr) {
            esp_netif_set_hostname(impl_->sta_netif, impl_->hostname.c_str());
        }
        impl_->ap_timer.set_default_window(settings.ap_window_s);

        restart_ap = credentials_changed && (impl_->ap_active || impl_->ap_requested);
        if (settings.sta_enabled) {
            auto result = impl_->apply_sta_locked();
            if (!result) {
                return result;
            }
        } else {
            impl_->disable_sta_locked();
        }

        if (!settings.napt_enabled) {
            impl_->disable_napt_locked();
        } else {
            impl_->enable_napt_locked();
        }

        if (hostname_changed && impl_->mdns_started) {
            impl_->stop_mdns_locked();
            impl_->ensure_mdns_locked();
        }
    }

    if (restart_ap) {
        auto result = stop_ap();
        if (!result) {
            return result;
        }
        result = start_ap();
        if (!result) {
            return result;
        }
    }
    return Result<Unit>::ok(Unit{});
}

Result<Unit> NetworkManager::apply_time_settings(const TimeSettings& settings) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->initialized) {
        return Result<Unit>::fail("not_initialized", "network manager not initialized");
    }
    const bool was_started = impl_->sntp_started;
    impl_->settings.time = settings;
    impl_->apply_timezone_locked();
    if (was_started) {
        impl_->stop_sntp_locked();
        impl_->start_sntp_locked();
    }
    return Result<Unit>::ok(Unit{});
}

uint64_t NetworkManager::uptime_seconds() const {
    return static_cast<uint64_t>(esp_timer_get_time()) / 1000000ULL;
}

void NetworkManager::set_listener(INetworkListener* listener) {
    bool initialized = false;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->listener = listener;
        initialized = impl_->initialized;
    }
    if (listener != nullptr && initialized) {
        impl_->emit_state();
    }
}

void NetworkManager::set_settings_provider(SettingsProvider provider) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->provider = std::move(provider);
}

std::string NetworkManager::ap_ssid() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->ap_ssid;
}

std::string NetworkManager::ap_password() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return (impl_->ap_active || impl_->ap_requested) ? impl_->ap_password : std::string{};
}

std::string NetworkManager::ap_url() const {
    return http_url(kCaptivePortalIp);
}

std::string NetworkManager::sta_url() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->sta_connected || impl_->sta_ip.empty()) {
        return {};
    }
    return http_url(impl_->sta_ip);
}

std::string NetworkManager::wifi_qr_payload() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->ap_active && !impl_->ap_requested) {
        return {};
    }
    return kaffee::wifi_qr_payload(impl_->ap_ssid, impl_->ap_password);
}

bool NetworkManager::ap_active() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->ap_active;
}

}  // namespace kaffee
