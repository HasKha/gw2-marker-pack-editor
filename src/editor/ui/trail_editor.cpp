#include "editor/ui/trail_editor.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "editor/trail_edit.h"

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

const pack::Pathable* selectedTrail(const editor::State& state) {
    const editor::NodeRef node = state.selection.primary();
    if (node.kind != editor::NodeKind::Pathable || node.index >= state.pack.pathables.size()) return nullptr;

    const pack::Pathable& pathable = state.pack.pathables[node.index];
    return pathable.type == pack::PathableType::Trail ? &pathable : nullptr;
}

// Same modifiers as the tree, over a flat list: the range is just an index span, so it
// needs none of the tree's level bookkeeping.
void selectPoint(editor::State& state, size_t index) {
    const ImGuiIO& io = ImGui::GetIO();

    editor::applyClick(state.points.indices, index, editor::clickMods(io.KeyCtrl, io.KeyShift), [](size_t anchor, size_t to) {
        std::vector<size_t> range;
        // Anchor first whichever way the range runs, so it stays the gizmo's anchor.
        if (anchor <= to) {
            for (size_t i = anchor; i <= to; ++i) range.push_back(i);
        } else {
            for (size_t i = anchor + 1; i-- > to;) range.push_back(i);
        }
        return range;
    });
}

void drawHeader(editor::State& state, const viewer::Camera& camera) {
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
        ImGui::SetTooltip(camera.valid ? "Current map: %u" : "The game has not reported a map yet", camera.map_id);
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
            if (ImGui::Selectable(label, state.points.indices.contains(index))) selectPoint(state, index);
            ImGui::PopID();
        }
    }
}

// Adds a point where the character is standing: after the selected one, or at the end of
// the trail when nothing is selected. Walking the route and dropping points is how these
// are authored, and a point can be dragged afterwards like any other.
void drawAddPoint(editor::State& state, const viewer::Camera& camera, size_t selected) {
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
    if (added != pack::kNoIndex) state.points.indices.set(added);
}

void drawSelectedPoint(editor::State& state, const viewer::Camera& camera) {
    // The last point clicked, which is the one the user is looking at. No bounds check:
    // trail_edit prunes the selection whenever the trail changes shape.
    const size_t index = state.points.indices.primary();
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

    const std::vector<size_t>& selected = state.points.indices.items();

    drawAddPoint(state, camera, index);
    ImGui::SameLine();
    // erasePoints drops the point selection itself — the indices it held all move. The
    // trail stays selected either way, so the editor stays open on it.
    if (ImGui::Button("Delete selected")) trail_edit::erasePoints(state, selected);
    ImGui::SameLine();
    ImGui::TextDisabled("%zu selected", selected.size());
}

void drawCirclePopup(editor::State& state, const viewer::Camera& camera) {
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

Request drawMenu(editor::State& state, const viewer::Camera& camera) {
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

void draw(editor::State& state, const viewer::Camera& camera) {
    if (!state.loaded) {
        ImGui::TextDisabled("No pack open.");
        return;
    }

    // Not followSelection: that runs from marker_editor every frame, because this body does
    // not when the window is collapsed.
    if (!trail_edit::isOpen()) {
        // trail_edit closes rather than keeping the last trail up when a .trl cannot be
        // read, so a trail selected with nothing open is one whose data is missing. Saying
        // "select a trail" there would make a broken traildata look like an empty trail.
        if (const pack::Pathable* trail = selectedTrail(state)) {
            const std::string* data = state.pack.effectiveValue(*trail, "traildata");
            ImGui::TextDisabled("%s", data ? data->c_str() : "(no traildata)");
            ImGui::TextWrapped(
                "This trail's data file is not in the pack, or could not be read. Its points cannot be edited until "
                "traildata points at a file the pack has.");
            return;
        }

        ImGui::TextDisabled("Select a trail to edit it.");
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
