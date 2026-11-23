#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "pack/model.h"
#include "pack/trl.h"
#include "viewer/bake.h"
#include "viewer/runtime.h"

// The pack open in the editor, drawn from the editor's live model rather than from disk, so
// that what the author sees is what a player sees once it is saved.
namespace viewer::edited {

// What the editor says about the pack it has open, once a frame.
struct Report {
    // Where it was opened from or last saved; empty for a pack never saved. The viewer drops
    // the installed pack at this path while it is open, and reads it again once it is not.
    std::filesystem::path path;

    // The live model, null when no pack is open. Read only on the render thread, where the
    // editor changes it, and never kept past the frame it was reported for.
    const pack::Pack* pack = nullptr;

    // Moves with every edit, and with every pack opened: the bake is redone when it does.
    uint64_t revision = 0;

    // Per Pack::pathables, whether the editor's tree shows it. A view state, not an edit, so
    // it moves no revision. Null shows everything.
    const std::vector<char>* shown = nullptr;
};

// A trail's points as the editor reads them, unsaved edits included. Null when there is no such
// file, or, with `*deferred` set, when this frame's reads are spent: try again next frame.
using TrailLookup = std::function<std::shared_ptr<const pack::trl::Trail>(const std::string& path, bool* deferred)>;

// The edited pack as a live map of its own, apart from the installed packs': an edit, which a
// gizmo drag makes every frame, never rebuilds theirs or restarts their scripts. Its own scripts
// never load, so they are paused while it is edited.
//
// Its markers and trails have no node in the shared tree: the editor's tree decides what shows,
// through Report::shown.
class EditedLive
{
public:
    // Brings the live map in line with `report` on `map_id`: bakes again when the revision moved,
    // and copies out what is on the map again when the bake, the map, what is shown or a
    // trail's map changed. A trail's map is in its file, so a trail is on no map until
    // `trail` has read it. True when the live map was built again, its entities all new.
    bool update(const Report& report, int map_id, const TrailLookup& trail);

    // Drops everything: no pack is open.
    void clear();

    // Behaviors keep their state in it from one frame to the next, until it is built again.
    runtime::LiveMap& live() { return live_; }
    const runtime::LiveMap& live() const { return live_; }

    // Every marker and trail of the pack, baked: what the editor's overlay places its handles by.
    const BakedPathables& baked() const { return baked_; }

private:
    // Reads the points and map of every trail without them. True when any arrived.
    bool resolveTrails(const TrailLookup& trail);
    void rebuild(int map_id);

    bool open_ = false;
    uint64_t revision_ = 0;
    BakedPathables baked_;
    std::vector<char> shown_;
    bool unresolved_ = false; // a trail whose file waits for a later frame
    uint64_t builds_ = 0; // never reset, so a live map's revision is never reused
    runtime::LiveMap live_;
};

} // namespace viewer::edited
