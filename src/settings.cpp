#include "settings.h"

#include <nlohmann/json.hpp>

#include <fstream>

#include "marker_editor.h"
#include "ui/windows.h"

namespace settings {

namespace {

constexpr size_t kWindowCount = static_cast<size_t>(ui::windows::Id::Count);

std::filesystem::path file;

// What was last written. `update` compares against this rather than being told when
// something changes: a setting added later cannot forget to announce itself, and there is
// no call site to instrument.
struct Snapshot {
    bool valid = false;

    bool highlight_3d = false;
    bool select_in_viewport = false;
    int gizmo = 0;
    bool windows[kWindowCount] = {};
    std::filesystem::path last_folder;
    std::filesystem::path last_image_folder;
};

Snapshot written;

ui::windows::Id windowId(size_t index) {
    return static_cast<ui::windows::Id>(index);
}

Snapshot capture(const editor::Settings& editor, const std::filesystem::path& last_folder, const std::filesystem::path& last_image_folder) {
    Snapshot out;
    out.valid = true;
    out.highlight_3d = editor.highlight_3d;
    out.select_in_viewport = editor.select_in_viewport;
    out.gizmo = static_cast<int>(editor.gizmo);
    for (size_t i = 0; i < kWindowCount; ++i) out.windows[i] = ui::windows::visible(windowId(i));
    out.last_folder = last_folder;
    out.last_image_folder = last_image_folder;
    return out;
}

// Field by field rather than building a snapshot to compare: this runs every frame, and a
// snapshot would allocate two paths each time to almost always conclude nothing happened.
bool differs(const editor::Settings& editor, const std::filesystem::path& last_folder, const std::filesystem::path& last_image_folder) {
    if (!written.valid) return true;
    if (written.highlight_3d != editor.highlight_3d) return true;
    if (written.select_in_viewport != editor.select_in_viewport) return true;
    if (written.gizmo != static_cast<int>(editor.gizmo)) return true;
    for (size_t i = 0; i < kWindowCount; ++i) {
        if (written.windows[i] != ui::windows::visible(windowId(i))) return true;
    }
    if (written.last_folder != last_folder) return true;
    if (written.last_image_folder != last_image_folder) return true;
    return false;
}

} // namespace

void init(AddonAPI* api) {
    if (!api) return;

    // Nexus makes the directory; the file is ours.
    const char* directory = api->Paths.GetAddonDirectory(marker_editor::SETTINGS_FOLDER);
    if (directory) file = std::filesystem::path(directory) / "settings.json";
}

void load(editor::Settings* editor, std::filesystem::path* last_folder, std::filesystem::path* last_image_folder) {
    if (file.empty() || !editor || !last_folder || !last_image_folder) return;

    std::ifstream input(file);
    if (!input) return;

    // A settings file that will not parse is not worth stopping for, and not worth
    // repairing either: the defaults are all reasonable and the next write replaces it.
    const nlohmann::json parsed = nlohmann::json::parse(input, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) return;

    editor->highlight_3d = parsed.value("highlight_3d", editor->highlight_3d);
    editor->select_in_viewport = parsed.value("select_in_viewport", editor->select_in_viewport);

    const int gizmo = parsed.value("gizmo", static_cast<int>(editor->gizmo));
    if (gizmo >= 0 && gizmo <= static_cast<int>(editor::GizmoMode::TranslateRotate)) {
        editor->gizmo = static_cast<editor::GizmoMode>(gizmo);
    }

    if (parsed.contains("windows") && parsed["windows"].is_object()) {
        const nlohmann::json& windows = parsed["windows"];
        for (size_t i = 0; i < kWindowCount; ++i) {
            bool& shown = ui::windows::visible(windowId(i));
            shown = windows.value(ui::windows::key(windowId(i)), shown);
        }
    }

    *last_folder = parsed.value("last_folder", std::string());
    *last_image_folder = parsed.value("last_image_folder", std::string());

    written = capture(*editor, *last_folder, *last_image_folder);
}

void update(const editor::Settings& editor, const std::filesystem::path& last_folder, const std::filesystem::path& last_image_folder) {
    if (file.empty() || !differs(editor, last_folder, last_image_folder)) return;

    nlohmann::json out;
    out["highlight_3d"] = editor.highlight_3d;
    out["select_in_viewport"] = editor.select_in_viewport;
    out["gizmo"] = static_cast<int>(editor.gizmo);
    for (size_t i = 0; i < kWindowCount; ++i) out["windows"][ui::windows::key(windowId(i))] = ui::windows::visible(windowId(i));
    out["last_folder"] = last_folder.string();
    out["last_image_folder"] = last_image_folder.string();

    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);

    std::ofstream output(file, std::ios::trunc);
    if (!output) return;
    output << out.dump(2) << "\n";
    if (!output) return;

    written = capture(editor, last_folder, last_image_folder);
}

} // namespace settings
