#include "kaffee/file_store.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

#include "kaffee/json.hpp"

namespace kaffee {
namespace {

Result<Unit> fs_fail(const std::string& what, const std::error_code& ec) {
    return Result<Unit>::fail("storage", what + ": " + ec.message());
}

bool write_and_sync(const std::string& path, const std::string& contents) {
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) return false;
    const bool written = std::fwrite(contents.data(), 1, contents.size(), file) == contents.size();
    bool ok = written;
    if (std::fflush(file) != 0) ok = false;
    const int fd = fileno(file);
    if (fd < 0 || ::fsync(fd) != 0) ok = false;
    std::fclose(file);
    return ok;
}

bool fsync_directory(const std::string& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd < 0) return false;
    const bool ok = ::fsync(fd) == 0;
    ::close(fd);
    return ok;
}

}  // namespace

FileEventStore::FileEventStore(std::string directory) : JournalEventStore(std::move(directory)) {}

Result<Unit> FileEventStore::ensure_root() {
    std::error_code ec;
    std::filesystem::create_directories(root(), ec);
    if (ec) return fs_fail("create directory " + root(), ec);
    return Result<Unit>::ok(Unit{});
}

Result<Unit> FileEventStore::remove_file(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
    if (ec) return fs_fail("remove " + path, ec);
    return Result<Unit>::ok(Unit{});
}

Result<Unit> FileEventStore::rename_file(const std::string& from, const std::string& to) {
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    if (ec) return fs_fail("rename " + from + " -> " + to, ec);
    return Result<Unit>::ok(Unit{});
}

Result<Unit> FileEventStore::sync_dir() {
    // Best effort: not every host filesystem supports directory fsync.
    fsync_directory(root());
    return Result<Unit>::ok(Unit{});
}

uint64_t FileEventStore::total_bytes() const {
    std::error_code ec;
    const auto space = std::filesystem::space(root(), ec);
    if (ec) return 0;
    return static_cast<uint64_t>(space.capacity);
}

JsonFileSettingsStore::JsonFileSettingsStore(std::string path) : path_(std::move(path)) {}

Result<Settings> JsonFileSettingsStore::load() {
    std::ifstream in(path_, std::ios::binary);
    if (!in) {
        if (!std::filesystem::exists(path_)) return Result<Settings>::ok(Settings{});
        return Result<Settings>::fail("settings", "cannot read settings file");
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    try {
        return Result<Settings>::ok(settings_from_json(nlohmann::json::parse(text)));
    } catch (const std::exception& e) {
        return Result<Settings>::fail("settings", e.what());
    }
}

Result<Unit> JsonFileSettingsStore::save(const Settings& settings) {
    const std::string tmp = path_ + ".tmp";
    const std::string contents = settings_to_json(settings, true).dump(2);
    if (!write_and_sync(tmp, contents)) {
        std::remove(tmp.c_str());
        return Result<Unit>::fail("settings", "cannot write settings file");
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path_, ec);
    if (ec) {
        std::remove(tmp.c_str());
        return fs_fail("rename settings file", ec);
    }
    const auto parent = std::filesystem::path(path_).parent_path();
    if (!parent.empty()) fsync_directory(parent.string());
    return Result<Unit>::ok(Unit{});
}

Result<Unit> JsonFileSettingsStore::reset() {
    std::error_code ec;
    std::filesystem::remove(path_, ec);
    if (ec) return fs_fail("remove settings file", ec);
    std::filesystem::remove(path_ + ".setup-password", ec);
    if (ec) return fs_fail("remove setup password file", ec);
    return Result<Unit>::ok(Unit{});
}

Result<std::string> JsonFileSettingsStore::load_setup_password() {
    const std::string setup_path = path_ + ".setup-password";
    std::ifstream in(setup_path, std::ios::binary);
    if (!in) {
        if (!std::filesystem::exists(setup_path)) return Result<std::string>::ok(std::string());
        return Result<std::string>::fail("settings", "cannot read setup password file");
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    return Result<std::string>::ok(std::move(text));
}

Result<Unit> JsonFileSettingsStore::save_setup_password(const std::string& password) {
    if (password.empty()) return clear_setup_password();
    const std::string setup_path = path_ + ".setup-password";
    const std::string tmp = setup_path + ".tmp";
    if (!write_and_sync(tmp, password)) {
        std::remove(tmp.c_str());
        return Result<Unit>::fail("settings", "cannot write setup password file");
    }
    std::error_code ec;
    std::filesystem::rename(tmp, setup_path, ec);
    if (ec) {
        std::remove(tmp.c_str());
        return fs_fail("rename setup password file", ec);
    }
    const auto parent = std::filesystem::path(path_).parent_path();
    if (!parent.empty()) fsync_directory(parent.string());
    return Result<Unit>::ok(Unit{});
}

Result<Unit> JsonFileSettingsStore::clear_setup_password() {
    std::error_code ec;
    std::filesystem::remove(path_ + ".setup-password", ec);
    if (ec) return fs_fail("remove setup password file", ec);
    return Result<Unit>::ok(Unit{});
}

MemorySettingsStore::MemorySettingsStore(Settings settings) : settings_(std::move(settings)) {}

Result<Settings> MemorySettingsStore::load() {
    return Result<Settings>::ok(settings_);
}

Result<Unit> MemorySettingsStore::save(const Settings& settings) {
    settings_ = settings;
    return Result<Unit>::ok(Unit{});
}

Result<Unit> MemorySettingsStore::reset() {
    settings_ = Settings{};
    setup_password_.clear();
    return Result<Unit>::ok(Unit{});
}

Result<std::string> MemorySettingsStore::load_setup_password() {
    return Result<std::string>::ok(setup_password_);
}

Result<Unit> MemorySettingsStore::save_setup_password(const std::string& password) {
    setup_password_ = password;
    return Result<Unit>::ok(Unit{});
}

Result<Unit> MemorySettingsStore::clear_setup_password() {
    setup_password_.clear();
    return Result<Unit>::ok(Unit{});
}

}  // namespace kaffee
