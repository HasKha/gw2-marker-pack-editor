#pragma once

#include <optional>
#include <string>
#include <vector>

#include "viewer/catalog.h"
#include "viewer/downloads.h"

// The window listing the public catalog: Pathing's PackRepoView and its MarkerPackHero rows —
// download, update, keep updated, the pack's page, delete.
namespace viewer::ui::pack_catalog {

// One package as the window shows it.
struct Row {
    const catalog::Package* package = nullptr;
    const catalog::LocalPack* local = nullptr; // its copy on disk, if any
    catalog::State state = catalog::State::Absent;
    std::optional<catalog::Downloads::Status> download;
    bool keep_updated = true;
};

// What the player asked for, by row.
struct Action {
    enum class Kind { Download, Delete, KeepUpdated, OpenPage };
    Kind kind = Kind::Download;
    size_t row = 0;
    bool value = false; // KeepUpdated's new value
};

// Draws the window while `*open`. `status` says how the catalog's list is coming along, and
// with `can_retry` a button asks for it again, which sets `*retry`.
std::vector<Action> draw(const std::vector<Row>& rows, const std::string& status, bool can_retry, bool* retry, bool* open);

} // namespace viewer::ui::pack_catalog
