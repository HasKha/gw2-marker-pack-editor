#include "ui/trail_editor.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "trail_edit.h"

namespace ui::trail_editor {

namespace {

// Kept between invocations on purpose: the whole point of "add circle" is to place several
// of the same circle, so only the centre is refreshed each time the dialog opens.
struct CircleParams {
    glm::vec3 centre{0.0f};
    float radius = 4.0f;
    int segments = 72; // 5 degrees a step, what make_circle uses
    float angle_start = 0.0f;
    float angle_end = 360.0f;
};

CircleParams circle;
bool open_circle_popup = false;

editor::NodeRef pointRef(size_t index) {
    return editor::NodeRef{editor::NodeKind::TrailPoint, trail_edit::pathable(), index};
}

// The trail point driving the drag fields, or kNoIndex when the selection is something
// else. The primary is the last thing clicked, which is the one the user is looking at.
size_t selectedPoint(const editor::State& state) {
    const editor::NodeRef primary = state.selection.primary();
    if (primary.kind != editor::NodeKind::TrailPoint) return pack::kNoIndex;
    if (primary.index != trail_edit::pathable() || primary.sub >= trail_edit::pointCount()) return pack::kNoIndex;
    return primary.sub;
}

std::vector<size_t> selectedPoints(const editor::State& state) {
    std::vector<size_t> points;
    for (const editor::NodeRef& node : state.selection.items()) {
        if (node.kind != editor::NodeKind::TrailPoint || node.index != trail_edit::pathable()) continue;
        if (node.sub < trail_edit::pointCount()) points.push_back(node.sub);
    }
    return points;
}

// Same modifiers as the tree, over a flat list: the range is just an index span, so it
// needs none of the tree's level bookkeeping.
void selectPoint(editor::State& state, size_t index) {
    const ImGuiIO& io = ImGui::GetIO();
    const editor::NodeRef anchor = state.selection.anchor();

    if (io.KeyShift && anchor.kind == editor::NodeKind::TrailPoint && anchor.index == trail_edit::pathable() &&
        anchor.sub < trail_edit::pointCount()) {
        std::vector<editor::NodeRef> range;
        if (anchor.sub <= index) {
            for (size_t i = anchor.sub; i <= index; ++i) range.push_back(pointRef(i));
        } else {
            // Anchor first whichever way the range runs, so it stays the gizmo's anchor.
            for (size_t i = anchor.sub + 1; i-- > index;) range.push_back(pointRef(i));
        }
        state.selection.setAll(std::move(range));
        return;
    }

    if (io.KeyCtrl) {
        state.selection.toggle(pointRef(index));
    } else {
        state.selection.set(pointRef(index));
    }
}

// Keeps the open trail in step with the selection, both ways: a trail selected is a trail
// opened, and selecting anything else closes it. Without the second half the handles in the
// world would outlive the reason they were drawn.
//
// Called every frame: reading the .trl is budgeted, so the first attempt after picking a
// trail can legitimately come back empty and want another go.
void followSelection(editor::State& state) {
    const editor::NodeRef primary = state.selection.primary();

    // A point of the trail already open. Editing its points is editing it.
    if (primary.kind == editor::NodeKind::TrailPoint && primary.index == trail_edit::pathable()) return;

    if (primary.kind == editor::NodeKind::Pathable && primary.index < state.pack.pathables.size() &&
        state.pack.pathables[primary.index].type == pack::PathableType::Trail) {
        trail_edit::open(state, primary.index);
        return;
    }

    trail_edit::close();
}

void drawHeader(editor::State& state, const world::Camera& camera) {
    int map_id = trail_edit::mapId();

    ImGui::PushItemWidth(120.0f);
    ImGui::InputInt("Map id", &map_id, 0);
    ImGui::PopItemWidth();
    // Committed when the field is left, not per keystroke: a half-typed id would otherwise
    // land in the file and mark the pack dirty on the way to the real one.
    if (ImGui::IsItemDeactivatedAfterEdit()) trail_edit::setMapId(state, map_id);

    ImGui::SameLine();
    if (ImGui::Button("Set to current map")) trail_edit::setMapId(state, static_cast<int32_t>(camera.map_id));
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(camera.valid ? "Current map: %u" : "No map: MumbleLink has no data yet", camera.map_id);
    }
}

void drawPointList(editor::State& state) {
    const size_t count = trail_edit::pointCount();
    const size_t segments = trail_edit::segmentCount();

    // A .trl can hold several runs of points, drawn as separate ribbons. When it does, each
    // row says which run it is in; when it does not, the column would be a constant.
    const bool show_segment = segments > 1;
    if (show_segment) {
        ImGui::TextDisabled("%zu points in %zu segments", count, segments);
    } else {
        ImGui::TextDisabled("%zu points", count);
    }

    // A trail can carry thousands of points — one pack ships 55k across its trails — so
    // only the rows actually on screen are emitted.
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(count));
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const size_t index = static_cast<size_t>(row);
            const glm::vec3 position = trail_edit::point(index);

            char segment[24] = "";
            if (show_segment) std::snprintf(segment, sizeof(segment), "s%zu ", trail_edit::segmentOf(index));

            char label[96];
            std::snprintf(label, sizeof(label), "%s%4zu   %8.2f, %8.2f, %8.2f", segment, index, position.x, position.y, position.z);

            ImGui::PushID(row);
            if (ImGui::Selectable(label, state.selection.contains(pointRef(index)))) selectPoint(state, index);
            ImGui::PopID();
        }
    }
}

