#pragma once

#include <filesystem>
#include <optional>
#include <vector>

// The game's own key binds, as Nexus keeps them: what interacting is bound to, so the addon
// interacts on the game's key rather than a setting of its own.
//
// Nexus's addon API can press a game bind but not say which key it is, so its file is read:
// `<game>/addons/Nexus/GameBinds.xml`, the game's InputBinds format,
//
//     <InputBindings><action id="65" device="Keyboard" button="70" mod="2" device2="None"/>...
//
// with `id` the game's action — EGameBinds in Nexus.h — `button` the game's own key code, and
// `mod` Shift 1, Ctrl 2, Alt 4. The second bind is `device2`, `button2` and `mod2`.
namespace viewer::game_binds {

constexpr int ACTION_INTERACT = 65; // EGameBinds_MiscInteract
constexpr const char* FILE_NAME = "GameBinds.xml"; // in <game>/addons/Nexus

struct Key {
    int virtual_key = 0; // Windows'
    bool shift = false;
    bool ctrl = false;
    bool alt = false;

    bool operator==(const Key&) const = default;
};

// The Windows virtual key for the game's code of a keyboard key; 0 for one not known here.
//
// The game's codes are its own. Its letters and digits are their ASCII codes, as Windows' virtual
// keys are, which a bind file shows at once: Interact on F is button 70. Nothing else has been
// matched against a key it is known to be, so nothing else is guessed at.
int virtualKey(int game_code);

// The keyboard keys `action` is bound to in `file`, its first bind before its second; those whose
// key virtualKey does not know are left out, so an action bound only to the mouse, or to keys not
// known here, has none. Nothing at all — not an empty list — for a file that is not there or not
// read, or that does not name the action: there the game's default stands.
std::optional<std::vector<Key>> read(const std::filesystem::path& file, int action);

} // namespace viewer::game_binds
