// Camera projection maths. Nothing here needs the game or a pack — a Camera is a handful
// of matrices and these are the closed-form answers Blish's renderer produces for the same
// inputs, which is the only way to check marker sizing without staring at the game.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>

#include "test_camera.h"
#include "viewer/camera.h"

using Catch::Approx;

namespace {

using test_camera::axisCamera;
using test_camera::kFov;
using test_camera::kHeight;
using test_camera::kWidth;

// Pixels per world unit at one unit of *depth*: half the viewport spans tan(fov/2) at unit
// distance along the view axis.
float pixelsPerUnitAtUnitDepth() {
    return (kHeight * 0.5f) / std::tan(kFov * 0.5f);
}

float billboardHeight(const viewer::Camera& camera, const glm::vec3& position, float size) {
    glm::vec2 corners[4];
    REQUIRE(camera.billboardCorners(position, size, corners));
    return viewer::quadPixelHeight(corners);
}

// How far a texture feature is misplaced by spreading an edge's content evenly over its two
// projected ends: where the middle of the edge really lands, against where the halfway
// point between the ends is. This is exactly the error a 2D draw list makes, since it has
// no w to divide by.
float affineEdgeError(const viewer::Camera& camera, const glm::vec3& a, const glm::vec3& b) {
    glm::vec2 screen_a(0.0f), screen_b(0.0f), screen_middle(0.0f);
    REQUIRE(camera.project(a, &screen_a));
    REQUIRE(camera.project(b, &screen_b));
    REQUIRE(camera.project((a + b) * 0.5f, &screen_middle));
    return glm::length((screen_a + screen_b) * 0.5f - screen_middle);
}

} // namespace

TEST_CASE("a billboard on the optical axis is size over distance", "[camera]") {
    const viewer::Camera camera = axisCamera();

    // Straight ahead, depth and radial distance are the same thing, so this is the one
    // case where the naive formula is also right.
    const float height = billboardHeight(camera, glm::vec3(0.0f, 0.0f, 100.0f), 2.0f);

    CHECK(height == Approx(2.0f / 100.0f * pixelsPerUnitAtUnitDepth()).epsilon(0.001f));
}

TEST_CASE("a billboard off to the side is sized by depth, not radial distance", "[camera][regression]") {
    const viewer::Camera camera = axisCamera();

    // 40 degrees off-axis horizontally, which at this FOV is still on screen. The quad
    // faces the camera, so its up axis stays vertical and its projected height is
    // size/depth — radial distance is 1/cos(40) = 1.3x larger and sizing by it made
    // markers near the edge of the screen visibly small.
    const float size = 2.0f;
    const float depth = 100.0f;
    const float x = depth * std::tan(0.698f);
    const float radius = std::sqrt(x * x + depth * depth);
    const float height = billboardHeight(camera, glm::vec3(x, 0.0f, depth), size);

    // The quad's near edge is turned a little towards the camera, which puts it
    // (size/2)*x/r closer than the marker itself. That is exact, not a fudge — the same
    // 0.6% Blish's own projected bounding rectangle picks up.
    const float near_edge = depth - (size * 0.5f) * x / radius;
    CHECK(height == Approx(size / near_edge * pixelsPerUnitAtUnitDepth()).epsilon(0.002f));

    // And the thing that regressed: it must not shrink as it moves off the axis. Sizing
    // by radial distance made this 24% smaller than the on-axis marker.
    const float on_axis = billboardHeight(camera, glm::vec3(0.0f, 0.0f, depth), size);
    CHECK(height == Approx(on_axis).epsilon(0.01f));
    CHECK(height > on_axis);
}

