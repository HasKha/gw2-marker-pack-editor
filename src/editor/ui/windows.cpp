#include "editor/ui/windows.h"

#include <imgui.h>

namespace ui::windows {

namespace {

struct Definition {
    const char* title; // with the ##id suffix
    const char* label; // what the View menu calls it: the title before its ##id
    const char* key; // what the settings file calls it
    bool visible_by_default;
};

// One row per window, in View menu order. Everything a window needs to be known by lives
// here rather than being spelled out at each use.
constexpr Definition kWindows[kCount] = {
    {"Markus Editor##Markus_Tree", "Tree", "tree", true},
    {"Marker Details##Markus_Details", "Marker Details", "details", true},
    {"Marker Toolbar##Markus_Toolbar", "Marker Toolbar", "toolbar", true},
    {"Trail Editor##Markus_Trail", "Trail Editor", "trail_editor", false},
    {"Pack Resources##Markus_Resources", "Pack Resources", "resources", false},
};

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

// Seeded from the table rather than spelled out per index: adding a window has to stay one
// row, and a hand-written initializer would quietly leave the sixth one hidden.
Shown::Shown() {
    for (size_t i = 0; i < kCount; ++i) shown_[i] = kWindows[i].visible_by_default;
}

void drawViewMenu(Shown& shown) {
    for (size_t i = 0; i < kCount; ++i) {
        const Id id = static_cast<Id>(i);
        if (id != Id::Tree) ImGui::MenuItem(kWindows[i].label, nullptr, &shown[id]);
    }
}

} // namespace ui::windows
