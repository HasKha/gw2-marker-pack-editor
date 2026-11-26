#include "viewer/game/interact_key.h"

#include <algorithm>
#include <iterator>
#include <vector>

#include "viewer/game/game_binds.h"

namespace viewer {

namespace {

uint32_t pack(const game_binds::Key& key) {
    return static_cast<uint32_t>(key.virtual_key) | (key.shift ? 1u << 8 : 0u) | (key.ctrl ? 2u << 8 : 0u) | (key.alt ? 4u << 8 : 0u);
}

} // namespace

void InteractKey::open(std::filesystem::path file) {
    file_ = std::move(file);
    read();
}

void InteractKey::read() {
    std::error_code ec;
    const std::filesystem::file_time_type written = std::filesystem::last_write_time(file_, ec);
    if (ec || written == written_) return;
    written_ = written;
    const std::vector<game_binds::Key> keys =
        game_binds::read(file_, game_binds::ACTION_INTERACT).value_or(std::vector<game_binds::Key>{game_binds::Key{'F'}});
    binds_[0] = keys.size() > 0 ? pack(keys[0]) : 0;
    binds_[1] = keys.size() > 1 ? pack(keys[1]) : 0;
}

bool InteractKey::matches(WPARAM key) const {
    const auto held = [](int virtual_key) { return (GetKeyState(virtual_key) & 0x8000) != 0; };
    const uint32_t mods = (held(VK_SHIFT) ? 1u : 0u) | (held(VK_CONTROL) ? 2u : 0u) | (held(VK_MENU) ? 4u : 0u);
    return std::any_of(std::begin(binds_), std::end(binds_), [&](const std::atomic<uint32_t>& bind) {
        const uint32_t packed = bind.load();
        return packed != 0 && static_cast<WPARAM>(packed & 0xFF) == key && (packed >> 8) == mods;
    });
}

std::string InteractKey::name() const {
    const int virtual_key = static_cast<int>(binds_[0].load() & 0xFF);
    if (virtual_key == 0) return "Click";
    const LONG scan = static_cast<LONG>(MapVirtualKeyW(static_cast<UINT>(virtual_key), MAPVK_VK_TO_VSC)) << 16;
    char name[64] = {};
    if (GetKeyNameTextA(scan, name, static_cast<int>(sizeof(name))) > 0) return name;
    return "?";
}

} // namespace viewer
