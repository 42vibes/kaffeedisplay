#include "png_writer.hpp"

#include <cstdio>
#include <cstring>

#include <zlib.h>

namespace sim {
namespace {

void put_u32_be(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

bool write_chunk(FILE* file, const char type[4], const std::vector<uint8_t>& data) {
    std::vector<uint8_t> header;
    put_u32_be(header, static_cast<uint32_t>(data.size()));
    std::vector<uint8_t> crc_data;
    crc_data.reserve(4 + data.size());
    crc_data.insert(crc_data.end(), type, type + 4);
    crc_data.insert(crc_data.end(), data.begin(), data.end());
    uLong crc = crc32(0L, Z_NULL, 0);
    crc = crc32(crc, crc_data.data(), static_cast<uInt>(crc_data.size()));
    std::vector<uint8_t> footer;
    put_u32_be(footer, static_cast<uint32_t>(crc));

    if (std::fwrite(header.data(), 1, header.size(), file) != header.size()) return false;
    if (std::fwrite(crc_data.data(), 1, crc_data.size(), file) != crc_data.size()) {
        return false;
    }
    if (std::fwrite(footer.data(), 1, footer.size(), file) != footer.size()) return false;
    return true;
}

}  // namespace

bool write_png_rgb(const std::string& path, int width, int height,
                   const std::vector<uint8_t>& rgb) {
    if (width <= 0 || height <= 0) return false;
    const size_t expected = static_cast<size_t>(width) * static_cast<size_t>(height) * 3;
    if (rgb.size() != expected) return false;

    std::vector<uint8_t> raw;
    raw.reserve(static_cast<size_t>(height) * (static_cast<size_t>(width) * 3 + 1));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);  // filter: none
        const uint8_t* row = rgb.data() + static_cast<size_t>(y) * width * 3;
        raw.insert(raw.end(), row, row + static_cast<size_t>(width) * 3);
    }

    uLongf compressed_size = compressBound(static_cast<uLong>(raw.size()));
    std::vector<uint8_t> compressed(compressed_size);
    int level = Z_BEST_COMPRESSION;
    if (compress2(compressed.data(), &compressed_size, raw.data(), static_cast<uLong>(raw.size()),
                  level) != Z_OK) {
        return false;
    }
    compressed.resize(compressed_size);

    FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) return false;

    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    bool ok = std::fwrite(signature, 1, 8, file) == 8;

    if (ok) {
        std::vector<uint8_t> ihdr;
        put_u32_be(ihdr, static_cast<uint32_t>(width));
        put_u32_be(ihdr, static_cast<uint32_t>(height));
        ihdr.push_back(8);  // bit depth
        ihdr.push_back(2);  // colour type: RGB
        ihdr.push_back(0);  // compression
        ihdr.push_back(0);  // filter
        ihdr.push_back(0);  // interlace
        ok = write_chunk(file, "IHDR", ihdr);
    }
    if (ok) ok = write_chunk(file, "IDAT", compressed);
    if (ok) ok = write_chunk(file, "IEND", {});

    if (std::fclose(file) != 0) ok = false;
    return ok;
}

}  // namespace sim
