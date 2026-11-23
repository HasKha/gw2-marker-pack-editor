#include "viewer/ui/script_console.h"

#include <imgui.h>

#include <string>

#include "pack/path_text.h"

namespace viewer::ui::script_console {

namespace {

ImVec4 colourOf(script::Level level) {
    switch (level) {
    case script::Level::Warn: return ImVec4(1.0f, 0.8f, 0.3f, 1.0f);
    case script::Level::Error: return ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
    case script::Level::System: return ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
    default: return ImGui::GetStyleColorVec4(ImGuiCol_Text);
    }
}

void drawMessages(const script::Engine& engine) {
    static uint64_t seen_revision = 0;
    ImGui::BeginChild("##messages", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    for (const script::Message& message : engine.console().messages()) {
        ImGui::PushStyleColor(ImGuiCol_Text, colourOf(message.level));
        // A traceback follows the first line; it is there on hover rather than in the way.
        const size_t newline = message.text.find('\n');
        const std::string first_line = message.text.substr(0, newline);
        ImGui::TextUnformatted(("[" + message.source + "] " + first_line).c_str());
        ImGui::PopStyleColor();
        if (newline != std::string::npos && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", message.text.c_str());
    }
    // Follows new messages, unless the player has scrolled up to read old ones.
    if (seen_revision != engine.console().revision() && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) ImGui::SetScrollHereY(1.0f);
    seen_revision = engine.console().revision();
    ImGui::EndChild();
}

void drawWatches(const script::Engine& engine) {
    if (engine.watches().empty()) {
        ImGui::TextDisabled("Nothing watched. Debug:Watch(name, value) adds a line here.");
        return;
    }
    ImGui::Columns(2, "##watches");
    for (const auto& [key, value] : engine.watches()) {
        ImGui::TextUnformatted(key.c_str());
        ImGui::NextColumn();
        ImGui::TextUnformatted(value.c_str());
        ImGui::NextColumn();
    }
    ImGui::Columns(1);
}

void drawTimes(const script::Engine& engine, const runtime::Installed& installed) {
    for (size_t pack = 0; pack < installed.packs.size(); ++pack) {
        const double milliseconds = engine.frameSeconds(pack) * 1000.0;
        if (milliseconds <= 0.0) continue;
        ImGui::Text("%8.3f ms  %s", milliseconds, pack::pathText(installed.packs[pack]->loaded->path.filename()).c_str());
    }
}

void drawCensus(const script::Engine& engine) {
    if (engine.census().entries().empty()) {
        ImGui::TextDisabled("Every member scripts asked for exists.");
        return;
    }
    for (const script::CensusEntry& entry : engine.census().entries()) {
        ImGui::Text("%s  (%s:%d)", entry.member.c_str(), entry.source.c_str(), entry.line);
    }
}

} // namespace

void draw(const script::Engine& engine, const runtime::Installed& installed, bool* open) {
    if (!*open) return;
    ImGui::SetNextWindowSize(ImVec2(640.0f, 400.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Script console", open)) {
        const size_t census = engine.census().entries().size();
        if (ImGui::BeginTabBar("##tabs")) {
            if (ImGui::BeginTabItem("Messages")) {
                drawMessages(engine);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Watches")) {
                drawWatches(engine);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Frame time")) {
                drawTimes(engine, installed);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(("Missing (" + std::to_string(census) + ")###census").c_str())) {
                drawCensus(engine);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}


} // namespace viewer::ui::script_console
