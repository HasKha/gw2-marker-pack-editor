#include "viewer/ui/pack_list.h"

#include <imgui.h>

namespace viewer::ui::pack_list {

namespace {

const ImVec4 kErrorColour(1.0f, 0.4f, 0.4f, 1.0f);

// One row; true when it asked for something, `*action` saying what.
bool drawRow(const Row& row, size_t index, bool can_edit, Action* action) {
    bool acted = false;
    ImGui::PushID(static_cast<int>(index));

    ImGui::TextUnformatted(row.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", row.where.c_str());

    if (row.in_editor) {
        ImGui::TextDisabled("Open in the editor, drawn as it is edited");
    } else if (can_edit && row.error.empty()) {
        if (ImGui::SmallButton(row.personal_copy ? "Edit" : "Edit a copy")) {
            *action = Action{Action::Kind::Edit, index};
            acted = true;
        }
        if (ImGui::IsItemHovered() && !row.personal_copy) {
            ImGui::SetTooltip(
                "Copies the pack into your edits, where it stands in for this one,\n"
                "so an update of the original cannot overwrite your changes.");
        }
    }

    if (row.personal_copy && !row.in_editor) {
        if (can_edit && row.error.empty()) ImGui::SameLine();
        if (ImGui::SmallButton("Delete")) ImGui::OpenPopup("##delete");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Deletes your copy. The pack it was copied from comes back.");
        if (ImGui::BeginPopup("##delete")) {
            ImGui::Text("Delete your copy %s from disk?", row.name.c_str());
            if (ImGui::Button("Delete")) {
                *action = Action{Action::Kind::Delete, index};
                acted = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    if (!row.error.empty()) ImGui::TextColored(kErrorColour, "Did not load: %s", row.error.c_str());

    ImGui::PopID();
    return acted;
}

} // namespace

std::optional<Action> draw(const std::vector<Row>& rows, bool can_edit, const std::string& status, bool* open) {
    std::optional<Action> asked;
    if (!*open) return asked;

    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 28.0f, ImGui::GetFontSize() * 24.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Packs", open)) {
        if (!status.empty()) ImGui::TextWrapped("%s", status.c_str());
        ImGui::Separator();
        if (ImGui::BeginChild("##packs")) {
            for (size_t i = 0; i < rows.size(); ++i) {
                Action action;
                if (drawRow(rows[i], i, can_edit, &action)) asked = action;
                ImGui::Separator();
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
    return asked;
}

} // namespace viewer::ui::pack_list