TEST_CASE("a billboard above the axis tilts towards the camera and grows", "[camera][regression]") {
    const viewer::Camera camera = axisCamera();

    // Offset vertically instead, and the billboard's up axis tilts to face the camera.
    // The quad subtends size/r radians and lands on the image plane magnified by
    // 1/cos^2, so the projected height is size * r / depth^2 — larger than on-axis, where
    // the radial formula makes it smaller.
    const float depth = 100.0f;
    const float y = depth * std::tan(0.524f); // 30 degrees up
    const float radius = std::sqrt(y * y + depth * depth);
    const float height = billboardHeight(camera, glm::vec3(0.0f, y, depth), 2.0f);

    CHECK(height == Approx(2.0f * radius / (depth * depth) * pixelsPerUnitAtUnitDepth()).epsilon(0.005f));

    const float on_axis = billboardHeight(camera, glm::vec3(0.0f, 0.0f, depth), 2.0f);
    CHECK(height > on_axis);
}

TEST_CASE("billboard height scales linearly with size and inversely with depth", "[camera]") {
    const viewer::Camera camera = axisCamera();

    const float base = billboardHeight(camera, glm::vec3(0.0f, 0.0f, 50.0f), 2.0f);

    CHECK(billboardHeight(camera, glm::vec3(0.0f, 0.0f, 50.0f), 4.0f) == Approx(base * 2.0f).epsilon(0.001f));
    CHECK(billboardHeight(camera, glm::vec3(0.0f, 0.0f, 100.0f), 2.0f) == Approx(base * 0.5f).epsilon(0.001f));
}

TEST_CASE("a billboard straddling the camera plane fails to project", "[camera]") {
    const viewer::Camera camera = axisCamera();

    // In the camera's face: Blish draws it clipped and enormous, and the maxSize clamp is
    // what bounds it. The renderer needs to be told there is no quad to speak of.
    //
    // It has to be off to the side to straddle at all — a billboard is perpendicular to
    // the ray, so one dead ahead stays parallel to the camera plane however close it gets.
    glm::vec2 corners[4];
    CHECK_FALSE(camera.billboardCorners(glm::vec3(3.0f, 0.0f, 0.5f), 4.0f, corners));
    CHECK(camera.billboardCorners(glm::vec3(0.0f, 0.0f, 0.5f), 4.0f, corners));
}

TEST_CASE("a billboard is square and upright on the optical axis", "[camera][orientation]") {
    const viewer::Camera camera = axisCamera();

    glm::vec2 corners[4];
    REQUIRE(camera.billboardCorners(glm::vec3(0.0f, 0.0f, 100.0f), 2.0f, corners));

    // kQuadCorners order is (-right,-up), (+right,-up), (+right,+up), (-right,+up), and
    // screen y points down — so corners 0 and 1 are the *bottom* pair. The UV tables in
    // renderer.cpp are written against exactly this, so it is worth pinning.
    CHECK(corners[0].x < corners[1].x);
    CHECK(corners[3].x < corners[2].x);
    CHECK(corners[0].y > corners[3].y);
    CHECK(corners[1].y > corners[2].y);

    // Dead ahead there is no tilt, so it comes out an axis-aligned square.
    CHECK(corners[0].x == Approx(corners[3].x));
    CHECK(corners[1].x == Approx(corners[2].x));
    CHECK(corners[0].y == Approx(corners[1].y));
    CHECK((corners[1].x - corners[0].x) == Approx(corners[0].y - corners[3].y));
}

TEST_CASE("a billboard off the axis is tilted, not axis-aligned", "[camera][orientation]") {
    const viewer::Camera camera = axisCamera();

    // Up and to the side, which is where drawing an axis-aligned square visibly parted
    // company with Blish. The billboard turns to face the camera, so its top and bottom
    // edges are no longer level with each other on screen.
    glm::vec2 corners[4];
    REQUIRE(camera.billboardCorners(glm::vec3(40.0f, 30.0f, 60.0f), 4.0f, corners));

    const float bottom_tilt = corners[1].y - corners[0].y;
    const float top_tilt = corners[2].y - corners[3].y;

    CHECK(std::abs(bottom_tilt) > 1.0f);
    // Both edges lean the same way, which is what a rotation looks like as opposed to a
    // shear or a corner landing in the wrong slot.
    CHECK(bottom_tilt * top_tilt > 0.0f);

    // And it is still wound the same way round, so the UVs still land where they should.
    CHECK(corners[0].x < corners[1].x);
    CHECK(corners[0].y > corners[3].y);
}

