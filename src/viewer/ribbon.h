#pragma once

#include <imgui.h>

#include <glm/vec3.hpp>

#include <vector>

// The ribbon a trail is drawn as, built the way Pathing builds its vertex strip.
namespace viewer {

// One quad of a ribbon, between two consecutive points. The corners go round the quad —
// left and right at the first point, then right and left at the next — with uv[i] at
// corners[i].
struct RibbonQuad {
    glm::vec3 corners[4];
    ImVec2 uv[4];
};

// Appends the ribbon along one run of trail points: `half_width` metres either side of the
// path, lying flat or, for a wall, standing upright. Nothing for a run of fewer than two
// points or of no length.
//
// StandardTrail.World.cs BuildTrailSection is the reference, in Blish's own space; this is
// the same strip in MumbleLink's:
// - **Joined, not per segment.** A pair of vertices sits at every point, offset along the
//   segment that leaves it (the last point reuses the one before), and quad i joins pair i to
//   pair i+1, so the ribbon has no gaps or overlaps at a bend.
// - **u runs from the path's left (0) to its right (1)**, seen walking along it, and **v
//   counts down the path**, starting at its length over one texture length less one, so the
//   top of the texture points the way the trail goes. One texture length is
//   2 * kTrailHalfWidth, whatever the trail's scale.
// - **An offset that reverses flips**, as Pathing's flipOver does, so a hairpin does not
//   cross the ribbon over itself.
//
// Each quad's v is shifted by a whole number of texture lengths to keep it near zero. The
// image is the same wherever the sampler wraps; whether GoodRender's world_quad wraps at
// all is an open question in docs/plan.md.
//
// `scroll` is added to every v: animspeed times seconds, as trail.hlsl adds it.
void appendRibbon(const std::vector<glm::vec3>& points, float half_width, bool is_wall, float scroll, std::vector<RibbonQuad>* out);

} // namespace viewer