// Adds a point where the character is standing: after the selected one, or at the end of
// the trail when nothing is selected. Walking the route and dropping points is how these
// are authored, and a point can be dragged afterwards like any other.
void drawAddPoint(editor::State& state, const world::Camera& camera, size_t selected) {
    if (!ImGui::Button("Add point")) {
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Adds a point at the character's position,\n"
                "after the selected one, or at the end.");
        }
        return;
    }

    if (!camera.valid) return;

    const size_t added = trail_edit::insertPoint(state, selected, camera.player);
    if (added != pack::kNoIndex) state.selection.set(pointRef(added));
}

void drawSelectedPoint(editor::State& state, const world::Camera& camera) {
    const size_t index = selectedPoint(state);
    if (index == pack::kNoIndex) {
        ImGui::TextDisabled("No point selected.");
        drawAddPoint(state, camera, pack::kNoIndex);
        return;
    }

    glm::vec3 position = trail_edit::point(index);
    bool changed = false;

    // Drag fields rather than sliders: a coordinate has no range to slide between, and
    // these are the same three numbers as a marker's xpos/ypos/zpos.
    ImGui::PushItemWidth(-60.0f);
    changed |= ImGui::DragFloat("xpos", &position.x, 0.05f);
    changed |= ImGui::DragFloat("ypos", &position.y, 0.05f);
    changed |= ImGui::DragFloat("zpos", &position.z, 0.05f);
    ImGui::PopItemWidth();

    if (changed) trail_edit::setPoint(state, index, position);

    const std::vector<size_t> selected = selectedPoints(state);

    drawAddPoint(state, camera, index);
    ImGui::SameLine();
    if (ImGui::Button("Delete selected")) {
        trail_edit::erasePoints(state, selected);
        // Every index after the first deletion has moved; keeping the old refs would point
        // at whatever slid into their place.
        state.selection.clear();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu selected", selected.size());
}

void drawCirclePopup(editor::State& state, const world::Camera& camera) {
    constexpr const char* kTitle = "Add circle";

    if (open_circle_popup) {
        ImGui::OpenPopup(kTitle);
        open_circle_popup = false;
    }

    if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    ImGui::PushItemWidth(220.0f);
    ImGui::DragFloat3("Centre", &circle.centre.x, 0.05f);
    if (ImGui::Button("Use character position")) circle.centre = camera.player;

    ImGui::DragFloat("Radius", &circle.radius, 0.05f, 0.0f, 0.0f, "%.3f");
    ImGui::InputInt("Segments", &circle.segments);
    ImGui::DragFloat("Angle start", &circle.angle_start, 1.0f, 0.0f, 0.0f, "%.1f deg");
    ImGui::DragFloat("Angle end", &circle.angle_end, 1.0f, 0.0f, 0.0f, "%.1f deg");
    ImGui::PopItemWidth();

    if (circle.segments < 1) circle.segments = 1;
    ImGui::TextDisabled("Appends %d points as a new segment.", circle.segments + 1);

    ImGui::Separator();
    if (ImGui::Button("Add", ImVec2(110, 0))) {
        trail_edit::addArc(state, circle.centre, circle.radius, circle.segments, circle.angle_start, circle.angle_end);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110, 0))) ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

} // namespace

Request drawMenu(editor::State& state, const world::Camera& camera) {
    Request request = Request::None;
    if (!ImGui::BeginMenu("Trail")) return request;

    const bool open = trail_edit::isOpen();

    if (ImGui::MenuItem("Reload from file", nullptr, false, open)) trail_edit::reload(state);
    if (ImGui::IsItemHovered() && open) ImGui::SetTooltip("Throws away the edits and rereads the .trl from the pack.");

    if (ImGui::MenuItem("Save to file...", nullptr, false, open)) request = Request::Export;
    if (ImGui::IsItemHovered() && open) ImGui::SetTooltip("Writes a copy outside the pack. Editing already saves with the pack.");

    ImGui::Separator();

    if (ImGui::MenuItem("Add circle...", nullptr, false, open)) {
        // Only the centre follows the character; the rest is whatever was used last.
        circle.centre = camera.player;
        open_circle_popup = true;
    }

    ImGui::EndMenu();
    return request;
}

void draw(editor::State& state, const world::Camera& camera) {
    if (!state.loaded) {
        ImGui::TextDisabled("No pack open.");
        return;
    }

    followSelection(state);

    if (!trail_edit::isOpen()) {
        ImGui::TextDisabled("Select a trail to edit it.");
        ImGui::TextWrapped("A trail's points live in its .trl file, which is loaded on demand - give it a frame after selecting.");
        return;
    }

    ImGui::TextDisabled("%s", trail_edit::path().c_str());
    drawHeader(state, camera);
    drawCirclePopup(state, camera);
    ImGui::Separator();

    // The point being edited is a fixed strip at the bottom, so the list above it does not
    // resize as the selection changes.
    constexpr float kFooterHeight = 150.0f;
    const float available = ImGui::GetContentRegionAvail().y;

    // No border: the separators above and below already say where the list starts and ends.
    ImGui::BeginChild("##points", ImVec2(0, std::max(80.0f, available - kFooterHeight)), false);
    drawPointList(state);
    ImGui::EndChild();

    ImGui::Separator();
    drawSelectedPoint(state, camera);
}

void exportTo(editor::State& state, const std::filesystem::path& file, std::string* status) {
    (void)state;

    std::string error;
    if (trail_edit::exportToFile(file, &error)) {
        if (status) *status = "Wrote " + file.filename().string();
    } else if (status) {
        *status = error.empty() ? "No trail open" : error;
    }
}

} // namespace ui::trail_editor
