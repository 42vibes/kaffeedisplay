#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace kaffee {

std::array<uint8_t, 32> sha256(std::string_view data);
std::string sha256_hex(std::string_view data);

}  // namespace kaffee
