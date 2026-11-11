#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sim {

bool write_png_rgb(const std::string& path, int width, int height, const std::vector<uint8_t>& rgb);

}  // namespace sim
