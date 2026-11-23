#include "viewer/game_binds.h"

#include <pugixml.hpp>

#include <string>

namespace viewer::game_binds {

namespace {

// One of the action's two binds, by the attributes' suffix: "" for the first, "2" for the second.
void addBind(const pugi::xml_node& action, const std::string& suffix, std::vector<Key>* keys) {
    if (std::string(action.attribute(("device" + suffix).c_str()).as_string()) != "Keyboard") return;
    const int key = virtualKey(action.attribute(("button" + suffix).c_str()).as_int(-1));
    if (key == 0) return;
    const int mods = action.attribute(("mod" + suffix).c_str()).as_int(0);
    keys->push_back(Key{key, (mods & 1) != 0, (mods & 2) != 0, (mods & 4) != 0});
}

} // namespace

int virtualKey(int game_code) {
    if ((game_code >= 'A' && game_code <= 'Z') || (game_code >= '0' && game_code <= '9')) return game_code;
    return 0;
}

std::vector<Key> read(const std::filesystem::path& file, int action) {
    std::vector<Key> keys;
    pugi::xml_document document;
    if (!document.load_file(file.c_str())) return keys;
    for (const pugi::xml_node& node : document.child("InputBindings").children("action")) {
        if (node.attribute("id").as_int(-1) != action) continue;
        addBind(node, "", &keys);
        addBind(node, "2", &keys);
        break;
    }
    return keys;
}

} // namespace viewer::game_binds
