#include <doctest.h>

#include "format.hpp"

using namespace kaffee;

TEST_CASE("money formatting is German and uses cents") {
    CHECK(ui::format_money(0) == "0,00 €");
    CHECK(ui::format_money(50) == "0,50 €");
    CHECK(ui::format_money(350) == "3,50 €");
    CHECK(ui::format_money(1234567) == "12345,67 €");
    CHECK(ui::format_money(-1250) == "-12,50 €");
    CHECK(ui::format_money(50, false) == "0,50");
}

TEST_CASE("balance labels credit and debt") {
    CHECK(ui::format_balance(350) == "Guthaben 3,50 €");
    CHECK(ui::format_balance(-1250) == "Offen 12,50 €");
}

TEST_CASE("initials handle one and two word names") {
    CHECK(ui::initials("Anna") == "A");
    CHECK(ui::initials("Anna Maria") == "AM");
    CHECK(ui::initials("Jürgen") == "J");
}

TEST_CASE("durations and byte sizes are compact") {
    CHECK(ui::format_uptime(274860) == "3 T 4 Std.");
    CHECK(ui::format_uptime(3660) == "1 Std. 1 Min.");
    CHECK(ui::format_uptime(120) == "2 Min.");
    CHECK(ui::format_bytes(148 * 1024) == "148 kB");
    CHECK(ui::format_bytes(7 * 1024 * 1024) == "7,0 MB");
}

TEST_CASE("month navigation produces stable bounds") {
    const Timestamp now = 1'794'386'460;  // 2026-11-11 09:41 Europe/Berlin
    CHECK(ui::format_month(now, 0) == "November 2026");
    CHECK(ui::format_month(now, -1) == "Oktober 2026");

    Timestamp from = 0;
    Timestamp to = 0;
    ui::month_bounds(now, 0, &from, &to);
    CHECK(from > 0);
    CHECK(to > from);
    CHECK(to - from >= 28 * 86400);
    CHECK(to - from <= 31 * 86400);
}
