#include <doctest.h>

#include "storage_boot.hpp"

using kaffee::boot::storage_action;
using kaffee::boot::StorageAction;

TEST_CASE("LittleFS recovery formats only a genuinely blank partition") {
    CHECK(storage_action(true, false) == StorageAction::UseMounted);
    CHECK(storage_action(true, true) == StorageAction::UseMounted);
    CHECK(storage_action(false, true) == StorageAction::FormatBlankThenMount);
    CHECK(storage_action(false, false) == StorageAction::Fail);
}
