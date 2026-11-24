#include "viewer/ui/import_window.h"

#include <IconsFontAwesome7.h>
#include <imgui.h>

#include <algorithm>

#include "util/path_text.h"
#include "viewer/ui/fonts.h"
#include "viewer/ui/style.h"

namespace viewer::ui::import_window {

Answer draw(const blish_import::Found& found, Choices* choices, const std::string& status, bool busy, bool* open) {
    Answer answer = Answer::None;
    if (!*open) return answer;
    choices->packs.resize(found.packs.size(), 1);

    // No close button while importing, as Close is locked then: the import would go on unseen.
    if (ImGui::Begin("Import from Blish HUD", busy ? nullptr : open, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (found.packs.empty()) {
            ImGui::TextWrapped("No marker packs found in Blish HUD.");
        } else {
            ImGui::TextWrapped("Do you wish to import your marker packs from Blish HUD?");
            const float height =
                ImGui::GetTextLineHeightWithSpacing() * std::min<float>(8.0f, static_cast<float>(found.packs.size()) + 0.5f);
            if (ImGui::BeginChild("##packs", ImVec2(ImGui::GetFontSize() * 24.0f, height), true)) {
                for (size_t i = 0; i < found.packs.size(); ++i) {
                    bool chosen = choices->packs[i] != 0;
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::Checkbox(util::pathText(found.packs[i].stem()).c_str(), &chosen)) choices->packs[i] = chosen ? 1 : 0;
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
        }

        if (!status.empty()) ImGui::TextWrapped("%s", status.c_str());
        ImGui::TextColored(style::muted(), "You can open this window anytime from the addon options.");

        // Right-aligned, both as wide as the wider, so the pair keeps its place as Import's label
        // changes. The window sizes itself to its contents, so its width is last frame's.
        const std::string import = fonts::label(ICON_FA_FILE_IMPORT, busy ? "Importing..." : "Import");
        const std::string close = fonts::label(ICON_FA_XMARK, "Close");
        const float width = style::buttonWidth({fonts::label(ICON_FA_FILE_IMPORT, "Importing...").c_str(), close.c_str()});
        const float buttons = width * 2.0f + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - buttons));
        const bool any_chosen = std::find(choices->packs.begin(), choices->packs.end(), 1) != choices->packs.end();
        if (style::button(import.c_str(), !busy && any_chosen, width)) answer = Answer::Import;
        if (!busy && !any_chosen && ImGui::IsItemHovered()) ImGui::SetTooltip("Tick a pack to import first.");
        ImGui::SameLine();
        if (style::button(close.c_str(), !busy, width)) {
            answer = Answer::Close;
            *open = false;
        }
    }
    ImGui::End();
    return answer;
}

} // namespace viewer::ui::import_window
