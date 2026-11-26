#pragma once

#include <imgui.h>

#include <glm/vec3.hpp>

#include <vector>

#include "pack/trl.h"

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
// Each quad's v is shifted by a whole number of texture lengths to keep it near zero, and spans
// as many lengths as its step is long, so **the sampler must wrap**: Nexus's ImGui and
// GoodRender's draw_list and world_quad do.
//
// `scroll` is added to every v: animspeed times seconds, as trail.hlsl adds it.
void appendRibbon(const std::vector<glm::vec3>& points, float half_width, bool is_wall, float scroll, std::vector<RibbonQuad>* out);

// Appends `quad` cut across into equal pieces no longer than `most_length` metres: the same
// strip and the same image, each piece's v shifted near zero as appendRibbon's are. For what
// changes along a step that a quad's one colour cannot show: its distance fade.
void cutStep(const RibbonQuad& quad, float most_length, std::vector<RibbonQuad>* out);

// A trail's whole ribbon, built once at no scroll, and bounded a stretch at a time, so that a
// frame passes over what is past the trail's fade or out of view without looking at each step.
// Only the scroll moves from frame to frame (scrolled); the points, the width and `wall` do not.
struct TrailRibbon {
    // Consecutive quads of one segment, and the box round their corners.
    struct Stretch {
        size_t first = 0;
        size_t count = 0;
        glm::vec3 min{0.0f};
        glm::vec3 max{0.0f};
    };

    std::vector<RibbonQuad> quads; // appendRibbon's at scroll 0, segment after segment
    std::vector<Stretch> stretches;
    glm::vec3 min{0.0f}; // round every quad; meaningless with none
    glm::vec3 max{0.0f};
};

// appendRibbon over each of `segments`' runs of points, bounded in stretches.
TrailRibbon buildRibbon(const std::vector<pack::trl::Segment>& segments, float half_width, bool is_wall);

// A quad of a ribbon built at no scroll, as appendRibbon builds it at `scroll`: the same v, less
// whole texture lengths.
RibbonQuad scrolled(const RibbonQuad& quad, float scroll);

} // namespace viewer
