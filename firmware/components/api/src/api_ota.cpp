#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "api_internal.hpp"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "sha256_stream.hpp"

// Multipart OTA upload.
//
// The image is streamed straight into the inactive OTA slot; nothing is
// buffered in RAM except one socket chunk and the multipart tail window. The
// SHA-256 is computed while writing and returned in the response. If the client
// sends `X-Firmware-SHA256` the digest is verified before the boot partition is
// switched.

namespace kaffee {
namespace api {
namespace {

constexpr const char* kTag = "kaffee.api";
constexpr size_t kReadBuffer = 2048;
constexpr size_t kMaxHeaderLine = 512;
constexpr size_t kMaxHeaderBytes = 4096;
constexpr size_t kMaxPreambleLines = 32;

// --- Buffered socket reader ---------------------------------------------

class MultipartReader {
  public:
    explicit MultipartReader(httpd_req_t* req) : req_(req) {}

    int read(char* out, size_t max) {
        if (pos_ == len_) {
            if (!fill()) return 0;
        }
        size_t take = std::min(max, len_ - pos_);
        std::memcpy(out, buffer_ + pos_, take);
        pos_ += take;
        return static_cast<int>(take);
    }

    bool read_line(std::string& line, size_t max_len) {
        line.clear();
        while (line.size() <= max_len) {
            char c = 0;
            int got = read(&c, 1);
            if (got <= 0) return false;
            if (c == '\n') {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                return true;
            }
            line.push_back(c);
        }
        return false;
    }

  private:
    bool fill() {
        for (int attempt = 0; attempt < 4; ++attempt) {
            int received = httpd_req_recv(req_, buffer_, sizeof(buffer_));
            if (received == HTTPD_SOCK_ERR_TIMEOUT) continue;
            if (received < 0) return false;
            pos_ = 0;
            len_ = static_cast<size_t>(received);
            return len_ > 0;
        }
        return false;
    }

    httpd_req_t* req_;
    char buffer_[kReadBuffer] = {};
    size_t pos_ = 0;
    size_t len_ = 0;
};

std::string parse_boundary(const std::string& content_type) {
    const std::string lower = lower_ascii(content_type);
    size_t pos = lower.find("boundary=");
    if (pos == std::string::npos) return {};
    pos += std::strlen("boundary=");
    std::string value = content_type.substr(pos);
    size_t semi = value.find(';');
    if (semi != std::string::npos) value.resize(semi);
    value = trim(value);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.size() - 2);
    }
    if (value.empty() || value.size() > 128) return {};
    return value;
}

bool header_names_field(const std::string& header) {
    const std::string lower = lower_ascii(header);
    return lower.find("name=\"firmware\"") != std::string::npos ||
           lower.find("name=firmware") != std::string::npos;
}

}  // namespace

