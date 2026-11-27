#pragma once

#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "viewer/packs/catalog.h"
#include "viewer/packs/downloads.h"
#include "viewer/packs/library.h"

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

// What the window lists, and how the catalog's list is coming along.
struct Listing {
    std::vector<Row> rows;
    std::string status;
    bool failed = false; // the catalog answered with no list, which a retry asks for again
};

// Every package of `library`'s list, with its copy in `local` if any, then every pack of `local`
// the catalog does not list: all of them while its list is not in. The rows point into both.
Listing list(const catalog::Library& library, const std::vector<catalog::LocalPack>& local);

// What the player asked for, and of which row.
struct Action {
    enum class Kind { Download, Delete, KeepUpdated, OpenPage, OpenFolder };
    Kind kind = Kind::Download;
    const Row* row = nullptr; // none for OpenFolder, which is the window's, not a row's
    bool value = false; // KeepUpdated's new value
};

enum class Tab { None, Installed, Library };

// What the window keeps between frames.
struct View {
    // Above the tabs, so it holds across both.
    std::string filter;
    // The tab drawn last frame, None while the window is closed or collapsed, and the packs it
    // has shown since. A pack downloaded or deleted no longer belongs in its tab but stays
    // there, rather than the list jumping under the player's mouse, until the player switches
    // tabs or opens the window again.
    Tab shown_tab = Tab::None;
    std::unordered_set<std::string> shown;
};

// Draws the window while `*open`. `library_status` says how the catalog's list is coming
// along, and with `can_retry` a button asks for it again, which sets `*retry`.
std::vector<Action>
draw(View* view, const std::vector<Row>& rows, const std::string& library_status, bool can_retry, bool* retry, bool* open);

} // namespace viewer::ui::pack_catalog
