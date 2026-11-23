#include "viewer/ui/import_window.h"

#include <imgui.h>

#include <algorithm>

namespace viewer::ui::import_window {

namespace {

// imgui 1.80 predates BeginDisabled: a disabled button is drawn dim and its press ignored.
bool button(const char* label, bool enabled) {
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.4f);
    const bool pressed = ImGui::Button(label);
    if (!enabled) ImGui::PopStyleVar();
    return pressed && enabled;
}

} // namespace

Answer draw(const blish_import::Found& found, Choices* choices, const std::string& status, bool busy, bool* open) {
    Answer answer = Answer::None;
    if (!*open) return answer;
    choices->packs.resize(found.packs.size(), 1);

    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 26.0f, 0.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Import from Blish HUD", open, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (found.packs.empty()) {
            ImGui::TextWrapped("No marker packs found in Blish HUD.");
        } else {
            ImGui::TextWrapped("Import your marker packs from Blish HUD, with the categories you turned on or off.");
            const float height =
                ImGui::GetTextLineHeightWithSpacing() * std::min<float>(8.0f, static_cast<float>(found.packs.size()) + 0.5f);
            if (ImGui::BeginChild("##packs", ImVec2(ImGui::GetFontSize() * 24.0f, height), true)) {
                for (size_t i = 0; i < found.packs.size(); ++i) {
                    bool chosen = choices->packs[i] != 0;
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::Checkbox(found.packs[i].stem().string().c_str(), &chosen)) choices->packs[i] = chosen ? 1 : 0;
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
        }

        if (!status.empty()) ImGui::TextWrapped("%s", status.c_str());
        ImGui::TextDisabled("You can open this window anytime from the addon options.");

        // Right-aligned by the buttons' width last frame: the first frame they stand at the left.
        static float buttons_width = 0.0f;
        const float right = ImGui::GetWindowContentRegionMax().x;
        if (buttons_width > 0.0f) ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), right - buttons_width));
        const float left = ImGui::GetCursorScreenPos().x;
        const bool any_chosen = std::find(choices->packs.begin(), choices->packs.end(), 1) != choices->packs.end();
        if (button(busy ? "Importing..." : "Import", !busy && any_chosen)) answer = Answer::Import;
        ImGui::SameLine();
        if (button("Close", !busy)) {
            answer = Answer::Close;
            *open = false;
        }
        buttons_width = ImGui::GetItemRectMax().x - left;
    }
    ImGui::End();
    return answer;
}

} // namespace viewer::ui::import_window