esp_err_t handle_ota(RequestContext& ctx) {
    auto content_type = req_header(ctx.req, "Content-Type");
    if (!content_type || lower_ascii(*content_type).rfind("multipart/form-data", 0) != 0) {
        return send_error(ctx.req, 415, "unsupported_media_type", "multipart/form-data erwartet");
    }
    const std::string boundary = parse_boundary(*content_type);
    if (boundary.empty()) {
        return send_error(ctx.req, 400, "invalid_input", "Multipart-Boundary fehlt");
    }
    if (ctx.req->content_len == 0) {
        return send_error(ctx.req, 400, "invalid_input", "Firmware-Datei fehlt");
    }
    if (ctx.req->content_len > kMaxOtaBytes + 16384) {
        return send_error(ctx.req, 413, "payload_too_large", "Firmware ist zu groß");
    }

    std::string expected_sha;
    if (auto header = req_header(ctx.req, "X-Firmware-SHA256")) {
        expected_sha = lower_ascii(trim(*header));
    }

    MultipartReader reader(ctx.req);
    const std::string first = "--" + boundary;
    std::string line;
    bool found = false;
    size_t preamble = 0;
    for (size_t i = 0; i < kMaxPreambleLines && preamble < 8192; ++i) {
        if (!reader.read_line(line, kMaxHeaderLine)) break;
        preamble += line.size() + 2;
        if (line == first) {
            found = true;
            break;
        }
        if (line == first + "--") break;
    }
    if (!found) {
        return send_error(ctx.req, 400, "invalid_input", "Multipart-Kopf fehlt");
    }

    bool firmware_part = false;
    size_t header_bytes = 0;
    while (true) {
        if (!reader.read_line(line, kMaxHeaderLine)) {
            return send_error(ctx.req, 400, "invalid_input", "Multipart-Kopf unvollständig");
        }
        header_bytes += line.size() + 2;
        if (header_bytes > kMaxHeaderBytes) {
            return send_error(ctx.req, 400, "invalid_input", "Multipart-Kopf ist zu groß");
        }
        if (line.empty()) break;
        if (header_names_field(line)) firmware_part = true;
    }
    if (!firmware_part) {
        return send_error(ctx.req, 400, "invalid_input", "Feld 'firmware' fehlt");
    }

    const esp_partition_t* update = esp_ota_get_next_update_partition(nullptr);
    if (update == nullptr) {
        return send_error(ctx.req, 500, "unavailable", "Keine OTA-Partition verfügbar");
    }

    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(update, OTA_WITH_SEQUENTIAL_WRITES, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_ota_begin failed: %s", esp_err_to_name(err));
        return send_error(ctx.req, 500, "unavailable", "OTA konnte nicht gestartet werden");
    }

    Sha256Stream sha;
    size_t total = 0;
    std::string carry;
    const std::string delimiter = "\r\n" + first;
    bool complete = false;
    bool failed = false;
    int failure_status = 500;
    std::string failure_code = "internal_error";
    std::string failure_message = "Firmware konnte nicht geschrieben werden";

    auto write_bytes = [&](const char* data, size_t size) {
        if (size == 0) return;
        total += size;
        if (total > kMaxOtaBytes) {
            failed = true;
            failure_status = 413;
            failure_code = "payload_too_large";
            failure_message = "Firmware ist zu groß";
            return;
        }
        sha.update(reinterpret_cast<const uint8_t*>(data), size);
        esp_err_t write_err = esp_ota_write(handle, data, size);
        if (write_err != ESP_OK) {
            ESP_LOGE(kTag, "esp_ota_write failed: %s", esp_err_to_name(write_err));
            failed = true;
            failure_status = 400;
            failure_code = "invalid_image";
            failure_message = "Firmware-Abbild ist ungültig";
        }
    };

    while (!complete && !failed) {
        char chunk[kReadBuffer];
        int received = reader.read(chunk, sizeof(chunk));
        if (received <= 0) {
            failed = true;
            failure_status = 400;
            failure_code = "invalid_input";
            failure_message = "Upload wurde abgebrochen";
            break;
        }
        carry.append(chunk, static_cast<size_t>(received));
        size_t index = carry.find(delimiter);
        if (index != std::string::npos) {
            write_bytes(carry.data(), index);
            complete = true;
            break;
        }
        const size_t keep = delimiter.size() - 1;
        if (carry.size() > keep) {
            size_t writable = carry.size() - keep;
            write_bytes(carry.data(), writable);
            carry.erase(0, writable);
        }
    }

    if (failed) {
        esp_ota_abort(handle);
        return send_error(ctx.req, failure_status, failure_code, failure_message);
    }
    if (total == 0) {
        esp_ota_abort(handle);
        return send_error(ctx.req, 400, "invalid_input", "Firmware-Datei ist leer");
    }

    err = esp_ota_end(handle);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_ota_end failed: %s", esp_err_to_name(err));
        return send_error(ctx.req, 400, "invalid_image", "Firmware-Abbild ist ungültig");
    }

    const std::string digest = sha.hex();
    if (!expected_sha.empty() && expected_sha != digest) {
        ESP_LOGE(kTag, "OTA SHA-256 mismatch");
        // The image is already written; do not switch the boot partition.
        return send_error(ctx.req, 400, "checksum_mismatch", "SHA-256 stimmt nicht überein");
    }

    err = esp_ota_set_boot_partition(update);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        return send_error(ctx.req, 500, "internal_error",
                          "Boot-Partition konnte nicht gesetzt werden");
    }

    ESP_LOGI(kTag, "OTA image written to %s (%u bytes, sha256 %s), rebooting", update->label,
             static_cast<unsigned>(total), digest.c_str());
    nlohmann::json body{
        {"status", "ok"}, {"sha256", digest}, {"bytes", total}, {"partition", update->label}};
    esp_err_t sent = send_json(ctx.req, 200, body);
    ctx.server.schedule_reboot(800);
    return sent;
}

}  // namespace api
}  // namespace kaffee
