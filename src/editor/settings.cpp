#include "editor/settings.h"

#include <nlohmann/json.hpp>

#include "editor/ui/windows.h"
#include "viewer/json_file.h"
#include "viewer/settings.h"
#include "viewer/viewer.h"

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
    const char* directory = api->Paths.GetAddonDirectory(viewer::ADDON_FOLDER);
    if (directory) file = std::filesystem::path(directory) / FILE_NAME;
}

void load(editor::Settings* editor, std::filesystem::path* last_folder, std::filesystem::path* last_image_folder) {
    if (!editor || !last_folder || !last_image_folder) return;

    const std::optional<nlohmann::json> read = viewer::json_file::readObject(file);
    if (!read) return;
    const nlohmann::json& parsed = *read;

    editor->highlight_3d = viewer::json_file::readBool(parsed, "highlight_3d", editor->highlight_3d);
    editor->select_in_viewport = viewer::json_file::readBool(parsed, "select_in_viewport", editor->select_in_viewport);

    const int gizmo = viewer::json_file::readInt(parsed, "gizmo", static_cast<int>(editor->gizmo));
    if (gizmo >= 0 && gizmo <= static_cast<int>(editor::GizmoMode::TranslateRotate)) {
        editor->gizmo = static_cast<editor::GizmoMode>(gizmo);
    }

    const auto windows = parsed.find("windows");
    if (windows != parsed.end() && windows->is_object()) {
        for (size_t i = 0; i < kWindowCount; ++i) {
            bool& shown = ui::windows::visible(windowId(i));
            shown = viewer::json_file::readBool(*windows, ui::windows::key(windowId(i)), shown);
        }
    }

    *last_folder = viewer::json_file::readString(parsed, "last_folder");
    *last_image_folder = viewer::json_file::readString(parsed, "last_image_folder");

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

    if (viewer::json_file::write(file, out)) written = capture(editor, last_folder, last_image_folder);
}

} // namespace settings
