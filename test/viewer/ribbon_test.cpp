// The trail ribbon against Pathing's strip, worked through by hand from
// StandardTrail.World.cs: which side is u = 0, which way v runs, and that the strip is
// joined.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "viewer/bake.h"
#include "viewer/ribbon.h"

using Catch::Approx;

namespace {

constexpr float kHalf = 0.5f;

} // namespace

TEST_CASE("a flat ribbon has u = 0 on the path's left and the texture's top ahead", "[ribbon][regression]") {
    // Due east along the ground. Facing east in MumbleLink's space (x east, z north), the left
    // is +z — where Pathing's left vertex lands once its Z-up crosses are brought over.
    const std::vector<glm::vec3> points = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}};
    std::vector<viewer::RibbonQuad> quads;
    viewer::appendRibbon(points, kHalf, false, 0.0f, &quads);

    REQUIRE(quads.size() == 2);
    const viewer::RibbonQuad& first = quads[0];

    CHECK(first.corners[0].z == Approx(kHalf)); // left of the first point
    CHECK(first.corners[1].z == Approx(-kHalf)); // right of it
    CHECK(first.uv[0].x == Approx(0.0f));
    CHECK(first.uv[1].x == Approx(1.0f));

    // v counts down the path, by one per texture length.
    const float texture_length = 2.0f * viewer::kTrailHalfWidth;
    CHECK(first.uv[3].y - first.uv[0].y == Approx(-1.0f / texture_length));
    for (const viewer::RibbonQuad& quad : quads) CHECK(quad.corners[0].y == Approx(0.0f));
}

TEST_CASE("a ribbon is one joined strip", "[ribbon]") {
    // A right-angle bend: per-segment quads would leave a notch at the corner.
    const std::vector<glm::vec3> points = {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}};
    std::vector<viewer::RibbonQuad> quads;
    viewer::appendRibbon(points, kHalf, false, 0.0f, &quads);

    REQUIRE(quads.size() == 2);
    CHECK(quads[1].corners[0] == quads[0].corners[3]);
    CHECK(quads[1].corners[1] == quads[0].corners[2]);
}

TEST_CASE("a wall ribbon stands up", "[ribbon]") {
    const std::vector<glm::vec3> points = {{0, 0, 0}, {1, 0, 0}};
    std::vector<viewer::RibbonQuad> quads;
    viewer::appendRibbon(points, kHalf, true, 0.0f, &quads);

    REQUIRE(quads.size() == 1);
    CHECK(std::abs(quads[0].corners[0].y - quads[0].corners[1].y) == Approx(2.0f * kHalf));
    CHECK(quads[0].corners[0].z == Approx(0.0f));
}

TEST_CASE("scrolling moves v and only v", "[ribbon]") {
    const std::vector<glm::vec3> points = {{0, 0, 0}, {0.1f, 0, 0}};
    std::vector<viewer::RibbonQuad> still;
    std::vector<viewer::RibbonQuad> moved;
    viewer::appendRibbon(points, kHalf, false, 0.0f, &still);
    viewer::appendRibbon(points, kHalf, false, 0.25f, &moved);

    REQUIRE(still.size() == 1);
    REQUIRE(moved.size() == 1);
    for (int i = 0; i < 4; ++i) {
        CHECK(moved[0].corners[i] == still[0].corners[i]);
        CHECK(moved[0].uv[i].x == still[0].uv[i].x);
        // Up to whole texture lengths, which are the same image.
        const float delta = moved[0].uv[i].y - still[0].uv[i].y - 0.25f;
        CHECK(delta == Approx(std::round(delta)).margin(1e-4f));
    }
}

TEST_CASE("a vertical step keeps the ribbon's width", "[ribbon]") {
    // Straight up has no flat offset; the step keeps the one before rather than collapsing.
    const std::vector<glm::vec3> points = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {2, 1, 0}};
    std::vector<viewer::RibbonQuad> quads;
    viewer::appendRibbon(points, kHalf, false, 0.0f, &quads);

    REQUIRE(quads.size() == 3);
    for (const viewer::RibbonQuad& quad : quads) {
        CHECK(glm::length(quad.corners[0] - quad.corners[1]) == Approx(2.0f * kHalf));
        for (const glm::vec3& corner : quad.corners) CHECK(std::isfinite(corner.x + corner.y + corner.z));
    }
}

TEST_CASE("fewer than two points, or no length, is no ribbon", "[ribbon]") {
    std::vector<viewer::RibbonQuad> quads;
    viewer::appendRibbon({}, kHalf, false, 0.0f, &quads);
    viewer::appendRibbon({{1, 2, 3}}, kHalf, false, 0.0f, &quads);
    viewer::appendRibbon({{1, 2, 3}, {1, 2, 3}}, kHalf, false, 0.0f, &quads);
    CHECK(quads.empty());
}
