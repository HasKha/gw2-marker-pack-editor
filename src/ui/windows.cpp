#include "ui/windows.h"

#include <imgui.h>

#include <cstddef>

namespace ui::windows {

namespace {

constexpr size_t kCount = static_cast<size_t>(Id::Count);

struct Definition {
    const char* title; // with the ##id suffix
    const char* label; // what the View menu and the options page call it
    bool visible_by_default;
};

// One row per window, in View menu order. Everything a window needs to be known by lives
// here rather than being spelled out at each use.
constexpr Definition kWindows[kCount] = {
    {"Marker Pack Editor##MPE_Tree", "Tree", true},
    {"Marker Details##MPE_Details", "Details", true},
    {"Marker Toolbar##MPE_Toolbar", "Toolbar", true},
    {"Trail Editor##MPE_Trail", "Trail Editor", false},
    {"Pack Resources##MPE_Resources", "Resources", false},
};

bool shown[kCount] = {
    kWindows[0].visible_by_default,
    kWindows[1].visible_by_default,
    kWindows[2].visible_by_default,
    kWindows[3].visible_by_default,
    kWindows[4].visible_by_default,
};

// What was on screen when everything was hidden, so the layout survives a toggle.
bool restore[kCount] = {};

size_t indexOf(Id id) {
    return static_cast<size_t>(id);
}

bool anyVisible() {
    for (bool visible : shown) {
        if (visible) return true;
    }
    return false;
}

void hideAll() {
    for (size_t i = 0; i < kCount; ++i) {
        restore[i] = shown[i];
        shown[i] = false;
    }
}

} // namespace

const char* title(Id id) {
    return kWindows[indexOf(id)].title;
}

bool& visible(Id id) {
    return shown[indexOf(id)];
}

void toggleAll() {
    if (anyVisible()) {
        hideAll();
        return;
    }

    bool restored_any = false;
    for (size_t i = 0; i < kCount; ++i) {
        shown[i] = restore[i];
        restored_any = restored_any || restore[i];
    }

    // Nothing to restore — first use, or the windows were closed one at a time rather than
    // hidden together. Coming back to an empty screen would look like a broken keybind.
    if (!restored_any) {
        for (size_t i = 0; i < kCount; ++i) shown[i] = kWindows[i].visible_by_default;
    }
}

void drawViewMenu() {
    for (size_t i = 0; i < kCount; ++i) ImGui::MenuItem(kWindows[i].label, nullptr, &shown[i]);

    ImGui::Separator();
    if (ImGui::MenuItem("Hide all")) hideAll();
}

void drawOptions() {
    for (size_t i = 0; i < kCount; ++i) ImGui::Checkbox(kWindows[i].label, &shown[i]);
}

} // namespace ui::windows
