#include "viewer/ui/script_console.h"

#include <IconsFontAwesome7.h>
#include <imgui.h>

#include <string>

#include "util/json_file.h"
#include "util/path_text.h"
#include "viewer/ui/fonts.h"
#include "viewer/ui/style.h"

namespace viewer::ui::script_console {

namespace {

ImVec4 colourOf(script::Level level) {
    switch (level) {
    case script::Level::Warn: return style::WARNING;
    case script::Level::Error: return style::ERROR_TEXT;
    case script::Level::System: return style::muted();
    default: return ImGui::GetStyleColorVec4(ImGuiCol_Text);
    }
}

void drawMessages(const script::Engine& engine, const std::filesystem::path& save_file) {
    static uint64_t seen_revision = 0;
    static std::string status; // what the last Copy or Save did, until the next

    // The whole log, tracebacks included, for a player to send with a report.
    if (ImGui::Button(fonts::label(ICON_FA_COPY, "Copy").c_str())) {
        ImGui::SetClipboardText(engine.console().text().c_str());
        status = "Copied " + std::to_string(engine.console().messages().size()) + " messages to the clipboard";
    }
    if (!save_file.empty()) {
        ImGui::SameLine();
        if (ImGui::Button(fonts::label(ICON_FA_FLOPPY_DISK, "Save").c_str())) {
            status = util::json_file::writeText(save_file, engine.console().text()) ? "Saved to " + util::pathText(save_file)
                                                                                    : "Could not write " + util::pathText(save_file);
        }
    }
    if (!status.empty()) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(style::muted(), "%s", status.c_str());
    }

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
        ImGui::TextDisabled("Nothing watched yet. A script adds a line here with Debug:Watch(name, value).");
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
    bool any = false;
    for (size_t pack = 0; pack < installed.packs.size(); ++pack) {
        const double milliseconds = engine.frameSeconds(pack) * 1000.0;
        if (milliseconds > 0.0) {
            ImGui::Text("%8.3f ms  %s", milliseconds, util::pathText(installed.packs[pack]->loaded->path.filename()).c_str());
            any = true;
        }
    }
    if (!any) ImGui::TextColored(style::muted(), "No pack script ran this frame.");
}

void drawCensus(const script::Engine& engine) {
    if (engine.census().entries().empty()) {
        ImGui::TextDisabled("No script has used anything missing.");
        return;
    }
    for (const script::CensusEntry& entry : engine.census().entries()) {
        ImGui::Text("%s  (%s:%d)", entry.member.c_str(), entry.source.c_str(), entry.line);
    }
}

} // namespace

void draw(const script::Engine& engine, const runtime::Installed& installed, const std::filesystem::path& save_file, bool* open) {
    if (!*open) return;
    ImGui::SetNextWindowSize(ImVec2(640.0f, 400.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Lua Script Console", open)) {
        const size_t census = engine.census().entries().size();
        if (ImGui::BeginTabBar("##tabs")) {
            if (ImGui::BeginTabItem("Messages")) {
                drawMessages(engine, save_file);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Watches")) {
                drawWatches(engine);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Frame Time")) {
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
