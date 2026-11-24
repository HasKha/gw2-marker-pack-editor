#include "viewer/ui/pack_catalog.h"
#include "util/path_text.h"
#include "viewer/ui/fonts.h"

#include <IconsFontAwesome7.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <unordered_set>

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

// Whether the filter keeps `row`: its name, author or categories hold the text, or its file
// name does, which is all a pack the catalog does not list has.
bool matchesFilter(const Row& row, const char* filter) {
    if (const catalog::Package* package = row.package) {
        if (containsIgnoringCase(package->name, filter) || containsIgnoringCase(package->author, filter) ||
            containsIgnoringCase(package->categories, filter)) {
            return true;
        }
    }
    return row.local && containsIgnoringCase(util::pathText(row.local->path.filename()), filter);
}

// One pack, the same in either tab; true when anything in it was used, `*action` saying what.
bool drawRow(const Row& row, Action* action) {
    bool acted = false;

    // What it is: the catalog's name, author, date, categories and description, or for a pack
    // the catalog does not list, its file name alone.
    if (const catalog::Package* package = row.package) {
        ImGui::TextUnformatted(package->name.c_str());
        if (!package->author.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("by %s", package->author.c_str());
        }
        if (package->total_downloads > 0 && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("About %d downloads a day", package->total_downloads);
        }
        if (const std::string updated = ago(package->last_update); !updated.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(kDateColour, "%s", updated.c_str());
        }
        if (!package->categories.empty()) ImGui::TextDisabled("%s", package->categories.c_str());
        if (!package->description.empty()) {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(package->description.c_str());
            ImGui::PopTextWrapPos();
        }
    } else {
        ImGui::TextUnformatted(util::pathText(row.local->path.filename()).c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Not in the public catalog.");
    }

    // Download, Update or Up to date, Pathing's words, or the bar of a download under way; then
    // Keep updated and the pack's page. All for a pack the catalog lists.
    const bool running = row.download && row.download->running;
    if (const catalog::Package* package = row.package) {
        if (running) {
            const float progress = row.download->progress;
            char label[32];
            std::snprintf(
                label,
                sizeof(label),
                progress < 0.0f ? "Downloading..." : "%d%%",
                static_cast<int>(std::min(progress, 0.99f) * 100.0f));
            ImGui::ProgressBar(progress < 0.0f ? 0.0f : progress, ImVec2(ImGui::GetFontSize() * 10.0f, 0.0f), label);
        } else if (row.state == catalog::State::Current) {
            ImGui::TextDisabled("Up to date");
        } else {
            if (ImGui::Button(fonts::label(ICON_FA_DOWNLOAD, row.state == catalog::State::Stale ? "Update" : "Download").c_str())) {
                *action = Action{Action::Kind::Download, &row};
                acted = true;
            }
            if (ImGui::IsItemHovered() && package->size_mb > 0.0f) ImGui::SetTooltip("%.1f MB", package->size_mb);
        }

        ImGui::SameLine();
        bool keep = row.keep_updated;
        if (ImGui::Checkbox("Keep updated", &keep)) {
            *action = Action{Action::Kind::KeepUpdated, &row, keep};
            acted = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("New versions download when the addon loads.");

        if (!package->info.empty()) {
            ImGui::SameLine();
            if (ImGui::Button(fonts::label(ICON_FA_ARROW_UP_RIGHT_FROM_SQUARE, "Info").c_str())) {
                *action = Action{Action::Kind::OpenPage, &row};
                acted = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", package->info.c_str());
        }
    }

    // Delete, for a pack on disk, but not while a new copy of it is being written.
    if (row.local && !running) {
        if (row.package) ImGui::SameLine();
        if (ImGui::Button(fonts::label(ICON_FA_TRASH, "Delete").c_str())) ImGui::OpenPopup("##delete");
        if (ImGui::BeginPopup("##delete")) {
            ImGui::Text("Delete %s from disk?", util::pathText(row.local->path.filename()).c_str());
            if (ImGui::Button("Delete")) {
                *action = Action{Action::Kind::Delete, &row};
                acted = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    if (row.download && !running && !row.download->error.empty()) {
        ImGui::TextColored(kErrorColour, "Error: %s", row.download->error.c_str());
    }
    return acted;
}

enum class Tab { None, Installed, Library };

// The tab drawn last frame, None while the window is closed or collapsed, and the packs it has
// shown since. A pack downloaded or deleted no longer belongs in its tab but stays there, rather
// than the list jumping under the player's mouse, until the player switches tabs or opens the
// window again.
Tab shown_tab = Tab::None;
std::unordered_set<std::string> shown;

// The rows of `tab`: the packs on disk under Installed, the rest under Library, and those that
// belonged there since the tab was shown.
void drawTab(Tab tab, const std::vector<Row>& rows, const char* filter, std::vector<Action>* actions) {
    if (shown_tab != tab) shown.clear();
    shown_tab = tab;
    if (ImGui::BeginChild("##packages")) {
        for (size_t i = 0; i < rows.size(); ++i) {
            const Row& row = rows[i];
            const std::string key = row.package ? row.package->file_name : util::pathText(row.local->path.filename());
            if ((row.local != nullptr) == (tab == Tab::Installed)) shown.insert(key);
            if (shown.count(key) && matchesFilter(row, filter)) {
                ImGui::PushID(static_cast<int>(i));
                Action action;
                if (drawRow(row, &action)) actions->push_back(action);
                ImGui::Separator();
                ImGui::PopID();
            }
        }
    }
    ImGui::EndChild();
}

} // namespace

std::vector<Action> draw(const std::vector<Row>& rows, const std::string& library_status, bool can_retry, bool* retry, bool* open) {
    std::vector<Action> actions;
    if (!*open) {
        shown_tab = Tab::None;
        return actions;
    }

    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 36.0f, ImGui::GetFontSize() * 30.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Marker Pack Library", open)) {
        // The filter, above the tabs, so it holds across both.
        static char filter[128] = "";
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##filter", "Filter by name, author or category", filter, sizeof(filter));

        if (ImGui::BeginTabBar("##tabs")) {
            // The tab's ID after ###, so it stays selected as its count changes.
            const size_t installed = std::count_if(rows.begin(), rows.end(), [](const Row& row) { return row.local != nullptr; });
            const std::string installed_label = "Installed (" + std::to_string(installed) + ")###installed";
            if (ImGui::BeginTabItem(installed_label.c_str())) {
                const size_t stale =
                    std::count_if(rows.begin(), rows.end(), [](const Row& row) { return row.state == catalog::State::Stale; });
                if (installed == 0) {
                    ImGui::Text("No packs installed. Download some from Library.");
                } else if (stale > 0) {
                    ImGui::Text("%zu with an update", stale);
                }
                drawTab(Tab::Installed, rows, filter, &actions);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Library")) {
                ImGui::TextUnformatted(library_status.c_str());
                if (can_retry) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Retry")) *retry = true;
                }
                drawTab(Tab::Library, rows, filter, &actions);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    } else {
        shown_tab = Tab::None; // collapsed
    }
    ImGui::End();
    return actions;
}

} // namespace viewer::ui::pack_catalog
