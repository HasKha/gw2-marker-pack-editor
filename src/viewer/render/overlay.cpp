#include "viewer/render/overlay.h"

#include <vector>

namespace viewer::overlay {

namespace {

// One corner of a subdivision cell, blended out of the quad it belongs to.
struct GridVertex {
    ImVec2 position;
    ImVec2 coord;
    float depth = 0.0f;
};

GridVertex quadVertex(const Camera& camera, const ClippedQuad& quad, const ImVec2 uv[4], glm::vec2 at) {
    float weight[4];
    bilinearWeights(at, weight);

    GridVertex vertex;
    glm::vec3 position(0.0f);
    ImVec2 coord(0.0f, 0.0f);
    for (int i = 0; i < 4; ++i) {
        position += quad.world[i] * weight[i];
        coord.x += uv[i].x * weight[i];
        coord.y += uv[i].y * weight[i];
        vertex.depth += quad.corner_depth[i] * weight[i];
    }

    // Projected unconditionally rather than only when the depth says it is in front. A
    // vertex the clip put exactly *on* the near plane can come back a hair negative, and
    // skipping it would leave a corner sitting at the screen origin — a stray triangle
    // across the whole viewport. Only vertices already known to be in front are read, so a
    // failure here never reaches the buffer.
    glm::vec2 projected(0.0f);
    camera.project(position, &projected);
    vertex.position = ImVec2(projected.x, projected.y);
    vertex.coord = coord;
    return vertex;
}

} // namespace

std::optional<ClippedQuad> clipAndProject(const Camera& camera, const glm::vec3 world[4]) {
    ClippedQuad out;
    for (int i = 0; i < 4; ++i) out.world[i] = world[i];
    camera.quadNearDepths(world, out.corner_depth);

    glm::vec2 parameter[5]; // where each surviving corner sits in the quad's own (u,v)
    out.count = clipQuadRegion(out.corner_depth, glm::vec2(0.0f), glm::vec2(1.0f), parameter);
    if (out.count < 3) return std::nullopt;

    for (int i = 0; i < out.count; ++i) {
        // Everything here is at or past the near plane, so the projection cannot fail.
        if (!camera.project(bilinearPoint(world, parameter[i]), &out.screen[i], &out.depth[i])) return std::nullopt;
    }
    return out;
}

int screenCorners(const ClippedQuad& quad, ImVec2 out[5]) {
    for (int i = 0; i < quad.count; ++i) out[i] = ImVec2(quad.screen[i].x, quad.screen[i].y);
    return quad.count;
}

void drawTexturedQuad(
    ImDrawList* list,
    const Camera& camera,
    ImTextureID texture,
    const ClippedQuad& quad,
    const ImVec2 uv[4],
    ImU32 colour) {
    // ImGui has no w to divide by. AddImageQuad rasterizes two triangles and interpolates the
    // UVs affinely across them, which is only correct when all four corners sit at the same
    // depth — and they never do. A billboard is perpendicular to the ray to its own centre,
    // so the further off-axis it sits the more its corners differ in view-space depth, and a
    // marker carrying `rotate` is tilted by construction. What comes out is a texture bent
    // along the quad's diagonal rather than foreshortened: on a large marker, a saddle.
    //
    // So the quad is subdivided in *world* space and every vertex projected. All four
    // corners of every quad drawn here are coplanar, so a bilinear blend of them stays on the
    // real surface, and each cell ends up small enough that affine is invisible. Where to
    // cut is perspectiveGrid, which lives with the projection because that is what it is
    // about; this is the error it is allowed to leave, in pixels.
    constexpr float kQuadPixelTolerance = 0.5f;

    if ((colour & IM_COL32_A_MASK) == 0) return; // what AddImageQuad checks for us below
    if (quad.count < 3) return;

    const float* corner_depth = quad.corner_depth;
    const bool whole =
        quad.count == 4 && corner_depth[0] >= 0.0f && corner_depth[1] >= 0.0f && corner_depth[2] >= 0.0f && corner_depth[3] >= 0.0f;

    // The grid covers the visible part's extent in (u,v), so that its cells tile it: cells
    // wholly behind the near plane are skipped and the few it cuts are clipped.
    glm::vec2 low(0.0f);
    glm::vec2 high(1.0f);
    if (!visibleRegion(camera, quad.world, &low, &high)) return;
    const QuadGrid cuts = perspectiveGrid(camera, quad.world, low, high, kQuadPixelTolerance);

    if (whole && cuts.columns == 1 && cuts.rows == 1) {
        ImVec2 corners[5];
        screenCorners(quad, corners);
        list->AddImageQuad(texture, corners[0], corners[1], corners[2], corners[3], uv[0], uv[1], uv[2], uv[3], colour);
        return;
    }

    // Kept between calls, as the grid can be thousands of vertices: too many for the stack, and
    // reused rather than allocated for every quad. One per thread, since the overlay and
    // GoodRender's moments may draw on different ones.
    thread_local std::vector<GridVertex> grid;
    const int stride = cuts.columns + 1;
    grid.resize(static_cast<size_t>(stride) * static_cast<size_t>(cuts.rows + 1));

    for (int row = 0; row <= cuts.rows; ++row) {
        for (int column = 0; column <= cuts.columns; ++column) {
            grid[row * stride + column] = quadVertex(camera, quad, uv, glm::vec2(cuts.u[column], cuts.v[row]));
        }
    }

    // One draw command for the whole grid: AddImageQuad per cell would push and pop the
    // texture id each time, and that is a command each. A clipped cell can come out a
    // pentagon, so a quad the plane cuts reserves the worst case and hands the rest back;
    // one it does not is exact, which is the common case by a long way.
    const int cells = cuts.columns * cuts.rows;
    const int reserved_vertices = cells * (whole ? 4 : 5);
    const int reserved_indices = cells * (whole ? 6 : 9);
    int used_vertices = 0;
    int used_indices = 0;

    list->PushTextureID(texture);
    list->PrimReserve(reserved_indices, reserved_vertices);

    for (int row = 0; row < cuts.rows; ++row) {
        for (int column = 0; column < cuts.columns; ++column) {
            const int corner[4] = {
                row * stride + column,
                row * stride + column + 1,
                (row + 1) * stride + column + 1,
                (row + 1) * stride + column};

            int behind = 0;
            for (int i = 0; i < 4; ++i) behind += grid[corner[i]].depth < 0.0f ? 1 : 0;
            if (behind == 4) continue;

            if (behind == 0) {
                const GridVertex& a = grid[corner[0]];
                const GridVertex& b = grid[corner[1]];
                const GridVertex& c = grid[corner[2]];
                const GridVertex& d = grid[corner[3]];
                list->PrimQuadUV(a.position, b.position, c.position, d.position, a.coord, b.coord, c.coord, d.coord, colour);
                used_vertices += 4;
                used_indices += 6;
                continue;
            }

            glm::vec2 clipped[5];
            const glm::vec2 cell_low(cuts.u[column], cuts.v[row]);
            const glm::vec2 cell_high(cuts.u[column + 1], cuts.v[row + 1]);
            const int clipped_count = clipQuadRegion(corner_depth, cell_low, cell_high, clipped);
            if (clipped_count < 3) continue;

            // A fan from the first corner: the cell is convex, and one plane cannot make it
            // otherwise.
            const unsigned int base = list->_VtxCurrentIdx;
            for (int i = 0; i < clipped_count; ++i) {
                const GridVertex vertex = quadVertex(camera, quad, uv, clipped[i]);
                list->PrimWriteVtx(vertex.position, vertex.coord, colour);
            }
            for (int i = 2; i < clipped_count; ++i) {
                list->PrimWriteIdx(static_cast<ImDrawIdx>(base));
                list->PrimWriteIdx(static_cast<ImDrawIdx>(base + i - 1));
                list->PrimWriteIdx(static_cast<ImDrawIdx>(base + i));
            }
            used_vertices += clipped_count;
            used_indices += (clipped_count - 2) * 3;
        }
    }

    list->PrimUnreserve(reserved_indices - used_indices, reserved_vertices - used_vertices);
    list->PopTextureID();
}

void drawScene(ImDrawList* list, const Camera& camera, const Scene& scene, bool occluded, bool over) {
    for (const SceneItem& item : scene.items) {
        if (!item.texture || !(item.occlusion != Occlusion::None ? occluded : over)) continue;
        glm::vec3 corners[4];
        const ImVec2* uv = nullptr;
        if (!itemCorners(camera, item, corners, &uv)) continue;
        if (const std::optional<ClippedQuad> quad = clipAndProject(camera, corners)) {
            drawTexturedQuad(list, camera, item.texture, *quad, uv, item.colour);
        }
    }
}

} // namespace viewer::overlay
