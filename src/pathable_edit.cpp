#include "pathable_edit.h"

#include <vector>

#include "pack/attributes.h"
#include "undo.h"

namespace pathable_edit {

namespace {

// Sets an attribute under the spelling the attribute table prescribes, so a marker the
// editor writes looks like one an author wrote.
void set(pack::AttributeCollection* attributes, std::string_view key, std::string value) {
    const pack::AttributeDef* def = pack::findAttribute(key);
    attributes->set(def ? def->display : key, std::move(value));
}

// Everything on `source` that a POI can carry. A trail's traildata or trailscale would be
// dead weight on a marker, so the attribute table decides what comes along. Attributes the
// table does not know are copied: dropping what we do not recognise is the one thing the
// pack layer never does.
pack::AttributeCollection copyForPoi(const pack::AttributeCollection& source) {
    pack::AttributeCollection copy;
    for (const pack::Attribute& attribute : source.all()) {
        const pack::AttributeDef* def = pack::findAttribute(attribute.key);
        if (def && (def->applies_to & pack::kAppliesPoi) == 0) continue;
        copy.set(attribute.name, attribute.value);
    }
    return copy;
}

// The file a category's markers should go in when it has none yet: the one its last
// declaration is in, which is where a reader would expect to find them.
size_t fileForCategory(const editor::State& state, size_t category) {
    if (category >= state.pack.categories.size()) return pack::kNoIndex;

    const std::vector<pack::SourceNode>& declarations = state.pack.categories[category].declarations;
    return declarations.empty() ? pack::kNoIndex : declarations.back().file;
}

// Every live category at or under `root`, root first.
std::vector<size_t> subtree(const editor::State& state, size_t root) {
    if (root >= state.pack.categories.size() || state.pack.categories[root].grave.deleted) return {};

    std::vector<size_t> found{root};
    for (size_t at = 0; at < found.size(); ++at) {
        for (size_t child : state.pack.categories[found[at]].children) {
            if (child < state.pack.categories.size() && !state.pack.categories[child].grave.deleted) found.push_back(child);
        }
    }
    return found;
}

// The live markers filed under any of `categories`.
std::vector<size_t> pathablesIn(const editor::State& state, const std::vector<size_t>& categories) {
    std::vector<size_t> found;
    for (size_t category : categories) {
        if (category >= state.pathables_by_category.size()) continue;
        for (size_t index : state.pathables_by_category[category]) {
            if (!state.pack.pathables[index].grave.deleted) found.push_back(index);
        }
    }
    return found;
}

} // namespace

Removal wouldRemove(const editor::State& state, editor::NodeRef node) {
    Removal removal;

    if (node.kind == editor::NodeKind::Category) {
        const std::vector<size_t> categories = subtree(state, node.index);
        removal.categories = categories.size();
        removal.pathables = pathablesIn(state, categories).size();
    } else if (node.kind == editor::NodeKind::Pathable && node.index < state.pack.pathables.size()) {
        if (!state.pack.pathables[node.index].grave.deleted) removal.pathables = 1;
    }

    return removal;
}

Removal remove(editor::State& state, editor::NodeRef node) {
    Removal removal;
    if (!state.loaded) return removal;

    undo::Step step(state, node.kind == editor::NodeKind::Category ? "Delete category" : "Delete marker");

    if (node.kind == editor::NodeKind::Category) {
        // The markers first: they live in <POIs>, outside the category's own element, so
        // removing the category takes its nested child categories but not them. Collected
        // before anything is tombstoned, since the walk skips tombstones.
        const std::vector<size_t> categories = subtree(state, node.index);
        if (categories.empty()) return removal;

        for (size_t index : pathablesIn(state, categories)) {
            undo::existence(state, editor::NodeRef{editor::NodeKind::Pathable, index});
            state.pack.removePathable(index);
            ++removal.pathables;
        }

        // Only the root: restoring it brings its descendants back with it, so recording
        // them separately would try to restore the same elements twice.
        undo::existence(state, node);

        std::vector<size_t> gone;
        state.pack.removeCategorySubtree(node.index, &gone);
        removal.categories = gone.size();
    } else if (node.kind == editor::NodeKind::Pathable && node.index < state.pack.pathables.size()) {
        if (state.pack.pathables[node.index].grave.deleted) return removal;
        undo::existence(state, node);
        state.pack.removePathable(node.index);
        removal.pathables = 1;
    } else {
        return removal;
    }

    state.selection.clear();
    state.touch();
    return removal;
}

size_t addPoi(editor::State& state, const glm::vec3& position) {
    if (!state.loaded) return pack::kNoIndex;

    undo::Step step(state, "Add POI");

    const editor::NodeRef node = state.selection.primary();

    size_t category = pack::kNoIndex;
    size_t after = pack::kNoIndex;
    size_t file = pack::kNoIndex;
    pack::AttributeCollection attributes;

    if (node.kind == editor::NodeKind::Category && node.index < state.pack.categories.size()) {
        category = node.index;
        set(&attributes, "type", state.pack.categories[category].id);

        // Behind the category's last marker, so "add" means "add to the end of what is
        // already there" rather than "put it wherever the file happens to end".
        const std::vector<size_t>& siblings = state.pathables_by_category[category];
        if (!siblings.empty()) after = siblings.back();
        file = fileForCategory(state, category);
    } else if (node.kind == editor::NodeKind::Pathable && node.index < state.pack.pathables.size()) {
        const pack::Pathable& source = state.pack.pathables[node.index];
        category = source.category;
        after = node.index;
        file = source.source.file;
        attributes = copyForPoi(source.attributes);
    } else {
        return pack::kNoIndex;
    }

    set(&attributes, "xpos", pack::formatFloat(position.x));
    set(&attributes, "ypos", pack::formatFloat(position.y));
    set(&attributes, "zpos", pack::formatFloat(position.z));
    // Its own identity, never the copied one: a GUID is what per-marker state is keyed on,
    // so two markers sharing one would toggle each other away.
    set(&attributes, "guid", pack::generateGuid());

    const size_t index = state.pack.addPathable(pack::PathableType::Poi, category, after, file, std::move(attributes));
    if (index == pack::kNoIndex) return pack::kNoIndex;

    state.pathableAdded(index, after);

    const editor::NodeRef added{editor::NodeKind::Pathable, index};
    undo::created(state, added);
    state.selection.set(added);
    state.requestReveal(added);
    return index;
}

} // namespace pathable_edit
