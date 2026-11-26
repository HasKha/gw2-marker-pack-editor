// The draw-list quad path, run into a real ImDrawList with no device behind it. What is
// asserted is the buffer the sink hands ImGui: how much was emitted, and that every index
// points at a vertex that was actually written.

#include <catch2/catch_test_macros.hpp>

#include <imgui.h>

#include <cmath>
#include <optional>

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

// The texture coordinate of each corner is its place in the quad's own (u,v), so a uv names
// the point of the surface it should be drawn at.
const ImVec2 kParameterUv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

// The farthest, in pixels, any point of the texture lands from where the perspective puts it,
// over every triangle drawn with kParameterUv: at the middle of each edge, where interpolating
// affinely across the triangle errs most. Only on screen: off it, nothing is seen to be wrong.
float worstTextureError(const ImDrawList& list, const viewer::Camera& camera, const glm::vec3 world[4]) {
    float worst = 0.0f;
    for (int i = 0; i + 2 < list.IdxBuffer.Size; i += 3) {
        for (int edge = 0; edge < 3; ++edge) {
            const ImDrawVert& a = list.VtxBuffer[list.IdxBuffer[i + edge]];
            const ImDrawVert& b = list.VtxBuffer[list.IdxBuffer[i + (edge + 1) % 3]];
            const glm::vec2 drawn((a.pos.x + b.pos.x) * 0.5f, (a.pos.y + b.pos.y) * 0.5f);
            const glm::vec2 uv((a.uv.x + b.uv.x) * 0.5f, (a.uv.y + b.uv.y) * 0.5f);
            glm::vec2 meant(0.0f);
            REQUIRE(camera.project(viewer::bilinearPoint(world, uv), &meant));
            const bool on_screen = meant.x >= 0.0f && meant.y >= 0.0f && meant.x <= camera.viewport.x && meant.y <= camera.viewport.y;
            if (on_screen) worst = std::max(worst, glm::length(drawn - meant));
        }
    }
    return worst;
}

} // namespace

TEST_CASE("a trail step running past the camera foreshortens smoothly", "[overlay][regression]") {
    // A flat trail from ahead and below to past the camera's side, as one running from the
    // character back past the camera does. Split evenly along its length, each cell near the
    // camera spans depths several times apart and is still interpolated affinely: the arrows
    // stretch too much at each cell's far end and too little at its near end, then jump at the
    // next — a sawtooth instead of the steady stretch the GPU draws.
    const viewer::Camera camera = test_camera::axisCamera();

    SECTION("wholly in front") {
        const glm::vec3 world[4] = {{-1.5f, -1.5f, 30.0f}, {-0.5f, -1.5f, 30.0f}, {-0.5f, -1.5f, 0.3f}, {-1.5f, -1.5f, 0.3f}};
        DrawListFixture fixture;
        const std::optional<viewer::overlay::ClippedQuad> quad = viewer::overlay::clipAndProject(camera, world);
        REQUIRE(quad);
        REQUIRE(quad->count == 4);
        viewer::overlay::drawTexturedQuad(&fixture.list(), camera, fakeTexture(), *quad, kParameterUv, IM_COL32_WHITE);
        checkBufferConsistent(fixture.list());
        CHECK(worstTextureError(fixture.list(), camera, world) < 2.0f);
    }

    SECTION("running on behind it") {
        const glm::vec3 world[4] = {{-1.5f, -1.5f, 30.0f}, {-0.5f, -1.5f, 30.0f}, {-0.5f, -1.5f, -10.0f}, {-1.5f, -1.5f, -10.0f}};
        DrawListFixture fixture;
        const std::optional<viewer::overlay::ClippedQuad> quad = viewer::overlay::clipAndProject(camera, world);
        REQUIRE(quad);
        viewer::overlay::drawTexturedQuad(&fixture.list(), camera, fakeTexture(), *quad, kParameterUv, IM_COL32_WHITE);
        checkBufferConsistent(fixture.list());
        CHECK(worstTextureError(fixture.list(), camera, world) < 2.0f);
    }
}

TEST_CASE("a quad wholly in front and square on is one AddImageQuad", "[overlay]") {
    DrawListFixture fixture;
    const viewer::Camera camera = test_camera::axisCamera();

    glm::vec3 world[4];
    REQUIRE(camera.billboardQuad(glm::vec3(0.0f, 0.0f, 50.0f), 4.0f, world));

    const std::optional<viewer::overlay::ClippedQuad> quad = viewer::overlay::clipAndProject(camera, world);
    REQUIRE(quad);
    CHECK(quad->count == 4);

    viewer::overlay::drawTexturedQuad(&fixture.list(), camera, fakeTexture(), *quad, kUv, IM_COL32_WHITE);
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

    const std::optional<viewer::overlay::ClippedQuad> quad = viewer::overlay::clipAndProject(camera, world);
    REQUIRE(quad);

    const int commands_before = fixture.list().CmdBuffer.Size;
    viewer::overlay::drawTexturedQuad(&fixture.list(), camera, fakeTexture(), *quad, kUv, IM_COL32_WHITE);

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

    const std::optional<viewer::overlay::ClippedQuad> quad = viewer::overlay::clipAndProject(camera, world);
    REQUIRE(quad);
    CHECK(quad->count == 4); // the plane cuts two edges, leaving a quad of the front part

    viewer::overlay::drawTexturedQuad(&fixture.list(), camera, fakeTexture(), *quad, kUv, IM_COL32_WHITE);
    CHECK(fixture.list().VtxBuffer.Size > 0);
    checkBufferConsistent(fixture.list());

    // Below the horizon and in front: nothing may come out above the screen's middle, which
    // is where a vertex projected from behind the camera would flip to.
    for (int i = 0; i < fixture.list().VtxBuffer.Size; ++i) CHECK(fixture.list().VtxBuffer[i].pos.y >= test_camera::kHeight * 0.5f);
}

TEST_CASE("a quad wholly behind the camera is not drawn", "[overlay]") {
    const viewer::Camera camera = test_camera::axisCamera();
    const glm::vec3 world[4] = {{-1.0f, -1.0f, -5.0f}, {1.0f, -1.0f, -5.0f}, {1.0f, 1.0f, -5.0f}, {-1.0f, 1.0f, -5.0f}};

    CHECK_FALSE(viewer::overlay::clipAndProject(camera, world));
}

TEST_CASE("a fully transparent quad emits nothing", "[overlay]") {
    DrawListFixture fixture;
    const viewer::Camera camera = test_camera::axisCamera();

    glm::vec3 world[4];
    REQUIRE(camera.billboardQuad(glm::vec3(0.0f, 0.0f, 50.0f), 4.0f, world));
    const std::optional<viewer::overlay::ClippedQuad> quad = viewer::overlay::clipAndProject(camera, world);
    REQUIRE(quad);

    viewer::overlay::drawTexturedQuad(&fixture.list(), camera, fakeTexture(), *quad, kUv, IM_COL32(255, 255, 255, 0));
    CHECK(fixture.list().VtxBuffer.Size == 0);
}
