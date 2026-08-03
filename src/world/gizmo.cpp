#include "world/gizmo.h"

#include <imgui.h>

// Must follow imgui.h: ImGuizmo.h uses ImDrawList and ImU32 without declaring them.
#include <ImGuizmo.h>

#include <vector>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/euler_angles.hpp>

namespace world::gizmo {

namespace {

constexpr float kRadiansToDegrees = 180.0f / 3.14159265358979323846f;
constexpr float kDegreesToRadians = 3.14159265358979323846f / 180.0f;

// Marker rotation is authored in Blish's Z-up space; the file's positions are Y-up. The
// two differ by swapping Y and Z, which is also what flips handedness between them. The
// matrix is its own inverse, so the same one converts either way.
//
// See the rotation notes in docs/format.md.
const glm::mat4& swapYZ() {
    static const glm::mat4 swap = [] {
        glm::mat4 m(1.0f);
        m[1] = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
        m[2] = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
        return m;
    }();
    return swap;
}

// Rendering applies X, then Y, then Z, which in column-vector form is Rz * Ry * Rx.
glm::mat4 rotationFromAttributes(const glm::vec3& radians) {
    return swapYZ() * glm::eulerAngleZYX(radians.z, radians.y, radians.x) * swapYZ();
}

glm::vec3 attributesFromRotation(const glm::mat4& world_rotation) {
    const glm::mat4 blish = swapYZ() * world_rotation * swapYZ();
    glm::vec3 radians(0.0f);
    glm::extractEulerAngleZYX(blish, radians.z, radians.y, radians.x);
    return radians;
}

// A POI the gizmo moves. The first one collected is the anchor: the handles sit on it, and
// a group rotation orbits the rest about it.
struct Target {
    size_t pathable = pack::kNoIndex;
    glm::vec3 position{0.0f}; // as drawn, i.e. including heightOffset
    glm::vec3 rotation{0.0f}; // radians, in the attributes' own axis order
    float height_offset = 0.0f;
};

// Same resolution the renderer does, so the gizmo sits exactly on the drawn icon.
bool markerTransform(
    const editor::State& state,
    size_t pathable_index,
    glm::vec3* out_position,
    glm::vec3* out_rotation,
    float* out_height_offset) {
    const pack::Pathable& pathable = state.pack.pathables[pathable_index];
    if (pathable.type != pack::PathableType::Poi) return false;

    const std::string* x = pathable.attributes.value("xpos");
    const std::string* y = pathable.attributes.value("ypos");
    const std::string* z = pathable.attributes.value("zpos");
    if (!x || !y || !z) return false;

    glm::vec3 position(0.0f);
    if (!pack::parseFloat(*x, &position.x)) return false;
    if (!pack::parseFloat(*y, &position.y)) return false;
    if (!pack::parseFloat(*z, &position.z)) return false;

    *out_height_offset = state.pack.effectiveFloat(pathable, "heightoffset");
    // The gizmo belongs where the marker is drawn, not at its bare coordinate.
    position.y += *out_height_offset;
    *out_position = position;

    glm::vec3 rotation(0.0f);
    if (const std::string* text = state.pack.effectiveValue(pathable, "rotate")) {
        glm::vec3 degrees(0.0f);
        if (pack::parseVec3(*text, &degrees)) rotation = degrees * kDegreesToRadians;
    }
    static constexpr const char* kAxisKeys[3] = {"rotate-x", "rotate-y", "rotate-z"};
    for (int axis = 0; axis < 3; ++axis) {
        const std::string* text = state.pack.effectiveValue(pathable, kAxisKeys[axis]);
        if (!text) continue;
        float degrees = 0.0f;
        if (pack::parseFloat(*text, &degrees)) rotation[axis] = degrees * kDegreesToRadians;
    }

    *out_rotation = rotation;
    return true;
}

// In selection order, so targets.front() is the first thing the user selected. Anything
// without a transform — categories, trails, a POI missing a coordinate — is skipped rather
// than blocking the gizmo, so a mixed selection still moves what it can.
std::vector<Target> collectTargets(const editor::State& state) {
    std::vector<Target> targets;
    for (const editor::NodeRef& node : state.selection.items()) {
        if (node.kind != editor::NodeKind::Pathable || node.index >= state.pack.pathables.size()) continue;

        Target target;
        if (!markerTransform(state, node.index, &target.position, &target.rotation, &target.height_offset)) continue;
        target.pathable = node.index;
        targets.push_back(target);
    }
    return targets;
}

void writePosition(editor::State& state, size_t pathable_index, const glm::vec3& drawn_position, float height_offset) {
    pack::AttributeCollection& attributes = state.pack.pathables[pathable_index].attributes;
    const glm::vec3 stored(drawn_position.x, drawn_position.y - height_offset, drawn_position.z);

    attributes.set("xpos", pack::formatFloat(stored.x));
    attributes.set("ypos", pack::formatFloat(stored.y));
    attributes.set("zpos", pack::formatFloat(stored.z));
    state.touch();
}

// Applied once, on first use. Defaults are tuned for a desktop editor viewport; over a
// game scene the thinner lines are hard to pick out.
void applyStyleOnce() {
    static bool done = false;
    if (done) return;
    done = true;

    ImGuizmo::Style& style = ImGuizmo::GetStyle();
    style.TranslationLineThickness = 5.0f; // default 3
    style.TranslationLineArrowSize = 9.0f; // default 6
    style.RotationLineThickness = 7.0f; // default 2
    style.RotationOuterLineThickness = 5.0f;
    style.CenterCircleSize = 3.0f;

    // ImGuizmo flips an axis towards the camera "for better visibility", which makes the
    // handles jump as you orbit. A marker's axes mean something fixed, so keep them put.
    ImGuizmo::AllowAxisFlip(false);
}

void writeRotation(editor::State& state, size_t pathable_index, const glm::vec3& radians) {
    pack::AttributeCollection& attributes = state.pack.pathables[pathable_index].attributes;
    const glm::vec3 degrees = radians * kRadiansToDegrees;

    // Collapse to the single `rotate` form. Keeping three separate per-axis attributes in
    // sync through a drag would leave whichever ones the author did not write behind.
    attributes.erase("rotate-x");
    attributes.erase("rotate-y");
    attributes.erase("rotate-z");
    attributes.set("rotate", pack::formatVec3(degrees));
    state.touch();
}

} // namespace

bool draw(editor::State& state, const Camera& camera) {
    if (!state.loaded || !camera.valid) return false;
    if (state.settings.gizmo == editor::GizmoMode::Disabled) return false;

    const std::vector<Target> targets = collectTargets(state);
    if (targets.empty()) return false;
    const Target& anchor = targets.front();

    applyStyleOnce();
    ImGuizmo::BeginFrame();
    ImGuizmo::SetOrthographic(false);
    // The same list the markers go into, called after them, so the gizmo sits on top of
    // the world but still below our own windows.
    ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
    ImGuizmo::SetRect(0.0f, 0.0f, camera.viewport.x, camera.viewport.y);

    glm::mat4 model = glm::translate(glm::mat4(1.0f), anchor.position) * rotationFromAttributes(anchor.rotation);

    const bool translating =
        state.settings.gizmo == editor::GizmoMode::Translate || state.settings.gizmo == editor::GizmoMode::TranslateRotate;
    const bool rotating = state.settings.gizmo == editor::GizmoMode::Rotate || state.settings.gizmo == editor::GizmoMode::TranslateRotate;

    // Per-axis rings only. ImGuizmo::ROTATE also includes ROTATE_SCREEN, and the presence
    // of that handle is what makes it halve every ring (circleMul in DrawRotationGizmo),
    // leaving semicircles whose cut swings around as the camera moves. Dropping it gives
    // full circles, at the cost of the outer screen-rotation ring — which does not map to
    // anything in the marker format anyway.
    ImGuizmo::OPERATION operation = static_cast<ImGuizmo::OPERATION>(0);
    if (translating) operation = operation | ImGuizmo::TRANSLATE;
    if (rotating) operation = operation | ImGuizmo::ROTATE_X | ImGuizmo::ROTATE_Y | ImGuizmo::ROTATE_Z;

    // The model matrix already carries the marker's orientation; LOCAL is what makes
    // ImGuizmo draw its rings in that orientation rather than world-aligned. Pure
    // translation stays in WORLD, because dragging a marker along the world axes — Y in
    // particular — is more predictable than along a tilted quad's own axes.
    const ImGuizmo::MODE mode = rotating ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

    const bool changed =
        ImGuizmo::Manipulate(glm::value_ptr(camera.view), glm::value_ptr(camera.projection), operation, mode, glm::value_ptr(model));

    if (changed) {
        // Write only what actually moved. In the combined mode a pure drag would otherwise
        // also stamp rotate="0,0,0" onto a plain billboard — and the mere presence of that
        // attribute switches the marker to a fixed orientation. See docs/format.md.
        constexpr float kAngleEpsilon = 1e-4f; // radians, ~0.006 degrees
        const glm::vec3 new_position(model[3]);
        const glm::vec3 new_rotation = attributesFromRotation(model);

        const bool moved = translating && glm::distance(new_position, anchor.position) > 1e-5f;
        const bool rotated = rotating && glm::any(glm::greaterThan(glm::abs(new_rotation - anchor.rotation), glm::vec3(kAngleEpsilon)));

        // Everything the drag did, expressed about the anchor: p' = anchor' + R * (p -
        // anchor), and likewise R * orientation. One formula covers both operations and
        // the anchor itself — a pure drag has R = identity, and under a pure rotation the
        // anchor's own offset is zero, so it stays put and only turns.
        //
        // Both rotations are products of a permutation and a Euler rotation, so they are
        // orthogonal and the transpose is the inverse.
        const glm::mat4 anchor_rotation = rotationFromAttributes(anchor.rotation);
        const glm::mat4 delta_rotation = rotated ? glm::mat4(glm::mat3(model)) * glm::transpose(anchor_rotation) : glm::mat4(1.0f);
        const glm::vec3 anchor_position = moved ? new_position : anchor.position;

        for (const Target& target : targets) {
            if (rotated) {
                writeRotation(state, target.pathable, attributesFromRotation(delta_rotation * rotationFromAttributes(target.rotation)));
            }

            const glm::vec3 offset = target.position - anchor.position;
            const glm::vec3 position = anchor_position + glm::vec3(delta_rotation * glm::vec4(offset, 0.0f));
            if (glm::distance(position, target.position) > 1e-5f) {
                writePosition(state, target.pathable, position, target.height_offset);
            }
        }
    }

    return ImGuizmo::IsOver() || ImGuizmo::IsUsing();
}

} // namespace world::gizmo