TEST_CASE("billboardQuad is billboardCorners before the projection", "[camera]") {
    const viewer::Camera camera = axisCamera();

    glm::vec3 world[4];
    glm::vec2 corners[4];
    REQUIRE(camera.billboardQuad(glm::vec3(40.0f, 30.0f, 60.0f), 4.0f, world));
    REQUIRE(camera.billboardCorners(glm::vec3(40.0f, 30.0f, 60.0f), 4.0f, corners));

    for (int i = 0; i < 4; ++i) {
        glm::vec2 projected(0.0f);
        REQUIRE(camera.project(world[i], &projected));
        CHECK(projected.x == Approx(corners[i].x));
        CHECK(projected.y == Approx(corners[i].y));
    }
}

TEST_CASE("a quad square on to the camera needs no subdividing", "[camera]") {
    const viewer::Camera camera = axisCamera();

    // Dead ahead the billboard is parallel to the camera plane, so all four corners come
    // back at the same depth and interpolating affinely across them is already exact.
    glm::vec3 world[4];
    REQUIRE(camera.billboardQuad(glm::vec3(0.0f, 0.0f, 50.0f), 20.0f, world));

    const viewer::QuadGrid grid = viewer::perspectiveGrid(camera, world, glm::vec2(0.0f), glm::vec2(1.0f), 0.5f);
    CHECK(grid.columns == 1);
    CHECK(grid.rows == 1);
}

TEST_CASE("a large off-axis billboard is subdivided until affine is invisible", "[camera][regression]") {
    const viewer::Camera camera = axisCamera();

    // Big and well off the optical axis, which is where the corners land at very different
    // depths. Drawn as two affine triangles the icon bends along the diagonal instead of
    // foreshortening — the saddle this subdivision exists to remove.
    const glm::vec3 position(30.0f, 0.0f, 60.0f);
    glm::vec3 world[4];
    glm::vec2 screen[4];
    float depth[4];
    REQUIRE(camera.billboardQuad(position, 24.0f, world));
    for (int i = 0; i < 4; ++i) REQUIRE(camera.project(world[i], &screen[i], &depth[i]));

    const float near_depth = std::min({depth[0], depth[1], depth[2], depth[3]});
    const float far_depth = std::max({depth[0], depth[1], depth[2], depth[3]});
    CAPTURE(near_depth, far_depth);
    CHECK(far_depth > near_depth * 1.15f);

    // Drawn whole, the middle of an edge is tens of pixels from where the texture puts it.
    const float whole = affineEdgeError(camera, world[0], world[1]);
    CHECK(whole > 10.0f);

    const viewer::QuadGrid grid = viewer::perspectiveGrid(camera, world, glm::vec2(0.0f), glm::vec2(1.0f), 0.5f);
    CAPTURE(grid.columns, grid.rows, whole);
    REQUIRE(grid.columns > 1);
    REQUIRE(grid.columns < viewer::kMaxQuadSubdivisions); // or the cap, not the sizing, is on trial

    // Cut there, no cell is off by more than the pixel or so that was asked for.
    float worst = 0.0f;
    for (int i = 0; i < grid.columns; ++i) {
        const glm::vec3 a = world[0] + (world[1] - world[0]) * grid.u[i];
        const glm::vec3 b = world[0] + (world[1] - world[0]) * grid.u[i + 1];
        worst = std::max(worst, affineEdgeError(camera, a, b));
    }
    CHECK(worst < 1.0f);
    CHECK(worst < whole / 20.0f);
}

