#include <doctest.h>

#include <string>

#include "kaffee/auth.hpp"

using namespace kaffee;

TEST_CASE("pbkdf2-hmac-sha256 known vectors") {
    // Vectors from the widely published PBKDF2-HMAC-SHA256 test set.
    const std::string salt = "73616c74";  // "salt"
    CHECK(pbkdf2_sha256_hex("password", salt, 1) ==
          "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
    CHECK(pbkdf2_sha256_hex("password", salt, 2) ==
          "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43");
    CHECK(pbkdf2_sha256_hex("password", salt, 4096) ==
          "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a");
    CHECK(pbkdf2_sha256_hex("password", salt, 1, 64).size() == 128);
}

TEST_CASE("constant time compare") {
    CHECK(constant_time_equal("abc", "abc"));
    CHECK_FALSE(constant_time_equal("abc", "abd"));
    CHECK_FALSE(constant_time_equal("abc", "ab"));
    CHECK(constant_time_equal("", ""));
}
