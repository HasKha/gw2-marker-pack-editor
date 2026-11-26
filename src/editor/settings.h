#pragma once

#include <Nexus.h>

#include <filesystem>
#include <optional>

#include "editor/editor_state.h"
#include "editor/ui/windows.h"

// The handful of things that should survive a game restart, in
// `<addons>/Markus/settings.json`.
//
// Deliberately not in here: anything about the *pack*. Visibility toggles, the selection
// and the open trail all belong to a pack, not to the editor, and a settings file that
// remembered them would be wrong the moment a different pack was opened.
namespace settings {

constexpr const char* FILE_NAME = "settings.json";

class File
{
public:
    // Nothing is read or written without an api to say where the addon's folder is.
    explicit File(AddonAPI* api);

    // Applies whatever was saved. Call once, before the first frame. A missing or unreadable
    // file is not an error — it just means the defaults stand.
    void load(
        editor::Settings* editor,
        ui::windows::Shown* windows,
        std::filesystem::path* last_folder,
        std::filesystem::path* last_image_folder);

    // Writes the file when anything it persists has changed since the last write. Call once a
    // frame: it compares rather than being told, so a new setting cannot be added and then
    // forgotten at the call sites that change it.
    void update(
        const editor::Settings& editor,
        const ui::windows::Shown& windows,
        const std::filesystem::path& last_folder,
        const std::filesystem::path& last_image_folder);

private:
    struct Snapshot {
        editor::Settings editor;
        ui::windows::Shown windows;
        std::filesystem::path last_folder;
        std::filesystem::path last_image_folder;
    };

    std::filesystem::path path_;
    // What was last written or read, for update to compare against.
    std::optional<Snapshot> written_;
};

} // namespace settings