TEST_CASE("a long strip toward the camera is cut finer where it is nearer", "[camera][regression]") {
    const viewer::Camera camera = axisCamera();

    // A metre-wide trail step on the floor from thirty metres out to half a metre ahead. Cut
    // evenly along it, the cells near the camera span depths several times apart and are
    // stretched a cell at a time, a sawtooth; cut evenly in 1/sqrt(depth), each errs alike.
    const glm::vec3 world[4] = {{-1.5f, -1.5f, 30.0f}, {-0.5f, -1.5f, 30.0f}, {-0.5f, -1.5f, 0.5f}, {-1.5f, -1.5f, 0.5f}};
    const viewer::QuadGrid grid = viewer::perspectiveGrid(camera, world, glm::vec2(0.0f), glm::vec2(1.0f), 0.5f);
    CAPTURE(grid.columns, grid.rows);

    // Across it the depth hardly changes; along it, it changes sixty-fold.
    CHECK(grid.columns < grid.rows);
    REQUIRE(grid.rows > 2);

    // v runs from the far end to the near one: each cut nearer the camera is shorter.
    for (int i = 0; i + 2 <= grid.rows; ++i) CHECK(grid.v[i + 1] - grid.v[i] > grid.v[i + 2] - grid.v[i + 1]);
    CHECK(grid.v[0] == 0.0f);
    CHECK(grid.v[grid.rows] == 1.0f);
}

TEST_CASE("clipping a quad's parameter space against the near plane", "[camera]") {
    glm::vec2 clipped[5];
    const glm::vec2 low(0.0f);
    const glm::vec2 high(1.0f);

    SECTION("all of it in front is the whole quad, corners in order") {
        const float depth[4] = {1.0f, 2.0f, 3.0f, 2.0f};
        REQUIRE(viewer::clipQuadRegion(depth, low, high, clipped) == 4);
        for (int i = 0; i < 4; ++i) {
            CHECK(clipped[i].x == Approx(i == 1 || i == 2 ? 1.0f : 0.0f));
            CHECK(clipped[i].y == Approx(i == 2 || i == 3 ? 1.0f : 0.0f));
        }
    }

    SECTION("all of it behind is nothing at all") {
        const float depth[4] = {-1.0f, -2.0f, -3.0f, -2.0f};
        CHECK(viewer::clipQuadRegion(depth, low, high, clipped) == 0);
    }

    SECTION("one corner behind cuts it into a pentagon") {
        const float depth[4] = {-1.0f, 10.0f, 20.0f, 10.0f};
        REQUIRE(viewer::clipQuadRegion(depth, low, high, clipped) == 5);
        for (int i = 0; i < 5; ++i) CHECK(viewer::bilinearValue(depth, clipped[i]) >= -0.0001f);
    }

    SECTION("three corners behind leaves a triangle") {
        const float depth[4] = {10.0f, -10.0f, -20.0f, -10.0f};
        REQUIRE(viewer::clipQuadRegion(depth, low, high, clipped) == 3);
        for (int i = 0; i < 3; ++i) CHECK(viewer::bilinearValue(depth, clipped[i]) >= -0.0001f);
    }

    SECTION("a subdivision cell is clipped against the same corner depths") {
        // The renderer grids the whole quad and re-clips each cell, so the region asked
        // about is a sub-rectangle rather than the unit square.
        const float depth[4] = {-4.0f, -4.0f, 4.0f, 4.0f}; // the cut runs across at v = 0.5
        CHECK(viewer::clipQuadRegion(depth, glm::vec2(0.0f, 0.0f), glm::vec2(0.5f, 0.25f), clipped) == 0);
        CHECK(viewer::clipQuadRegion(depth, glm::vec2(0.0f, 0.75f), glm::vec2(0.5f, 1.0f), clipped) == 4);

        REQUIRE(viewer::clipQuadRegion(depth, glm::vec2(0.0f, 0.25f), glm::vec2(0.5f, 0.75f), clipped) == 4);
        for (int i = 0; i < 4; ++i) CHECK(clipped[i].y >= 0.5f - 0.0001f);
    }
}

