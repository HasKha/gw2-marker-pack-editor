#include "viewer/ribbon.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

#include "viewer/bake.h"

namespace viewer {

void appendRibbon(const std::vector<glm::vec3>& points, float half_width, bool is_wall, float scroll, std::vector<RibbonQuad>* out) {
    if (points.size() < 2) return;

    float length = 0.0f;
    for (size_t i = 0; i + 1 < points.size(); ++i) length += glm::distance(points[i], points[i + 1]);
    if (length <= 0.0f) return;

    const float texture_length = kTrailHalfWidth * 2.0f;
    // Blish's Vector3.Forward, (0,0,-1) in its Z-up space, is down in MumbleLink's.
    const glm::vec3 down(0.0f, -1.0f, 0.0f);

    // Pathing's crosses are taken in Blish's space, which is MumbleLink's with Y and Z
    // swapped. A swap is a reflection, and a reflection negates a cross product: so the flat
    // offset, one cross, comes out negated here, and the wall's, a cross of a cross, does not.
    const auto offsetAlong = [&](const glm::vec3& direction) {
        return is_wall ? glm::cross(direction, glm::cross(direction, down)) : -glm::cross(direction, down);
    };

    glm::vec3 last_offset(0.0f);
    float flip = 1.0f;
    glm::vec3 shift(0.0f); // the offset in metres, as last set; a degenerate segment keeps it
    float remaining = length; // Pathing's pastDistance: v counts down the path

    glm::vec3 previous_left(0.0f);
    glm::vec3 previous_right(0.0f);
    float previous_v = 0.0f;

    for (size_t i = 0; i < points.size(); ++i) {
        // Each point takes its offset from the segment that leaves it; the last keeps the one
        // before. A segment the offset is undefined for — vertical on a flat trail, or of no
        // length — keeps the previous one too, where Pathing would normalize a zero vector.
        if (i + 1 < points.size()) {
            glm::vec3 offset = offsetAlong(points[i + 1] - points[i]);
            const float offset_length = glm::length(offset);
            if (offset_length > 1e-6f) {
                offset /= offset_length;
                if (last_offset != glm::vec3(0.0f) && glm::dot(offset, last_offset) < 0.0f) flip = -flip;
                shift = offset * half_width * flip;
                last_offset = offset;
            }
        }

        const glm::vec3 left = points[i] + shift;
        const glm::vec3 right = points[i] - shift;
        const float v = remaining / texture_length - 1.0f + scroll;

        if (i > 0) {
            // Whole texture lengths off both ends: the same image, and v stays small enough
            // that float precision across a long trail is not spent on the offset.
            const float base = std::floor(std::min(previous_v, v));
            RibbonQuad quad;
            quad.corners[0] = previous_left;
            quad.corners[1] = previous_right;
            quad.corners[2] = right;
            quad.corners[3] = left;
            quad.uv[0] = ImVec2(0.0f, previous_v - base);
            quad.uv[1] = ImVec2(1.0f, previous_v - base);
            quad.uv[2] = ImVec2(1.0f, v - base);
            quad.uv[3] = ImVec2(0.0f, v - base);
            out->push_back(quad);
        }

        if (i + 1 < points.size()) remaining -= glm::distance(points[i], points[i + 1]);
        previous_left = left;
        previous_right = right;
        previous_v = v;
    }
}

void cutStep(const RibbonQuad& quad, float most_length, std::vector<RibbonQuad>* out) {
    const glm::vec3 start = (quad.corners[0] + quad.corners[1]) * 0.5f;
    const glm::vec3 end = (quad.corners[2] + quad.corners[3]) * 0.5f;
    const int count = most_length > 0.0f ? std::max(1, static_cast<int>(std::ceil(glm::distance(start, end) / most_length))) : 1;
    if (count == 1) {
        out->push_back(quad);
        return;
    }
    // v runs the same along both long edges, corners 0→3 and 1→2, so a cut at t is a cut
    // straight across.
    const float v_start = quad.uv[0].y;
    const float v_end = quad.uv[3].y;
    for (int i = 0; i < count; ++i) {
        const float from = static_cast<float>(i) / static_cast<float>(count);
        const float until = static_cast<float>(i + 1) / static_cast<float>(count);
        const float v_from = v_start + (v_end - v_start) * from;
        const float v_until = v_start + (v_end - v_start) * until;
        const float base = std::floor(std::min(v_from, v_until));

        RibbonQuad piece;
        piece.corners[0] = glm::mix(quad.corners[0], quad.corners[3], from);
        piece.corners[1] = glm::mix(quad.corners[1], quad.corners[2], from);
        piece.corners[2] = glm::mix(quad.corners[1], quad.corners[2], until);
        piece.corners[3] = glm::mix(quad.corners[0], quad.corners[3], until);
        piece.uv[0] = ImVec2(quad.uv[0].x, v_from - base);
        piece.uv[1] = ImVec2(quad.uv[1].x, v_from - base);
        piece.uv[2] = ImVec2(quad.uv[2].x, v_until - base);
        piece.uv[3] = ImVec2(quad.uv[3].x, v_until - base);
        out->push_back(piece);
    }
}

} // namespace viewer
