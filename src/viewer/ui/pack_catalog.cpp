#include "viewer/ui/pack_catalog.h"
#include "pack/path_text.h"
#include "viewer/ui/fonts.h"

#include <IconsFontAwesome7.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace viewer::ui::pack_catalog {

namespace {

// Pathing's Chardonnay, for when a pack was last updated, and its red for an error.
const ImVec4 kDateColour(1.0f, 205.0f / 255.0f, 130.0f / 255.0f, 1.0f);
const ImVec4 kErrorColour(1.0f, 0.4f, 0.4f, 1.0f);

bool containsIgnoringCase(const std::string& text, const char* needle) {
    const std::string_view wanted(needle);
    if (wanted.empty()) return true;
    const auto it = std::search(text.begin(), text.end(), wanted.begin(), wanted.end(), [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    });
    return it != text.end();
}

// "today", "3 days ago", "2 months ago": roughly how Pathing's Humanize() reads.
std::string ago(UtcMillis time) {
    if (time <= 0) return "";
    const long long days = (utcNow() - time) / (24LL * 60 * 60 * 1000);
    if (days < 1) return "Updated today";
    if (days < 2) return "Updated yesterday";
    if (days < 60) return "Updated " + std::to_string(days) + " days ago";
    if (days < 730) return "Updated " + std::to_string(days / 30) + " months ago";
    return "Updated " + std::to_string(days / 365) + " years ago";
}

// The download button's words, Pathing's: Download, Update, Up to Date.
const char* buttonLabel(const Row& row) {
    switch (row.state) {
    case catalog::State::Absent: return "Download";
    case catalog::State::Stale: return "Update";
    case catalog::State::Current: return nullptr;
    }
    return nullptr;
}

// One row; true when anything in it was used, `*action` saying what.
bool drawRow(const Row& row, size_t index, Action* action) {
    const catalog::Package& package = *row.package;
    bool acted = false;
    ImGui::PushID(static_cast<int>(index));

    ImGui::TextUnformatted(package.name.c_str());
    if (!package.author.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("by %s", package.author.c_str());
    }
    if (package.total_downloads > 0 && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("About %d downloads a day", package.total_downloads);
    }
    if (const std::string updated = ago(package.last_update); !updated.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(kDateColour, "%s", updated.c_str());
    }
    if (!package.categories.empty()) ImGui::TextDisabled("%s", package.categories.c_str());
    if (!package.description.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(package.description.c_str());
        ImGui::PopTextWrapPos();
    }

    const bool running = row.download && row.download->running;
    if (running) {
        const float progress = row.download->progress;
        char label[32];
        std::snprintf(
            label,
            sizeof(label),
            progress < 0.0f ? "Downloading..." : "%d%%",
            static_cast<int>(std::min(progress, 0.99f) * 100.0f));
        ImGui::ProgressBar(progress < 0.0f ? 0.0f : progress, ImVec2(ImGui::GetFontSize() * 10.0f, 0.0f), label);
    } else if (const char* label = buttonLabel(row)) {
        if (ImGui::Button(fonts::label(ICON_FA_DOWNLOAD, label).c_str())) {
            *action = Action{Action::Kind::Download, index};
            acted = true;
        }
        if (ImGui::IsItemHovered() && package.size_mb > 0.0f) ImGui::SetTooltip("%.1f MB", package.size_mb);
    } else {
        ImGui::TextDisabled("Up to date");
    }

    ImGui::SameLine();
    bool keep = row.keep_updated;
    if (ImGui::Checkbox("Keep updated", &keep)) {
        *action = Action{Action::Kind::KeepUpdated, index, keep};
        acted = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("New versions download when the addon loads.");

    if (!package.info.empty()) {
        ImGui::SameLine();
        if (ImGui::Button(fonts::label(ICON_FA_ARROW_UP_RIGHT_FROM_SQUARE, "Info").c_str())) {
            *action = Action{Action::Kind::OpenPage, index};
            acted = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", package.info.c_str());
    }

    if (row.local && !running) {
        ImGui::SameLine();
        if (ImGui::Button(fonts::label(ICON_FA_TRASH, "Delete").c_str())) ImGui::OpenPopup("##delete");
        if (ImGui::BeginPopup("##delete")) {
            ImGui::Text("Delete %s from disk?", pack::pathText(row.local->path.filename()).c_str());
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

    if (row.download && !row.download->running && !row.download->error.empty()) {
        ImGui::TextColored(kErrorColour, "Error: %s", row.download->error.c_str());
    }

    ImGui::PopID();
    return acted;
}

} // namespace

std::vector<Action> draw(const std::vector<Row>& rows, const std::string& status, bool can_retry, bool* retry, bool* open) {
    std::vector<Action> actions;
    if (!*open) return actions;

    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 36.0f, ImGui::GetFontSize() * 30.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Marker Packs", open)) {
        ImGui::TextUnformatted(status.c_str());
        if (can_retry) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Retry")) *retry = true;
        }

        static char filter[128] = "";
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##filter", "Filter by name, author or category", filter, sizeof(filter));
        ImGui::Separator();

        if (ImGui::BeginChild("##packages")) {
            for (size_t i = 0; i < rows.size(); ++i) {
                const catalog::Package& package = *rows[i].package;
                if (!containsIgnoringCase(package.name, filter) && !containsIgnoringCase(package.author, filter) &&
                    !containsIgnoringCase(package.categories, filter)) {
                    continue;
                }
                Action action;
                if (drawRow(rows[i], i, &action)) actions.push_back(action);
                ImGui::Separator();
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
    return actions;
}

} // namespace viewer::ui::pack_catalog
