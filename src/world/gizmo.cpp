#include "world/gizmo.h"

#include <imgui.h>

// Must follow imgui.h: ImGuizmo.h uses ImDrawList and ImU32 without declaring them.
#include <ImGuizmo.h>

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

// Same resolution the renderer does, so the gizmo sits exactly on the drawn icon.
bool markerTransform(const editor::State& state, size_t pathable_index, glm::vec3* out_position, glm::vec3* out_rotation,
                     bool* out_has_rotation, float* out_height_offset) {
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
    bool present = false;
    if (const std::string* text = state.pack.effectiveValue(pathable, "rotate")) {
        glm::vec3 degrees(0.0f);
        if (pack::parseVec3(*text, &degrees)) rotation = degrees * kDegreesToRadians;
        present = true;
    }
    static constexpr const char* kAxisKeys[3] = {"rotate-x", "rotate-y", "rotate-z"};
    for (int axis = 0; axis < 3; ++axis) {
        const std::string* text = state.pack.effectiveValue(pathable, kAxisKeys[axis]);
        if (!text) continue;
        float degrees = 0.0f;
        if (pack::parseFloat(*text, &degrees)) rotation[axis] = degrees * kDegreesToRadians;
        present = true;
    }

    *out_rotation = rotation;
    *out_has_rotation = present;
    return true;
}

void writePosition(editor::State& state, size_t pathable_index, const glm::vec3& drawn_position, float height_offset) {
    pack::AttributeCollection& attributes = state.pack.pathables[pathable_index].attributes;
    const glm::vec3 stored(drawn_position.x, drawn_position.y - height_offset, drawn_position.z);

    attributes.set("xpos", pack::formatFloat(stored.x));
    attributes.set("ypos", pack::formatFloat(stored.y));
    attributes.set("zpos", pack::formatFloat(stored.z));
    state.touch();
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

}  // namespace

bool draw(editor::State& state, const Camera& camera) {
    if (!state.loaded || !camera.valid) return false;
    if (state.settings.gizmo == editor::GizmoMode::Disabled) return false;
    if (state.selection.size() != 1) return false;

    const editor::NodeRef node = state.selection.primary();
    if (node.kind != editor::NodeKind::Pathable || node.index >= state.pack.pathables.size()) return false;

    glm::vec3 position(0.0f);
    glm::vec3 rotation(0.0f);
    bool has_rotation = false;
    float height_offset = 0.0f;
    if (!markerTransform(state, node.index, &position, &rotation, &has_rotation, &height_offset)) return false;

    ImGuizmo::BeginFrame();
    ImGuizmo::SetOrthographic(false);
    // The same list the markers go into, called after them, so the gizmo sits on top of
    // the world but still below our own windows.
    ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
    ImGuizmo::SetRect(0.0f, 0.0f, camera.viewport.x, camera.viewport.y);

    glm::mat4 model = glm::translate(glm::mat4(1.0f), position) * rotationFromAttributes(rotation);

    const ImGuizmo::OPERATION operation =
        state.settings.gizmo == editor::GizmoMode::Rotate ? ImGuizmo::ROTATE : ImGuizmo::TRANSLATE;

    const bool changed = ImGuizmo::Manipulate(glm::value_ptr(camera.view), glm::value_ptr(camera.projection), operation,
                                              ImGuizmo::WORLD, glm::value_ptr(model));

    if (changed) {
        if (state.settings.gizmo == editor::GizmoMode::Translate) {
            writePosition(state, node.index, glm::vec3(model[3]), height_offset);
        } else {
            writeRotation(state, node.index, attributesFromRotation(model));
        }
    }

    return ImGuizmo::IsOver() || ImGuizmo::IsUsing();
}

}  // namespace world::gizmo
