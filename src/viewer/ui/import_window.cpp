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

// A box for one kind of thing Pathing kept, or a line saying there is none of it.
void option(const char* label, bool available, bool* value) {
    if (available) {
        ImGui::Checkbox(label, value);
    } else {
        ImGui::TextDisabled("%s: none in Blish HUD's folder", label);
        *value = false;
    }
}

} // namespace

Answer draw(const blish_import::Found& found, Choices* choices, const std::string& status, bool busy, bool* open) {
    Answer answer = Answer::None;
    if (!*open) return answer;
    choices->packs.resize(found.packs.size(), 1);

    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 30.0f, ImGui::GetFontSize() * 26.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Import from Blish HUD", open)) {
        ImGui::TextWrapped(
            "Copies Blish HUD's marker packs into this addon's folder, with what Pathing remembers about "
            "them. Blish HUD's folder is left as it is, and is not drawn from: both drawing it would show "
            "every marker twice.");
        ImGui::Separator();

        if (found.packs.empty()) {
            ImGui::TextDisabled("No packs in Blish HUD's folder.");
        } else {
            ImGui::Text("Packs");
            ImGui::SameLine();
            if (ImGui::SmallButton("All")) std::fill(choices->packs.begin(), choices->packs.end(), 1);
            ImGui::SameLine();
            if (ImGui::SmallButton("None")) std::fill(choices->packs.begin(), choices->packs.end(), 0);
            const float height =
                ImGui::GetTextLineHeightWithSpacing() * std::min<float>(8.0f, static_cast<float>(found.packs.size()) + 0.5f);
            if (ImGui::BeginChild("##packs", ImVec2(0.0f, height), true)) {
                for (size_t i = 0; i < found.packs.size(); ++i) {
                    bool chosen = choices->packs[i] != 0;
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::Checkbox(found.packs[i].filename().string().c_str(), &chosen)) choices->packs[i] = chosen ? 1 : 0;
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
        }

        option("Categories turned on or off", !found.categories.empty(), &choices->categories);
        option("What pack scripts stored", !found.storage.empty(), &choices->storage);
        option("Markers hidden until a time", !found.timers.empty(), &choices->timers);
        ImGui::Separator();

        if (!status.empty()) ImGui::TextWrapped("%s", status.c_str());
        if (button(busy ? "Importing..." : "Import", !busy)) answer = Answer::Import;
        ImGui::SameLine();
        if (button("Not now", !busy)) {
            answer = Answer::Later;
            *open = false;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("The options page has this again: Import from Blish HUD.");
    }
    ImGui::End();
    return answer;
}

} // namespace viewer::ui::import_window
