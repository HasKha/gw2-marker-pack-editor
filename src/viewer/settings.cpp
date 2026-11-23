#include "viewer/settings.h"

#include "viewer/json_file.h"

namespace viewer::settings {

Settings load(const std::filesystem::path& folder) {
    Settings settings;
    if (folder.empty()) return settings; // Nexus gave no folder: nothing to read, nothing to inherit

    std::error_code ec;
    settings.enable_editing = std::filesystem::exists(folder / EDITOR_FILE_NAME, ec);

    if (const std::optional<nlohmann::json> parsed = json_file::readObject(folder / FILE_NAME)) {
        settings.enable_editing = json_file::readBool(*parsed, "enable_editing", settings.enable_editing);
        settings.read_blish_folder = json_file::readBool(*parsed, "read_blish_folder", settings.read_blish_folder);
        settings.run_scripts = json_file::readBool(*parsed, "run_scripts", settings.run_scripts);
        settings.allow_auto_hide = json_file::readBool(*parsed, "allow_auto_hide", settings.allow_auto_hide);
        settings.allow_animation = json_file::readBool(*parsed, "allow_animation", settings.allow_animation);
        settings.show_interact_prompt = json_file::readBool(*parsed, "show_interact_prompt", settings.show_interact_prompt);
        settings.clipboard_consent = json_file::readInt(*parsed, "clipboard_consent", settings.clipboard_consent);
        settings.interact_key = json_file::readInt(*parsed, "interact_key", settings.interact_key);
    }
    return settings;
}

bool save(const std::filesystem::path& folder, const Settings& settings) {
    if (folder.empty()) return false;

    nlohmann::json out;
    out["enable_editing"] = settings.enable_editing;
    out["read_blish_folder"] = settings.read_blish_folder;
    out["run_scripts"] = settings.run_scripts;
    out["allow_auto_hide"] = settings.allow_auto_hide;
    out["allow_animation"] = settings.allow_animation;
    out["show_interact_prompt"] = settings.show_interact_prompt;
    out["clipboard_consent"] = settings.clipboard_consent;
    out["interact_key"] = settings.interact_key;
    return json_file::write(folder / FILE_NAME, out);
}

} // namespace viewer::settings
