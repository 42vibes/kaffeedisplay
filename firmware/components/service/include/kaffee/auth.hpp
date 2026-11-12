#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace kaffee {

// PBKDF2-HMAC-SHA256. `salt_hex` is the hex encoded salt (possibly empty).
// Returns lowercase hex. Iterations below 1 are treated as 1.
std::string pbkdf2_sha256_hex(std::string_view password, std::string_view salt_hex,
                              uint32_t iterations, size_t dk_len = 32);

// Length-aware, value-independent comparison for secrets.
bool constant_time_equal(std::string_view a, std::string_view b);

std::string hex_encode(const unsigned char* data, size_t length);

}  // namespace kaffee
