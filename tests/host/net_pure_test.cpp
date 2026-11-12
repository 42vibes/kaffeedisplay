#include <doctest.h>

#include <cstring>
#include <string>
#include <vector>

#include "kaffee/net_pure.hpp"

using namespace kaffee;

namespace {

std::vector<uint8_t> make_query(uint16_t id, uint16_t flags, uint16_t qdcount, uint16_t qtype,
                                uint16_t qclass, bool terminate = true, bool compression = false,
                                bool include_qtype = true, bool include_qclass = true) {
    std::vector<uint8_t> query(12, 0);
    query[0] = static_cast<uint8_t>(id >> 8);
    query[1] = static_cast<uint8_t>(id);
    query[2] = static_cast<uint8_t>(flags >> 8);
    query[3] = static_cast<uint8_t>(flags);
    query[4] = static_cast<uint8_t>(qdcount >> 8);
    query[5] = static_cast<uint8_t>(qdcount);
    if (compression) {
        query.push_back(0xC0);
        query.push_back(0x0C);
    } else {
        for (const char* label : {"example", "test"}) {
            const size_t length = std::strlen(label);
            query.push_back(static_cast<uint8_t>(length));
            query.insert(query.end(), label, label + length);
        }
        if (terminate) {
            query.push_back(0);
        }
    }
    if (include_qtype) {
        query.push_back(static_cast<uint8_t>(qtype >> 8));
        query.push_back(static_cast<uint8_t>(qtype));
    }
    if (include_qclass) {
        query.push_back(static_cast<uint8_t>(qclass >> 8));
        query.push_back(static_cast<uint8_t>(qclass));
    }
    return query;
}

uint16_t read_u16(const uint8_t* data, size_t offset) {
    return static_cast<uint16_t>((data[offset] << 8) | data[offset + 1]);
}

}  // namespace

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

TEST_CASE("ApTimer re-arms for a bounded stop retry") {
    ApTimer timer;
    timer.start(100, 60);
    CHECK_FALSE(timer.tick(159));
    CHECK(timer.tick(160));
    CHECK_FALSE(timer.active());

    // stop_ap() uses a short re-arm when the driver refuses to stop.
    timer.start(160, 5);
    CHECK(timer.active());
    CHECK(timer.remaining(160) == 5);
    CHECK_FALSE(timer.tick(164));
    CHECK(timer.tick(165));
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

TEST_CASE("DNS parser rejects malformed queries") {
    uint8_t response[512] = {};
    size_t response_length = 0;

    CHECK_FALSE(build_dns_response(nullptr, 0, response, sizeof(response), response_length));

    const auto short_header = std::vector<uint8_t>(11, 0);
    CHECK_FALSE(build_dns_response(short_header.data(), short_header.size(), response,
                                   sizeof(response), response_length));

    const auto is_response = make_query(1, 0x8000, 1, 1, 1);
    CHECK_FALSE(build_dns_response(is_response.data(), is_response.size(), response,
                                   sizeof(response), response_length));

    const auto bad_opcode = make_query(1, 0x0800, 1, 1, 1);
    CHECK_FALSE(build_dns_response(bad_opcode.data(), bad_opcode.size(), response, sizeof(response),
                                   response_length));

    const auto no_question = make_query(1, 0x0100, 0, 1, 1);
    CHECK_FALSE(build_dns_response(no_question.data(), no_question.size(), response,
                                   sizeof(response), response_length));
    const auto two_questions = make_query(1, 0x0100, 2, 1, 1);
    CHECK_FALSE(build_dns_response(two_questions.data(), two_questions.size(), response,
                                   sizeof(response), response_length));

    const auto compressed = make_query(1, 0x0100, 1, 1, 1, true, true);
    CHECK_FALSE(build_dns_response(compressed.data(), compressed.size(), response, sizeof(response),
                                   response_length));

    const auto missing_root = make_query(1, 0x0100, 1, 1, 1, false);
    CHECK_FALSE(build_dns_response(missing_root.data(), missing_root.size(), response,
                                   sizeof(response), response_length));

    const auto missing_qtype = make_query(1, 0x0100, 1, 1, 1, true, false, false, true);
    CHECK_FALSE(build_dns_response(missing_qtype.data(), missing_qtype.size(), response,
                                   sizeof(response), response_length));
    const auto missing_qclass = make_query(1, 0x0100, 1, 1, 1, true, false, true, false);
    CHECK_FALSE(build_dns_response(missing_qclass.data(), missing_qclass.size(), response,
                                   sizeof(response), response_length));

    const auto a_query = make_query(0x1234, 0x0100, 1, 1, 1);
    CHECK_FALSE(build_dns_response(a_query.data(), a_query.size(), response, 20, response_length));
}

TEST_CASE("DNS parser answers A, AAAA and NXDOMAIN correctly") {
    uint8_t response[512] = {};
    size_t response_length = 0;

    const auto a_query = make_query(0x1234, 0x0100, 1, 1, 1);
    REQUIRE(build_dns_response(a_query.data(), a_query.size(), response, sizeof(response),
                               response_length));
    CHECK(response_length > 12);
    CHECK(response[0] == 0x12);
    CHECK(response[1] == 0x34);
    CHECK((response[2] & 0x80) != 0);   // QR
    CHECK((response[2] & 0x04) != 0);   // AA
    CHECK((response[2] & 0x01) != 0);   // RD copied
    CHECK(response[3] == 0x80);         // RA, NOERROR
    CHECK(read_u16(response, 4) == 1);  // QDCOUNT
    CHECK(read_u16(response, 6) == 1);  // ANCOUNT
    CHECK(response_length >= 4);
    CHECK(response[response_length - 4] == 192);
    CHECK(response[response_length - 3] == 168);
    CHECK(response[response_length - 2] == 4);
    CHECK(response[response_length - 1] == 1);

    const auto aaaa_query = make_query(7, 0x0000, 1, 28, 1);
    REQUIRE(build_dns_response(aaaa_query.data(), aaaa_query.size(), response, sizeof(response),
                               response_length));
    CHECK(response[3] == 0x80);         // NOERROR, empty answer
    CHECK((response[2] & 0x01) == 0);   // RD not set in query
    CHECK(read_u16(response, 6) == 0);  // ANCOUNT

    const auto mx_query = make_query(9, 0x0100, 1, 15, 1);
    REQUIRE(build_dns_response(mx_query.data(), mx_query.size(), response, sizeof(response),
                               response_length));
    CHECK(response[3] == 0x83);         // NXDOMAIN
    CHECK(read_u16(response, 6) == 0);  // ANCOUNT

    const auto a_ch_class = make_query(11, 0x0100, 1, 1, 3);
    REQUIRE(build_dns_response(a_ch_class.data(), a_ch_class.size(), response, sizeof(response),
                               response_length));
    CHECK(response[3] == 0x83);
}

TEST_CASE("Hostname validation") {
    CHECK(is_valid_hostname("kaffeekasse"));
    CHECK(is_valid_hostname("kaffee-02"));
    CHECK(is_valid_hostname("A1"));
    CHECK_FALSE(is_valid_hostname(""));
    CHECK_FALSE(is_valid_hostname("-kaffee"));
    CHECK_FALSE(is_valid_hostname("kaffee-"));
    CHECK_FALSE(is_valid_hostname("kaffee_display"));
    CHECK_FALSE(is_valid_hostname("kaffee.local"));
    CHECK_FALSE(is_valid_hostname(std::string(33, 'a')));
    CHECK(is_valid_hostname(std::string(32, 'a')));
}
