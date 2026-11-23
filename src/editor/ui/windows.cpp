#include "editor/ui/windows.h"

#include <imgui.h>

#include <array>
#include <cstddef>

namespace ui::windows {

namespace {

constexpr size_t kCount = static_cast<size_t>(Id::Count);

struct Definition {
    const char* title; // with the ##id suffix
    const char* label; // what the View menu calls it
    const char* key; // what the settings file calls it
    bool visible_by_default;
};

// One row per window, in View menu order. Everything a window needs to be known by lives
// here rather than being spelled out at each use.
constexpr Definition kWindows[kCount] = {
    {"Marker Pack Editor##MPE_Tree", "Tree", "tree", true},
    {"Marker Details##MPE_Details", "Details", "details", true},
    {"Marker Toolbar##MPE_Toolbar", "Toolbar", "toolbar", true},
    {"Trail Editor##MPE_Trail", "Trail Editor", "trail_editor", false},
    {"Pack Resources##MPE_Resources", "Resources", "resources", false},
};

// Seeded from the table rather than spelled out per index: adding a window has to stay one
// row, and a hand-written initializer would quietly leave the sixth one hidden.
constexpr std::array<bool, kCount> defaultVisibility() {
    std::array<bool, kCount> out{};
    for (size_t i = 0; i < kCount; ++i) out[i] = kWindows[i].visible_by_default;
    return out;
}

std::array<bool, kCount> shown = defaultVisibility();

size_t indexOf(Id id) {
    return static_cast<size_t>(id);
}

} // namespace

const char* title(Id id) {
    return kWindows[indexOf(id)].title;
}

const char* key(Id id) {
    return kWindows[indexOf(id)].key;
}

bool& visible(Id id) {
    return shown[indexOf(id)];
}

void drawViewMenu() {
    for (size_t i = 0; i < kCount; ++i) {
        if (static_cast<Id>(i) != Id::Tree) ImGui::MenuItem(kWindows[i].label, nullptr, &shown[i]);
    }
}

} // namespace ui::windows
