#pragma once

// Pure multipart helpers for the OTA upload. No ESP-IDF dependency so boundary
// parsing and the streaming scanner are unit-tested on the host.

#include <optional>
#include <string>
#include <string_view>

namespace kaffee {
namespace api {

// Extracts a parameter from a header value such as
//   form-data; name="firmware"; filename="fw.bin"
// Parameter names are matched case-insensitively; quotes are stripped.
std::optional<std::string> multipart_parameter(std::string_view header, std::string_view name);

// Returns the boundary token from a multipart/form-data Content-Type, or an
// empty string when the header is malformed/missing.
std::string parse_multipart_boundary(std::string_view content_type);

// True only when the part is named exactly "firmware" (a filename alone is not
// enough).
bool multipart_is_firmware_part(std::string_view content_disposition);

// Streaming scanner for the body of a single multipart part.
//
// The caller consumes the opening `--boundary` line itself, then feeds all
// following bytes. Bytes that are definitely payload are appended to `out`;
// the last `delimiter().size() - 1` bytes stay buffered so a delimiter split
// across socket chunks is still detected. `tail()` exposes the bytes that
// followed the closing delimiter (start of `--` for a well-formed close).
class MultipartScanner {
  public:
    explicit MultipartScanner(std::string_view boundary);

    // Returns true once the delimiter has been seen. Never appends delimiter
    // bytes to `out`.
    bool feed(std::string_view chunk, std::string& out);

    bool complete() const {
        return complete_;
    }
    const std::string& delimiter() const {
        return delimiter_;
    }
    std::string_view tail() const {
        return tail_;
    }

  private:
    std::string delimiter_;
    std::string pending_;
    std::string tail_;
    bool complete_ = false;
};

}  // namespace api
}  // namespace kaffee
