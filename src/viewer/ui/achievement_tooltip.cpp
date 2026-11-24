#include "viewer/ui/achievement_tooltip.h"

#include <imgui.h>

#include "viewer/ui/style.h"

namespace viewer::ui::achievement_tooltip {

void draw(const achievements::Details& details, int bit) {
    // Near the 300 Blish pixels its labels wrap at, in units of the font so it follows its size.
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetFontSize() * 20.0f);
    ImGui::Text("%s", details.name.c_str());
    if (!details.description.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, style::ACHIEVEMENT_DESCRIPTION);
        ImGui::Text("%s", details.description.c_str());
        ImGui::PopStyleColor();
    }
    if (const std::string& requirement = details.requirementFor(bit); !requirement.empty()) {
        ImGui::Spacing();
        ImGui::Text("%s", requirement.c_str());
    }
    ImGui::PopTextWrapPos();
}

} // namespace viewer::ui::achievement_tooltip
