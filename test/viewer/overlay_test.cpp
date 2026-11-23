// The draw-list quad path, run into a real ImDrawList with no device behind it. What is
// asserted is the buffer the sink hands ImGui: how much was emitted, and that every index
// points at a vertex that was actually written.

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>

#include <cmath>

#include "draw_list.h"
#include "test_camera.h"
#include "viewer/render/overlay.h"

namespace {

using viewer::test::DrawListFixture;
using viewer::test::fakeTexture;

const ImVec2 kUv[4] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};

// Every reservation handed back, every index in range, every vertex on a real screen
// position. A miscounted PrimUnreserve leaves uninitialised vertices referenced at the end
// of the buffer; a corner projected from behind the camera lands at the screen origin.
void checkBufferConsistent(const ImDrawList& list) {
    CHECK(list._VtxWritePtr == list.VtxBuffer.Data + list.VtxBuffer.Size);
    CHECK(list._IdxWritePtr == list.IdxBuffer.Data + list.IdxBuffer.Size);
    CHECK(list.IdxBuffer.Size % 3 == 0);
    for (int i = 0; i < list.IdxBuffer.Size; ++i) REQUIRE(list.IdxBuffer[i] < static_cast<ImDrawIdx>(list.VtxBuffer.Size));
    for (int i = 0; i < list.VtxBuffer.Size; ++i) {
        REQUIRE(std::isfinite(list.VtxBuffer[i].pos.x));
        REQUIRE(std::isfinite(list.VtxBuffer[i].pos.y));
    }
}

} // namespace

TEST_CASE("a quad wholly in front and square on is one AddImageQuad", "[overlay]") {
    DrawListFixture fixture;
    const viewer::Camera camera = test_camera::axisCamera();

    glm::vec3 world[4];
    REQUIRE(camera.billboardQuad(glm::vec3(0.0f, 0.0f, 50.0f), 4.0f, world));

    viewer::overlay::ClippedQuad quad;
    REQUIRE(viewer::overlay::clipAndProject(camera, world, &quad));
    CHECK(quad.count == 4);

    viewer::overlay::drawTexturedQuad(&fixture.list(), camera, fakeTexture(), quad, kUv, IM_COL32_WHITE);
    CHECK(fixture.list().VtxBuffer.Size == 4);
    CHECK(fixture.list().IdxBuffer.Size == 6);
    checkBufferConsistent(fixture.list());
}

TEST_CASE("a quad running away from the camera is subdivided in one draw command", "[overlay]") {
    DrawListFixture fixture;
    const viewer::Camera camera = test_camera::axisCamera();

    // A strip of floor from a metre ahead to fifty: its near and far ends project at very
    // different depths, which is what affine interpolation gets wrong.
    const glm::vec3 world[4] = {{-1.0f, -1.0f, 1.0f}, {1.0f, -1.0f, 1.0f}, {1.0f, -1.0f, 50.0f}, {-1.0f, -1.0f, 50.0f}};

    viewer::overlay::ClippedQuad quad;
    REQUIRE(viewer::overlay::clipAndProject(camera, world, &quad));

    const int commands_before = fixture.list().CmdBuffer.Size;
    viewer::overlay::drawTexturedQuad(&fixture.list(), camera, fakeTexture(), quad, kUv, IM_COL32_WHITE);

    CHECK(fixture.list().VtxBuffer.Size > 4);
    // The grid is drawn under one texture id: one command for it, however many cells.
    CHECK(fixture.list().CmdBuffer.Size - commands_before <= 1);
    checkBufferConsistent(fixture.list());
}

TEST_CASE("a quad straddling the near plane draws only what is in front", "[overlay][regression]") {
    DrawListFixture fixture;
    const viewer::Camera camera = test_camera::axisCamera();

    // Standing on it: the floor runs from behind the camera to ten metres ahead.
    const glm::vec3 world[4] = {{-1.0f, -1.0f, -5.0f}, {1.0f, -1.0f, -5.0f}, {1.0f, -1.0f, 10.0f}, {-1.0f, -1.0f, 10.0f}};

    viewer::overlay::ClippedQuad quad;
    REQUIRE(viewer::overlay::clipAndProject(camera, world, &quad));
    CHECK(quad.count == 4); // the plane cuts two edges, leaving a quad of the front part

    viewer::overlay::drawTexturedQuad(&fixture.list(), camera, fakeTexture(), quad, kUv, IM_COL32_WHITE);
    CHECK(fixture.list().VtxBuffer.Size > 0);
    checkBufferConsistent(fixture.list());

    // Below the horizon and in front: nothing may come out above the screen's middle, which
    // is where a vertex projected from behind the camera would flip to.
    for (int i = 0; i < fixture.list().VtxBuffer.Size; ++i) CHECK(fixture.list().VtxBuffer[i].pos.y >= test_camera::kHeight * 0.5f);
}

TEST_CASE("a quad wholly behind the camera is not drawn", "[overlay]") {
    const viewer::Camera camera = test_camera::axisCamera();
    const glm::vec3 world[4] = {{-1.0f, -1.0f, -5.0f}, {1.0f, -1.0f, -5.0f}, {1.0f, 1.0f, -5.0f}, {-1.0f, 1.0f, -5.0f}};

    viewer::overlay::ClippedQuad quad;
    CHECK_FALSE(viewer::overlay::clipAndProject(camera, world, &quad));
    CHECK(quad.count == 0);
}

TEST_CASE("a fully transparent quad emits nothing", "[overlay]") {
    DrawListFixture fixture;
    const viewer::Camera camera = test_camera::axisCamera();

    glm::vec3 world[4];
    REQUIRE(camera.billboardQuad(glm::vec3(0.0f, 0.0f, 50.0f), 4.0f, world));
    viewer::overlay::ClippedQuad quad;
    REQUIRE(viewer::overlay::clipAndProject(camera, world, &quad));

    viewer::overlay::drawTexturedQuad(&fixture.list(), camera, fakeTexture(), quad, kUv, IM_COL32(255, 255, 255, 0));
    CHECK(fixture.list().VtxBuffer.Size == 0);
}
