// The trail ribbon against Pathing's strip, worked through by hand from
// StandardTrail.World.cs: which side is u = 0, which way v runs, and that the strip is
// joined.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/vector_relational.hpp>

#include <cmath>

#include "viewer/live/bake.h"
#include "viewer/render/ribbon.h"

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

TEST_CASE("a step cut across is the same strip, in equal pieces", "[ribbon]") {
    std::vector<viewer::RibbonQuad> quads;
    viewer::appendRibbon({{0, 0, 0}, {9, 0, 0}}, kHalf, false, 0.0f, &quads);
    REQUIRE(quads.size() == 1);
    const float v_span = quads[0].uv[0].y - quads[0].uv[3].y;

    std::vector<viewer::RibbonQuad> pieces;
    viewer::cutStep(quads[0], 2.0f, &pieces);
    REQUIRE(pieces.size() == 5); // 9 m in pieces of at most 2: five of 1.8
    CHECK(pieces.front().corners[0].x == Approx(0.0f));
    CHECK(pieces.back().corners[2].x == Approx(9.0f));
    for (size_t i = 0; i < pieces.size(); ++i) {
        const viewer::RibbonQuad& piece = pieces[i];
        CHECK(piece.corners[2].x - piece.corners[1].x == Approx(1.8f));
        CHECK(piece.uv[0].y - piece.uv[3].y == Approx(v_span / 5.0f)); // v still counts down, as fast
        CHECK(std::min(piece.uv[0].y, piece.uv[3].y) >= 0.0f);
        CHECK(std::min(piece.uv[0].y, piece.uv[3].y) < 1.0f);
        CHECK(piece.uv[0].x == 0.0f);
        CHECK(piece.uv[1].x == 1.0f);
        if (i > 0) {
            CHECK(piece.corners[0].x == Approx(pieces[i - 1].corners[3].x));
            CHECK(piece.corners[1].x == Approx(pieces[i - 1].corners[2].x));
            // The same image across a cut: v the same but for whole lengths.
            const float jump = pieces[i - 1].uv[3].y - piece.uv[0].y;
            CHECK(jump == Approx(std::round(jump)).margin(1e-4));
        }
    }

    pieces.clear();
    viewer::cutStep(quads[0], 10.0f, &pieces);
    CHECK(pieces.size() == 1); // short enough already: as it was
}

TEST_CASE("a ribbon built once and scrolled is the ribbon built at that scroll", "[ribbon]") {
    // A bend and a run of 40 points, so the second segment spans stretches, and steps of every
    // length against the texture's.
    std::vector<pack::trl::Segment> segments(2);
    segments[0].points = {{0, 0, 0}, {0.3f, 0, 0}, {0.3f, 0, 2.5f}};
    for (int i = 0; i < 40; ++i) segments[1].points.push_back({static_cast<float>(i) * 0.7f, 1.0f, std::sin(static_cast<float>(i))});
    const viewer::TrailRibbon ribbon = viewer::buildRibbon(segments, kHalf, false);

    for (const float scroll : {0.0f, 0.25f, 0.999f, 3.6f, -1.3f, 4321.75f}) {
        std::vector<viewer::RibbonQuad> expected;
        for (const pack::trl::Segment& segment : segments) viewer::appendRibbon(segment.points, kHalf, false, scroll, &expected);
        REQUIRE(ribbon.quads.size() == expected.size());
        for (size_t q = 0; q < expected.size(); ++q) {
            const viewer::RibbonQuad quad = viewer::scrolled(ribbon.quads[q], scroll);
            for (int i = 0; i < 4; ++i) {
                CHECK(quad.corners[i] == expected[q].corners[i]);
                CHECK(quad.uv[i].x == expected[q].uv[i].x);
                CHECK(quad.uv[i].y == Approx(expected[q].uv[i].y).margin(1e-3f));
            }
        }
    }
}

TEST_CASE("a ribbon's stretches cover its quads in order, each within its box", "[ribbon]") {
    std::vector<pack::trl::Segment> segments(2);
    for (int i = 0; i < 50; ++i) segments[0].points.push_back({static_cast<float>(i), 0, 0});
    segments[1].points = {{0, 5, 0}, {0, 5, 1}};
    const viewer::TrailRibbon ribbon = viewer::buildRibbon(segments, kHalf, false);

    size_t next = 0;
    for (const viewer::TrailRibbon::Stretch& stretch : ribbon.stretches) {
        CHECK(stretch.first == next);
        REQUIRE(stretch.count > 0);
        next += stretch.count;
        for (size_t q = stretch.first; q < stretch.first + stretch.count; ++q) {
            for (const glm::vec3& corner : ribbon.quads[q].corners) {
                CHECK(glm::all(glm::greaterThanEqual(corner, stretch.min)));
                CHECK(glm::all(glm::lessThanEqual(corner, stretch.max)));
                CHECK(glm::all(glm::greaterThanEqual(corner, ribbon.min)));
                CHECK(glm::all(glm::lessThanEqual(corner, ribbon.max)));
            }
        }
    }
    CHECK(next == ribbon.quads.size());
    // No stretch runs across two segments: the last one is the second segment's step alone.
    CHECK(ribbon.stretches.back().count == 1);
}