TEST_CASE("a quad the camera stands inside keeps the part still in front", "[camera][regression]") {
    const viewer::Camera camera = axisCamera();

    // The floor you are standing on: a big flat quad whose centre is behind the camera and
    // which reaches well past it. Testing the marker's centre — which is what used to
    // decide this — drops the whole thing, and a large marker then vanishes the moment you
    // walk past its middle even though most of it is still on screen.
    const glm::vec3 world[4] = {{-20.0f, -1.0f, -10.0f}, {20.0f, -1.0f, -10.0f}, {20.0f, -1.0f, 30.0f}, {-20.0f, -1.0f, 30.0f}};

    float depth[4];
    camera.quadNearDepths(world, depth);
    CHECK(depth[0] < 0.0f);
    CHECK(depth[1] < 0.0f);
    CHECK(depth[2] > 0.0f);

    glm::vec2 clipped[5];
    const int count = viewer::clipQuadRegion(depth, glm::vec2(0.0f), glm::vec2(1.0f), clipped);
    REQUIRE(count == 4);

    for (int i = 0; i < count; ++i) {
        // Everything that survived is in front of the near plane and therefore projects.
        CHECK(viewer::bilinearValue(depth, clipped[i]) >= -0.0001f);
        glm::vec2 screen(0.0f);
        CHECK(camera.project(viewer::bilinearPoint(world, clipped[i]), &screen));
    }

    // And it is a slice of the quad, not the whole of it: the far edge is untouched and the
    // near one has been pulled forward to the plane.
    CHECK(clipped[1].y == Approx(1.0f));
    CHECK(clipped[2].y == Approx(1.0f));
    CHECK(clipped[0].y > 0.0f);
    CHECK(clipped[3].y > 0.0f);
}

TEST_CASE("a large billboard centred behind the camera is still partly visible", "[camera][regression]") {
    const viewer::Camera camera = axisCamera();

    // A billboard faces the ray to its own centre, so one centred behind the camera *and*
    // off to the side swings a corner back into view. Scaled up enough, that is most of it.
    glm::vec3 world[4];
    REQUIRE(camera.billboardQuad(glm::vec3(10.0f, 0.0f, -2.0f), 40.0f, world));

    float depth[4];
    camera.quadNearDepths(world, depth);

    glm::vec2 clipped[5];
    CHECK(viewer::clipQuadRegion(depth, glm::vec2(0.0f), glm::vec2(1.0f), clipped) >= 3);
}

TEST_CASE("project puts the view axis in the middle of the viewport", "[camera]") {
    const viewer::Camera camera = axisCamera();

    glm::vec2 screen;
    REQUIRE(camera.project(glm::vec3(0.0f, 0.0f, 10.0f), &screen));
    CHECK(screen.x == Approx(kWidth * 0.5f).margin(0.01f));
    CHECK(screen.y == Approx(kHeight * 0.5f).margin(0.01f));

    // Behind the camera has no meaningful screen position.
    CHECK_FALSE(camera.project(glm::vec3(0.0f, 0.0f, -10.0f), &screen));
}

TEST_CASE("project reports the depth it divided by", "[camera]") {
    const viewer::Camera camera = axisCamera();

    // Looking down +Z with a left-handed projection, clip w is the view-space depth
    // outright. Everything interpolating across a projected quad needs it, because the
    // screen positions alone no longer say how the surface between them is foreshortened.
    glm::vec2 screen(0.0f);
    float depth = 0.0f;
    REQUIRE(camera.project(glm::vec3(4.0f, -3.0f, 25.0f), &screen, &depth));
    CHECK(depth == Approx(25.0f));

    // Written even when the point is behind the camera, where it comes out negative.
    CHECK_FALSE(camera.project(glm::vec3(0.0f, 0.0f, -10.0f), &screen, &depth));
    CHECK(depth == Approx(-10.0f));
}
