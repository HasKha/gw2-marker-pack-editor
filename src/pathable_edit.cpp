#include "pathable_edit.h"

#include "pack/attributes.h"

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

} // namespace

size_t addPoi(editor::State& state, const glm::vec3& position) {
    if (!state.loaded) return pack::kNoIndex;

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
    state.selection.set(added);
    state.requestReveal(added);
    return index;
}

} // namespace pathable_edit
