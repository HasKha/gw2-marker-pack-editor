#include "undo.h"

#include <algorithm>
#include <vector>

namespace undo {

namespace {

// How many steps to keep. Attribute records are tiny, but an added image or an edited
// `.trl` carries its bytes twice, so the stack is bounded rather than unbounded.
constexpr size_t kMaxSteps = 100;

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

    bool operator==(const AssetState& other) const {
        return added == other.added && removed == other.removed && bytes == other.bytes && original_path == other.original_path;
    }
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

std::vector<Entry> steps;

// How many steps are applied. steps[0, position) are done; steps[position, end) are redo.
size_t position = 0;

// The step being filled in, while a Step guard is alive.
size_t open_index = 0;
int open_depth = 0;

uint64_t frame = 0;

// Attribute order and spelling are part of what round-trips, so "unchanged" has to mean
// the same attributes in the same order under the same names.
bool sameAttributes(const pack::AttributeCollection& a, const pack::AttributeCollection& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.all().size(); ++i) {
        if (a.all()[i].name != b.all()[i].name) return false;
        if (a.all()[i].value != b.all()[i].value) return false;
    }
    return true;
}

AssetState assetStateOf(const pack::Pack& pack, const std::string& path) {
    AssetState state;
    if (const pack::Pack::AddedAsset* added = pack.findAddedAsset(path)) {
        state.added = true;
        state.original_path = added->original_path;
        state.bytes = added->bytes;
    }
    state.removed = pack.isAssetRemoved(path);
    return state;
}

void applyAssetState(pack::Pack& pack, const std::string& path, const AssetState& state) {
    // Back to nothing pending, then whichever of the two it was. They are mutually
    // exclusive by construction: adding a file cancels its removal and vice versa.
    pack.dropAddedAsset(path);
    pack.restoreAsset(path);

    if (state.added) pack.addAsset(state.original_path.empty() ? path : state.original_path, state.bytes);
    if (state.removed) pack.removeAsset(path);
}

void setDeleted(editor::State& state, editor::NodeRef node, bool deleted) {
    if (node.kind == editor::NodeKind::Category) {
        if (deleted) {
            state.pack.removeCategorySubtree(node.index, nullptr);
        } else {
            state.pack.restoreCategorySubtree(node.index, nullptr);
        }
        return;
    }
    if (node.kind != editor::NodeKind::Pathable) return;

    if (deleted) {
        state.pack.removePathable(node.index);
    } else {
        state.pack.restorePathable(node.index);
    }
}

Entry* openEntry() {
    return open_depth > 0 && open_index < steps.size() ? &steps[open_index] : nullptr;
}

// Reading the after-images is what closes a step. Records that turn out to change nothing
// are dropped, so a click that edits nothing does not land on the stack.
void closeEntry(editor::State& state) {
    Entry& entry = steps[open_index];

    for (AttributeRecord& record : entry.attributes) {
        const pack::AttributeCollection* current = editor::attributesOf(state, record.node);
        record.after = current ? *current : pack::AttributeCollection{};
    }
    entry.attributes.erase(
        std::remove_if(
            entry.attributes.begin(),
            entry.attributes.end(),
            [](const AttributeRecord& record) { return sameAttributes(record.before, record.after); }),
        entry.attributes.end());

    for (ExistenceRecord& record : entry.existence) record.after_deleted = editor::isDeleted(state, record.node);
    entry.existence.erase(
        std::remove_if(
            entry.existence.begin(),
            entry.existence.end(),
            [](const ExistenceRecord& record) { return record.before_deleted == record.after_deleted; }),
        entry.existence.end());

    for (AssetRecord& record : entry.assets) record.after = assetStateOf(state.pack, record.path);
    entry.assets.erase(
        std::remove_if(entry.assets.begin(), entry.assets.end(), [](const AssetRecord& record) { return record.before == record.after; }),
        entry.assets.end());

    if (entry.empty()) {
        steps.erase(steps.begin() + static_cast<long long>(open_index));
        position = steps.size();
        return;
    }

    entry.last_frame = frame;
}

void applyEntry(editor::State& state, Entry& entry, bool undoing) {
    for (AttributeRecord& record : entry.attributes) {
        if (pack::AttributeCollection* attributes = editor::attributesOf(state, record.node)) {
            *attributes = undoing ? record.before : record.after;
        }
    }

    for (ExistenceRecord& record : entry.existence) {
        setDeleted(state, record.node, undoing ? record.before_deleted : record.after_deleted);
    }

    for (AssetRecord& record : entry.assets) applyAssetState(state.pack, record.path, undoing ? record.before : record.after);

    // Whatever was selected may have just stopped existing. Trail points are pruned by
    // trail_edit when the caller refreshes the working copy — see applyUndo.
    editor::prune(state);
    state.touch();
}

} // namespace

