#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "api_internal.hpp"
#include "esp_log.h"
#include "esp_ota_ops.h"

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

// --- Incremental SHA-256 -------------------------------------------------
// TODO(T-001): expose a streaming variant from kaffee/sha256.hpp and delete
// this local copy. Kept here so OTA does not need the whole image in RAM.
class Sha256Stream {
  public:
    void update(const uint8_t* data, size_t size) {
        total_ += size;
        while (size > 0) {
            size_t take = std::min(size, sizeof(buffer_) - buffer_len_);
            std::memcpy(buffer_ + buffer_len_, data, take);
            buffer_len_ += take;
            data += take;
            size -= take;
            if (buffer_len_ == sizeof(buffer_)) {
                compress(buffer_);
                buffer_len_ = 0;
            }
        }
    }

    std::string hex() {
        const uint64_t bits = total_ * 8;
        uint8_t pad = 0x80;
        update_byte(pad);
        while (buffer_len_ != 56) update_byte(0x00);
        for (int i = 7; i >= 0; --i) {
            uint8_t byte = static_cast<uint8_t>(bits >> (8 * i));
            buffer_[buffer_len_++] = byte;
        }
        compress(buffer_);
        buffer_len_ = 0;

        static constexpr char kHex[] = "0123456789abcdef";
        std::string out;
        out.reserve(64);
        for (uint32_t word : state_) {
            for (int shift = 28; shift >= 0; shift -= 4) {
                out.push_back(kHex[(word >> shift) & 0x0f]);
            }
        }
        return out;
    }

  private:
    static constexpr uint32_t kRound[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2};

    static uint32_t rotr(uint32_t value, uint32_t count) {
        return (value >> count) | (value << (32 - count));
    }

    void update_byte(uint8_t byte) {
        if (buffer_len_ == sizeof(buffer_)) {
            compress(buffer_);
            buffer_len_ = 0;
        }
        buffer_[buffer_len_++] = byte;
    }

    void compress(const uint8_t* block) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
                   (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
                   static_cast<uint32_t>(block[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t temp1 = h + s1 + ch + kRound[i] + w[i];
            uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = s0 + maj;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    uint32_t state_[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t buffer_[64] = {};
    size_t buffer_len_ = 0;
    uint64_t total_ = 0;
};

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
