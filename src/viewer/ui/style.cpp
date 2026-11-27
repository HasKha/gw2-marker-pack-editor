#include "viewer/ui/style.h"

// For PushItemFlag and ImGuiItemFlags_Disabled: imgui 1.80 has no BeginDisabled.
#include <imgui_internal.h>

#include <algorithm>

#include "viewer/ui/fonts.h"

namespace viewer::ui::style {

namespace {

// A subsection's indent, in font heights: about a Font Awesome icon's width, so that what is under
// a subheading starts near where its words do. Not measured from a glyph, so it is the same before
// the icons are built and whichever icon heads it.
constexpr float kSubsectionIndentEms = 1.5f;

} // namespace

ImVec4 muted() {
    // Enough to tell apart from the text beside it, and still read on a bright background.
    constexpr float kAlpha = 0.65f;
    ImVec4 colour = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    colour.w *= kAlpha;
    return colour;
}

void beginDisabled(bool disabled) {
    constexpr float kDisabledAlpha = 0.5f;
    ImGui::PushItemFlag(ImGuiItemFlags_Disabled, disabled);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * (disabled ? kDisabledAlpha : 1.0f));
}

void endDisabled() {
    ImGui::PopStyleVar();
    ImGui::PopItemFlag();
}

bool button(const char* label, bool enabled, float width) {
    constexpr float kDisabledAlpha = 0.4f;
    if (!enabled) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * kDisabledAlpha);
    const bool pressed = ImGui::Button(label, ImVec2(width, 0.0f));
    if (!enabled) ImGui::PopStyleVar();
    return pressed && enabled;
}

float buttonWidth(std::initializer_list<const char*> labels) {
    float widest = 0.0f;
    for (const char* label : labels) widest = std::max(widest, ImGui::CalcTextSize(label, nullptr, true).x);
    return widest + ImGui::GetStyle().FramePadding.x * 2.0f;
}

void subheading(const char* icon, const char* text) {
    ImGui::Spacing();
    ImGui::TextColored(ACCENT, "%s", fonts::label(icon, text).c_str());
}

bool section(const char* id, const char* icon, const char* label, bool default_open) {
    const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | (default_open ? ImGuiTreeNodeFlags_DefaultOpen : 0);
    return ImGui::TreeNodeEx(id, flags, "%s", fonts::label(icon, label).c_str());
}

void beginSubsection(const char* icon, const char* text) {
    subheading(icon, text);
    ImGui::Indent(ImGui::GetFontSize() * kSubsectionIndentEms);
}

void endSubsection() {
    ImGui::Unindent(ImGui::GetFontSize() * kSubsectionIndentEms);
}

bool link(const char* text, const char* tooltip) {
    // Whether it is hovered before it is drawn, to draw it in the right colour.
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + ImGui::CalcTextSize(text).x, min.y + ImGui::GetTextLineHeight());
    const bool hovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(min, max);
    const ImVec4& colour = hovered ? LINK_HOVERED : LINK;
    ImGui::TextColored(colour, "%s", text);
    const ImVec2 drawn_min = ImGui::GetItemRectMin();
    const ImVec2 drawn_max = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(drawn_min.x, drawn_max.y), drawn_max, ImGui::GetColorU32(colour));
    if (!ImGui::IsItemHovered()) return false;
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImGui::SetTooltip("%s", tooltip);
    return ImGui::IsItemClicked();
}

} // namespace viewer::ui::style