void beginFrame() {
    ++frame;
}

void clear() {
    steps.clear();
    position = 0;
    open_depth = 0;
}

Step::Step(editor::State& state, std::string label, std::string merge_key)
    : state_(&state) {
    if (open_depth > 0) {
        // Nested: an operation built out of smaller ones is still one undo step.
        ++open_depth;
        return;
    }

    // Still holding the same handle, still the same interaction: keep filling in the step
    // that is already there rather than starting a new one per frame.
    const bool merges = !merge_key.empty() && position > 0 && position == steps.size() && steps[position - 1].merge_key == merge_key &&
                        steps[position - 1].last_frame + 1 >= frame;

    if (merges) {
        open_index = position - 1;
        open_depth = 1;
        return;
    }

    // Anything that was undone is now unreachable: the new edit branches off here.
    steps.erase(steps.begin() + static_cast<long long>(position), steps.end());

    if (steps.size() >= kMaxSteps) steps.erase(steps.begin());

    Entry entry;
    entry.label = std::move(label);
    entry.merge_key = std::move(merge_key);
    entry.last_frame = frame;
    steps.push_back(std::move(entry));

    open_index = steps.size() - 1;
    position = steps.size();
    open_depth = 1;
}

Step::~Step() {
    if (open_depth == 0) return;
    if (--open_depth > 0) return; // an inner guard; the outermost one closes the step
    if (open_index < steps.size()) closeEntry(*state_);
}

void attributes(editor::State& state, editor::NodeRef node) {
    Entry* entry = openEntry();
    if (!entry) return;

    for (const AttributeRecord& record : entry->attributes) {
        if (record.node == node) return; // already captured, and the first image is the one
    }

    const pack::AttributeCollection* current = editor::attributesOf(state, node);
    if (!current) return;

    AttributeRecord record;
    record.node = node;
    record.before = *current;
    entry->attributes.push_back(std::move(record));
}

void existence(editor::State& state, editor::NodeRef node) {
    Entry* entry = openEntry();
    if (!entry) return;

    for (const ExistenceRecord& record : entry->existence) {
        if (record.node == node) return;
    }

    ExistenceRecord record;
    record.node = node;
    record.before_deleted = editor::isDeleted(state, node);
    entry->existence.push_back(record);
}

void created(editor::State& state, editor::NodeRef node) {
    (void)state;

    Entry* entry = openEntry();
    if (!entry) return;

    for (const ExistenceRecord& record : entry->existence) {
        if (record.node == node) return;
    }

    // It did not exist before the step, which for a tombstoned model is the same state as
    // deleted: undoing the creation removes it, redoing brings it back.
    ExistenceRecord record;
    record.node = node;
    record.before_deleted = true;
    entry->existence.push_back(record);
}

void asset(editor::State& state, const std::string& path) {
    Entry* entry = openEntry();
    if (!entry) return;

    for (const AssetRecord& record : entry->assets) {
        if (record.path == path) return;
    }

    AssetRecord record;
    record.path = path;
    record.before = assetStateOf(state.pack, path);
    entry->assets.push_back(std::move(record));
}

bool canUndo() {
    return position > 0;
}

bool canRedo() {
    return position < steps.size();
}

std::string undoLabel() {
    return canUndo() ? steps[position - 1].label : std::string();
}

std::string redoLabel() {
    return canRedo() ? steps[position].label : std::string();
}

void undo(editor::State& state) {
    if (!canUndo() || open_depth > 0) return;
    --position;
    applyEntry(state, steps[position], true);
    // A step that has been undone must not swallow the next edit.
    steps[position].merge_key.clear();
}

void redo(editor::State& state) {
    if (!canRedo() || open_depth > 0) return;
    applyEntry(state, steps[position], false);
    ++position;
}

} // namespace undo
