#include "kaffee/api_multipart.hpp"

#include <cctype>
#include <vector>

namespace kaffee {
namespace api {
namespace {

std::string trim_copy(std::string_view value) {
    size_t begin = 0;
    size_t end = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])) != 0) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) --end;
    return std::string(value.substr(begin, end - begin));
}

std::string lower_copy(std::string_view value) {
    std::string out(value);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

struct Parameter {
    std::string name;
    std::string value;
};

// Splits `key=value; key2="value2"` respecting quoted strings.
std::vector<Parameter> parse_parameters(std::string_view header) {
    std::vector<Parameter> params;
    size_t pos = 0;
    bool quoted = false;
    std::string token;
    auto flush = [&]() {
        std::string trimmed = trim_copy(token);
        token.clear();
        if (trimmed.empty()) return;
        size_t eq = trimmed.find('=');
        if (eq == std::string::npos) return;
        Parameter param;
        param.name = lower_copy(trim_copy(std::string_view(trimmed).substr(0, eq)));
        param.value = trim_copy(std::string_view(trimmed).substr(eq + 1));
        if (param.value.size() >= 2 && param.value.front() == '"' && param.value.back() == '"') {
            param.value = param.value.substr(1, param.value.size() - 2);
        }
        params.push_back(std::move(param));
    };
    while (pos < header.size()) {
        char c = header[pos++];
        if (c == '"') quoted = !quoted;
        if (c == ';' && !quoted) {
            flush();
            continue;
        }
        token.push_back(c);
    }
    flush();
    return params;
}

}  // namespace

std::optional<std::string> multipart_parameter(std::string_view header, std::string_view name) {
    const std::string wanted = lower_copy(name);
    for (const Parameter& param : parse_parameters(header)) {
        if (param.name == wanted) return param.value;
    }
    return std::nullopt;
}

std::string parse_multipart_boundary(std::string_view content_type) {
    size_t semi = content_type.find(';');
    const std::string media_type = lower_copy(
        trim_copy(semi == std::string_view::npos ? content_type : content_type.substr(0, semi)));
    if (media_type != "multipart/form-data") return {};

    auto boundary = multipart_parameter(content_type, "boundary");
    if (!boundary || boundary->empty() || boundary->size() > 128) return {};
    if (boundary->find('\r') != std::string::npos || boundary->find('\n') != std::string::npos) {
        return {};
    }
    return *boundary;
}

bool multipart_is_firmware_part(std::string_view content_disposition) {
    auto name = multipart_parameter(content_disposition, "name");
    return name && *name == "firmware";
}

MultipartScanner::MultipartScanner(std::string_view boundary)
    : delimiter_("\r\n--" + std::string(boundary)) {}

bool MultipartScanner::feed(std::string_view chunk, std::string& out) {
    if (complete_) return true;
    pending_.append(chunk.data(), chunk.size());

    const size_t index = pending_.find(delimiter_);
    if (index != std::string::npos) {
        out.append(pending_.data(), index);
        const size_t tail_start = index + delimiter_.size();
        tail_.assign(pending_.data() + tail_start, pending_.size() - tail_start);
        if (tail_.size() > 8) tail_.resize(8);
        pending_.clear();
        complete_ = true;
        return true;
    }

    // Keep a delimiter-sized tail so a boundary split across chunks is found.
    const size_t keep = delimiter_.size() - 1;
    if (pending_.size() > keep) {
        const size_t writable = pending_.size() - keep;
        out.append(pending_.data(), writable);
        pending_.erase(0, writable);
    }
    return false;
}

}  // namespace api
}  // namespace kaffee
