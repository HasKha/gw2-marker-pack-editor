#pragma once

#include <optional>
#include <string>
#include <vector>

#include "viewer/catalog.h"
#include "viewer/downloads.h"

// The Marker Pack Library window: Pathing's PackRepoView and its MarkerPackHero rows — download,
// update, keep updated, the pack's page, delete — sorted into two tabs, Installed for the packs
// on disk and Library for the rest.
namespace viewer::ui::pack_catalog {

// One pack as the window shows it: a catalog entry, a pack on disk the catalog does not list,
// or both.
struct Row {
    const catalog::Package* package = nullptr; // the catalog's entry, if it lists the pack
    const catalog::LocalPack* local = nullptr; // its copy on disk, if any
    catalog::State state = catalog::State::Absent;
    std::optional<catalog::Downloads::Status> download;
    bool keep_updated = true;
};

// What the player asked for, and of which row.
struct Action {
    enum class Kind { Download, Delete, KeepUpdated, OpenPage };
    Kind kind = Kind::Download;
    const Row* row = nullptr;
    bool value = false; // KeepUpdated's new value
};

// Draws the window while `*open`. `library_status` says how the catalog's list is coming
// along, and with `can_retry` a button asks for it again, which sets `*retry`.
std::vector<Action> draw(const std::vector<Row>& rows, const std::string& library_status, bool can_retry, bool* retry, bool* open);

} // namespace viewer::ui::pack_catalog
