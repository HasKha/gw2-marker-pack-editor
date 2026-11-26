#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "editor/node_ref.h"
#include "pack/model.h"

// What undo keeps: before/after images of attributes, existence and files. editor::State holds a
// History and undo.h is everything that reads or writes it.
namespace undo {

struct AttributeRecord {
    editor::NodeRef node;
    pack::AttributeCollection before;
    pack::AttributeCollection after;
};

struct ExistenceRecord {
    editor::NodeRef node;
    bool before_deleted = false;
    bool after_deleted = false;
};

// Everything the pack knows about one file that is not in the DataSource: pending add,
// pending removal, or neither.
struct AssetState {
    bool added = false;
    std::string original_path;
    std::vector<uint8_t> bytes;
    bool removed = false;

    bool operator==(const AssetState& other) const = default;
};

struct AssetRecord {
    std::string path;
    AssetState before;
    AssetState after;
};

struct Entry {
    std::string label;
    std::string merge_key;
    uint64_t last_frame = 0;

    std::vector<AttributeRecord> attributes;
    std::vector<ExistenceRecord> existence;
    std::vector<AssetRecord> assets;

    bool empty() const { return attributes.empty() && existence.empty() && assets.empty(); }
};

struct History {
    std::vector<Entry> steps;

    // How many steps are applied. steps[0, position) are done; steps[position, end) are redo.
    size_t position = 0;

    // The step being filled in, while a Step guard is alive.
    size_t open_index = 0;
    int open_depth = 0;

    // Bumped by beginFrame, so a step can tell it is reopened on the next frame.
    uint64_t frame = 0;
};

} // namespace undo
