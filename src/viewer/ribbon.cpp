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

} // namespace viewer
