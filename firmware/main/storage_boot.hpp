#pragma once

namespace kaffee::boot {

// Policy for the LittleFS data partition. A failed mount must never silently
// format a partition that may contain the ledger; only a genuinely blank/new
// partition is formatted once. The decision is pure so host tests can cover the
// truth table without ESP-IDF.
enum class StorageAction {
    UseMounted,
    FormatBlankThenMount,
    Fail,
};

constexpr StorageAction storage_action(bool mounted, bool blank) {
    if (mounted) return StorageAction::UseMounted;
    if (blank) return StorageAction::FormatBlankThenMount;
    return StorageAction::Fail;
}

}  // namespace kaffee::boot
