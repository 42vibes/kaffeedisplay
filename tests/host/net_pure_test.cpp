#include <doctest.h>

#include <string>

#include "kaffee/net_pure.hpp"

using namespace kaffee;

TEST_CASE("ApTimer counts down and expires exactly once") {
    ApTimer timer;
    CHECK(timer.default_window() == kApDefaultWindowSeconds);
    CHECK_FALSE(timer.active());
    CHECK(timer.remaining(100) == 0);
    CHECK_FALSE(timer.tick(100));

    timer.start(100);
    CHECK(timer.active());
    CHECK(timer.deadline() == 100 + kApDefaultWindowSeconds);
    CHECK(timer.remaining(100) == kApDefaultWindowSeconds);
    CHECK(timer.remaining(999) == kApDefaultWindowSeconds - 899);
    CHECK_FALSE(timer.tick(timer.deadline() - 1));
    CHECK(timer.tick(timer.deadline()));
    CHECK_FALSE(timer.active());
    CHECK(timer.remaining(timer.deadline() + 10) == 0);
    CHECK_FALSE(timer.tick(timer.deadline() + 11));
}

TEST_CASE("ApTimer extend respects the maximum window") {
    ApTimer timer;
    timer.start(0, 60);
    CHECK(timer.extend(10, 60));
    CHECK(timer.deadline() == 120);
    CHECK(timer.remaining(10) == 110);
    CHECK_FALSE(timer.extend(20, 0));
    CHECK_FALSE(timer.extend(20, -5));

    timer.start(0, kApMaxWindowSeconds);
    CHECK(timer.deadline() == kApMaxWindowSeconds);
    CHECK(timer.extend(0, kApMaxWindowSeconds));
    CHECK(timer.deadline() == kApMaxWindowSeconds);  // capped

    timer.stop();
    CHECK_FALSE(timer.extend(0, 60));

    timer.start(0, 10);
    CHECK(timer.extend(100, 60));
    CHECK(timer.deadline() == 160);  // extends from now if the window already lapsed
}

TEST_CASE("ApTimer supports a custom default window") {
    ApTimer timer(30);
    CHECK(timer.default_window() == 30);
    timer.start(5);
    CHECK(timer.deadline() == 35);
    timer.set_default_window(0);
    CHECK(timer.default_window() == kApDefaultWindowSeconds);
}

TEST_CASE("Wi-Fi QR payload is stable and escaped") {
    CHECK(wifi_qr_payload("Kaffeekasse-B834", "ABCD2345EFGH") ==
          "WIFI:T:WPA;S:Kaffeekasse-B834;P:ABCD2345EFGH;;");
    CHECK(wifi_qr_payload("My;Wi,Fi:1", "pa\\ss") == "WIFI:T:WPA;S:My\\;Wi\\,Fi\\:1;P:pa\\\\ss;;");
}

TEST_CASE("HTTP URL helper omits the default port") {
    CHECK(http_url("192.168.4.1") == "http://192.168.4.1");
    CHECK(http_url("192.168.4.1", 80) == "http://192.168.4.1");
    CHECK(http_url("192.168.4.1", 8080) == "http://192.168.4.1:8080");
    CHECK(http_url("") == "");
}

TEST_CASE("Captive probe paths are recognised") {
    CHECK(is_captive_probe_path("/generate_204"));
    CHECK(is_captive_probe_path("/gen_204"));
    CHECK(is_captive_probe_path("/hotspot-detect.html"));
    CHECK(is_captive_probe_path("/hotspot-detect.html?redirect=x"));
    CHECK(is_captive_probe_path("/connecttest.txt"));
    CHECK(is_captive_probe_path("/ncsi.txt"));
    CHECK(is_captive_probe_path("/library/test/success.html"));
    CHECK_FALSE(is_captive_probe_path("/"));
    CHECK_FALSE(is_captive_probe_path("/api/status"));
    CHECK_FALSE(is_captive_probe_path("/generate_2040"));
    CHECK_FALSE(is_captive_probe_path(""));
}

TEST_CASE("Time sanity enforces absolute and future bounds") {
    CHECK(is_time_sane(kTimeMinEpochSeconds));
    CHECK(is_time_sane(kTimeMinEpochSeconds - 1) == false);
    CHECK(is_time_sane(kTimeMaxEpochSeconds - 1));
    CHECK(is_time_sane(kTimeMaxEpochSeconds) == false);
    CHECK(is_time_sane(0) == false);

    const int64_t reference = kTimeMinEpochSeconds;
    CHECK(is_time_sane(reference + 100, reference));
    CHECK(is_time_sane(reference + kMaxFutureSkewSeconds, reference));
    CHECK(is_time_sane(reference + kMaxFutureSkewSeconds + 1, reference) == false);
    // Without a trusted reference only the absolute bounds apply.
    CHECK(is_time_sane(reference + 30LL * 24 * 3600, 0));
}

TEST_CASE("Europe/Berlin offset follows EU DST rules") {
    CHECK(berlin_utc_offset_seconds(1705320000) == 3600);  // 2024-01-15
    CHECK(berlin_utc_offset_seconds(1711846799) == 3600);  // 2024-03-31 00:59:59Z
    CHECK(berlin_utc_offset_seconds(1711846800) == 7200);  // 2024-03-31 01:00:00Z
    CHECK(berlin_utc_offset_seconds(1719835200) == 7200);  // 2024-07-01
    CHECK(berlin_utc_offset_seconds(1729990799) == 7200);  // 2024-10-27 00:59:59Z
    CHECK(berlin_utc_offset_seconds(1729990800) == 3600);  // 2024-10-27 01:00:00Z
    CHECK(berlin_utc_offset_seconds(1735128000) == 3600);  // 2024-12-25
    CHECK(berlin_utc_offset_seconds(4102358400) == 3600);  // 2099-12-31
}
