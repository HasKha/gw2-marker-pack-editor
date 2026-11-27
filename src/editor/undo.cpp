#include "editor/undo.h"

#include <algorithm>
#include <vector>

namespace undo {

namespace {

// How many steps to keep. Attribute records are tiny, but an added image or an edited
// `.trl` carries its bytes twice, so the stack is bounded rather than unbounded.
constexpr size_t kMaxSteps = 100;

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

Entry* openEntry(History& history) {
    return history.open_depth > 0 && history.open_index < history.steps.size() ? &history.steps[history.open_index] : nullptr;
}

// Reading the after-images is what closes a step. Records that turn out to change nothing
// are dropped, so a click that edits nothing does not land on the stack.
void closeEntry(editor::State& state) {
    History& history = state.history;
    Entry& entry = history.steps[history.open_index];

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
        history.steps.erase(history.steps.begin() + static_cast<long long>(history.open_index));
        history.position = history.steps.size();
        return;
    }

    entry.last_frame = history.frame;
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

void beginFrame(editor::State& state) {
    ++state.history.frame;
}

void clear(editor::State& state) {
    state.history = History{};
}

Step::Step(editor::State& state, std::string label, std::string merge_key)
    : state(state) {
    History& history = state.history;
    if (history.open_depth > 0) {
        // Nested: an operation built out of smaller ones is still one undo step.
        ++history.open_depth;
        return;
    }

    // Still holding the same handle, still the same interaction: keep filling in the step
    // that is already there rather than starting a new one per frame.
    std::vector<Entry>& steps = history.steps;
    const size_t position = history.position;
    const bool merges = !merge_key.empty() && position > 0 && position == steps.size() && steps[position - 1].merge_key == merge_key &&
                        steps[position - 1].last_frame + 1 >= history.frame;

    if (merges) {
        history.open_index = position - 1;
        history.open_depth = 1;
        return;
    }

    // Anything that was undone is now unreachable: the new edit branches off here.
    steps.erase(steps.begin() + static_cast<long long>(position), steps.end());

    if (steps.size() >= kMaxSteps) steps.erase(steps.begin());

    Entry entry;
    entry.label = std::move(label);
    entry.merge_key = std::move(merge_key);
    entry.last_frame = history.frame;
    steps.push_back(std::move(entry));

    history.open_index = steps.size() - 1;
    history.position = steps.size();
    history.open_depth = 1;
}

Step::~Step() {
    History& history = state.history;
    if (history.open_depth == 0) return;
    if (--history.open_depth > 0) return; // an inner guard; the outermost one closes the step
    if (history.open_index < history.steps.size()) closeEntry(state);
}

void attributes(editor::State& state, editor::NodeRef node) {
    Entry* entry = openEntry(state.history);
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
    Entry* entry = openEntry(state.history);
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
    Entry* entry = openEntry(state.history);
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
    Entry* entry = openEntry(state.history);
    if (!entry) return;

    for (const AssetRecord& record : entry->assets) {
        if (record.path == path) return;
    }

    AssetRecord record;
    record.path = path;
    record.before = assetStateOf(state.pack, path);
    entry->assets.push_back(std::move(record));
}

bool canUndo(const editor::State& state) {
    return state.history.position > 0;
}

bool canRedo(const editor::State& state) {
    return state.history.position < state.history.steps.size();
}

std::string undoLabel(const editor::State& state) {
    return canUndo(state) ? state.history.steps[state.history.position - 1].label : std::string();
}

std::string redoLabel(const editor::State& state) {
    return canRedo(state) ? state.history.steps[state.history.position].label : std::string();
}

void undo(editor::State& state) {
    History& history = state.history;
    if (!canUndo(state) || history.open_depth > 0) return;
    --history.position;
    applyEntry(state, history.steps[history.position], true);
    // A step that has been undone must not swallow the next edit.
    history.steps[history.position].merge_key.clear();
}

void redo(editor::State& state) {
    History& history = state.history;
    if (!canRedo(state) || history.open_depth > 0) return;
    applyEntry(state, history.steps[history.position], false);
    ++history.position;
}

} // namespace undo
