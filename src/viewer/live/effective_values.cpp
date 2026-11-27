#include "viewer/live/effective_values.h"

#include <unordered_map>

#include "pack/attributes.h"

namespace viewer {

namespace {

// The slot of each baked key, for the attributes a node holds, whose keys are only known at run time.
const std::unordered_map<std::string_view, size_t>& slots() {
    static const std::unordered_map<std::string_view, size_t> map = [] {
        std::unordered_map<std::string_view, size_t> out;
        for (size_t i = 0; i < kBakedKeyCount; ++i) out.emplace(kBakedKeys[i], i);
        return out;
    }();
    return map;
}

// The attribute table's defaults per slot, read once: finding one by name per read was a third of
// what the bake cost.
struct Defaults {
    std::array<float, kBakedKeyCount> numbers{};
    std::array<bool, kBakedKeyCount> flags{};
    std::array<int, kBakedKeyCount> integers{};
};

const Defaults& defaults() {
    static const Defaults table = [] {
        Defaults out;
        for (size_t i = 0; i < kBakedKeyCount; ++i) {
            out.numbers[i] = pack::defaultFloat(kBakedKeys[i]);
            out.flags[i] = pack::defaultBool(kBakedKeys[i]);
            out.integers[i] = pack::defaultInt(kBakedKeys[i]);
        }
        return out;
    }();
    return table;
}

// What `attributes` set over `out`: a node's own value wins over what it inherits.
void overlay(const pack::AttributeCollection& attributes, EffectiveValues* out) {
    for (const pack::Attribute& attribute : attributes.all()) {
        if (const auto it = slots().find(attribute.key); it != slots().end()) out->values[it->second] = &attribute.value;
    }
}

} // namespace

float EffectiveValues::number(BakedKey key) const {
    const std::string* text = value(key);
    const float fallback = defaults().numbers[key.slot];
    return text ? pack::parseFloat(*text).value_or(fallback) : fallback;
}

bool EffectiveValues::flag(BakedKey key) const {
    const std::string* text = value(key);
    const bool fallback = defaults().flags[key.slot];
    return text ? pack::parseBool(*text).value_or(fallback) : fallback;
}

int EffectiveValues::integer(BakedKey key) const {
    const std::string* text = value(key);
    const int fallback = defaults().integers[key.slot];
    return text ? pack::parseInt(*text).value_or(fallback) : fallback;
}

EffectiveValues effectiveValues(const pack::Pack& pack, const pack::Pathable& pathable) {
    EffectiveValues out;
    for (size_t i = 0; i < kBakedKeyCount; ++i) out.values[i] = pack.effectiveValue(pathable, kBakedKeys[i]);
    return out;
}

EffectiveTable::EffectiveTable(const pack::Pack& pack) {
    // A parent always comes before its children, since a category is only ever added under one
    // that exists, so its row is complete by the time a child copies it.
    rows.resize(pack.categories.size());
    for (size_t i = 0; i < pack.categories.size(); ++i) {
        const size_t parent = pack.categories[i].parent;
        if (parent != pack::kNoIndex) rows[i] = rows[parent];
        overlay(pack.categories[i].attributes, &rows[i]);
    }
}

EffectiveValues EffectiveTable::of(const pack::Pathable& pathable) const {
    EffectiveValues out = pathable.category == pack::kNoIndex ? EffectiveValues{} : rows[pathable.category];
    overlay(pathable.attributes, &out);
    return out;
}

} // namespace viewer
