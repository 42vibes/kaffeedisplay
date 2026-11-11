#include <doctest.h>

#include <initializer_list>

#include "kaffee/touch_transform.hpp"

using kaffee::touch::Point;
using kaffee::touch::transform;

TEST_CASE("touch transform keeps the raw point at rotation 0") {
    CHECK(transform(0, 0, 480, 480, 0) == Point{0, 0});
    CHECK(transform(479, 479, 480, 480, 0) == Point{479, 479});
    CHECK(transform(123, 456, 480, 480, 0) == Point{123, 456});
}

TEST_CASE("touch transform maps the panel corners for every rotation") {
    // Panel corners: top-left (0,0), top-right (479,0), bottom-left (0,479),
    // bottom-right (479,479).
    CHECK(transform(0, 0, 480, 480, 90) == Point{479, 0});
    CHECK(transform(479, 0, 480, 480, 90) == Point{479, 479});
    CHECK(transform(0, 479, 480, 480, 90) == Point{0, 0});
    CHECK(transform(479, 479, 480, 480, 90) == Point{0, 479});

    CHECK(transform(0, 0, 480, 480, 180) == Point{479, 479});
    CHECK(transform(479, 0, 480, 480, 180) == Point{0, 479});
    CHECK(transform(0, 479, 480, 480, 180) == Point{479, 0});
    CHECK(transform(479, 479, 480, 480, 180) == Point{0, 0});

    CHECK(transform(0, 0, 480, 480, 270) == Point{0, 479});
    CHECK(transform(479, 0, 480, 480, 270) == Point{0, 0});
    CHECK(transform(0, 479, 480, 480, 270) == Point{479, 479});
    CHECK(transform(479, 479, 480, 480, 270) == Point{479, 0});
}

TEST_CASE("touch transform is reversible for every rotation") {
    for (int rotation : {0, 90, 180, 270}) {
        for (int y = 0; y < 480; y += 37) {
            for (int x = 0; x < 480; x += 41) {
                const Point logical = transform(x, y, 480, 480, rotation);
                CHECK(logical.x >= 0);
                CHECK(logical.x < 480);
                CHECK(logical.y >= 0);
                CHECK(logical.y < 480);
            }
        }
    }
}

TEST_CASE("rotation normalization only accepts quarter turns") {
    CHECK(kaffee::touch::normalize_rotation(0) == 0);
    CHECK(kaffee::touch::normalize_rotation(90) == 90);
    CHECK(kaffee::touch::normalize_rotation(180) == 180);
    CHECK(kaffee::touch::normalize_rotation(270) == 270);
    CHECK(kaffee::touch::normalize_rotation(45) == 0);
    CHECK(kaffee::touch::normalize_rotation(-90) == 0);
}

TEST_CASE("clamp keeps points inside the drawable area") {
    CHECK(kaffee::touch::clamp(Point{-1, -1}, 480, 480) == Point{0, 0});
    CHECK(kaffee::touch::clamp(Point{480, 480}, 480, 480) == Point{479, 479});
    CHECK(kaffee::touch::clamp(Point{12, 34}, 480, 480) == Point{12, 34});
}
