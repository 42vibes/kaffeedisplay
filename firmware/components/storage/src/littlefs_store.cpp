#include "kaffee/littlefs_store.hpp"

#ifdef ESP_PLATFORM

#include <sys/stat.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <utility>

#include <esp_littlefs.h>
#include <esp_log.h>
#include <fcntl.h>
#include <unistd.h>

namespace kaffee {
namespace {

constexpr const char* kTag = "kaffee-storage";

Result<Unit> fs_fail(const std::string& what) {
    return Result<Unit>::fail("storage", what + ": " + std::string(std::strerror(errno)));
}

}  // namespace

LittleFsEventStore::LittleFsEventStore(std::string mount_point)
    : JournalEventStore(std::move(mount_point)) {}

Result<Unit> LittleFsEventStore::mount() {
    auto result = JournalEventStore::mount();
    if (result) {
        ESP_LOGI(kTag, "journal mounted at %s", root().c_str());
    } else {
        ESP_LOGE(kTag, "journal mount failed: %s", result.error->message.c_str());
    }
    return result;
}

Result<Unit> LittleFsEventStore::factory_reset() {
    ESP_LOGW(kTag, "factory reset of %s", root().c_str());
    return JournalEventStore::factory_reset();
}

Result<Unit> LittleFsEventStore::ensure_root() {
    if (::mkdir(root().c_str(), 0755) != 0 && errno != EEXIST) {
        return fs_fail("mkdir " + root());
    }
    struct stat st = {};
    if (::stat(root().c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
        return Result<Unit>::fail("storage", "mount point is not a directory: " + root());
    }
    return Result<Unit>::ok(Unit{});
}

Result<Unit> LittleFsEventStore::remove_file(const std::string& path) {
    if (::remove(path.c_str()) != 0 && errno != ENOENT) return fs_fail("remove " + path);
    return Result<Unit>::ok(Unit{});
}

Result<Unit> LittleFsEventStore::rename_file(const std::string& from, const std::string& to) {
    if (::rename(from.c_str(), to.c_str()) != 0) return fs_fail("rename " + from + " -> " + to);
    return Result<Unit>::ok(Unit{});
}

Result<Unit> LittleFsEventStore::sync_dir() {
    const int fd = ::open(root().c_str(), O_RDONLY);
    if (fd < 0) return Result<Unit>::ok(Unit{});  // LittleFS flushes per write.
    ::fsync(fd);
    ::close(fd);
    return Result<Unit>::ok(Unit{});
}

uint64_t LittleFsEventStore::total_bytes() const {
    size_t total = 0;
    size_t used = 0;
    // The project partition table names the LittleFS data partition "storage";
    // do not rely on CONFIG_LITTLEFS_PARTITION_LABEL.
    if (esp_littlefs_info("storage", &total, &used) != ESP_OK) return 0;
    (void)used;
    return static_cast<uint64_t>(total);
}

}  // namespace kaffee

#endif  // ESP_PLATFORM
