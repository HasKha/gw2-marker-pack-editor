#include "editor/settings.h"

#include <nlohmann/json.hpp>

#include "util/json_file.h"
#include "util/path_text.h"
#include "viewer/viewer.h"

namespace settings {

namespace {

ui::windows::Id windowId(size_t index) {
    return static_cast<ui::windows::Id>(index);
}

} // namespace

File::File(AddonAPI* api) {
    if (!api) return;

    // Nexus makes the directory; the file is ours.
    const char* directory = api->Paths.GetAddonDirectory(viewer::ADDON_FOLDER);
    if (directory) path = std::filesystem::path(directory) / FILE_NAME;
}

void File::load(
    editor::Settings* editor,
    ui::windows::Shown* windows,
    std::filesystem::path* last_folder,
    std::filesystem::path* last_image_folder) {
    const std::optional<nlohmann::json> read = util::json_file::readObject(path);
    if (!read) return;
    const nlohmann::json& parsed = *read;

    editor->highlight_3d = util::json_file::readBool(parsed, "highlight_3d", editor->highlight_3d);
    editor->select_in_viewport = util::json_file::readBool(parsed, "select_in_viewport", editor->select_in_viewport);

    const int gizmo = util::json_file::readInt(parsed, "gizmo", static_cast<int>(editor->gizmo));
    if (gizmo >= 0 && gizmo <= static_cast<int>(editor::GizmoMode::TranslateRotate)) {
        editor->gizmo = static_cast<editor::GizmoMode>(gizmo);
    }

    const auto shown = parsed.find("windows");
    if (shown != parsed.end() && shown->is_object()) {
        for (size_t i = 0; i < ui::windows::kCount; ++i) {
            bool& visible = (*windows)[windowId(i)];
            visible = util::json_file::readBool(*shown, ui::windows::key(windowId(i)), visible);
        }
    }

    *last_folder = util::pathFromText(util::json_file::readString(parsed, "last_folder"));
    *last_image_folder = util::pathFromText(util::json_file::readString(parsed, "last_image_folder"));

    written = Snapshot{*editor, *windows, *last_folder, *last_image_folder};
}

void File::update(
    const editor::Settings& editor,
    const ui::windows::Shown& windows,
    const std::filesystem::path& last_folder,
    const std::filesystem::path& last_image_folder) {
    if (path.empty()) return;
    // Part by part rather than against a Snapshot built to compare: this runs every frame, and
    // building one would copy two paths each time to almost always conclude nothing happened.
    if (written && written->editor == editor && written->windows == windows && written->last_folder == last_folder &&
        written->last_image_folder == last_image_folder) {
        return;
    }

    nlohmann::json out;
    out["highlight_3d"] = editor.highlight_3d;
    out["select_in_viewport"] = editor.select_in_viewport;
    out["gizmo"] = static_cast<int>(editor.gizmo);
    for (size_t i = 0; i < ui::windows::kCount; ++i) out["windows"][ui::windows::key(windowId(i))] = windows[windowId(i)];
    out["last_folder"] = util::pathText(last_folder);
    out["last_image_folder"] = util::pathText(last_image_folder);

    if (util::json_file::write(path, out)) written = Snapshot{editor, windows, last_folder, last_image_folder};
}

} // namespace settings
