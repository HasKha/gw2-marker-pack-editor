#include "pack/model.h"

#include <algorithm>

#include "pack/source.h" // normalizePath, so asset paths key the same way lookups do

namespace pack {

const Attribute* AttributeCollection::find(std::string_view key) const {
    const std::string normalized = attributeKey(key);
    auto it = std::find_if(attributes_.begin(), attributes_.end(), [&](const Attribute& attribute) { return attribute.key == normalized; });
    return it == attributes_.end() ? nullptr : &*it;
}

const std::string* AttributeCollection::value(std::string_view key) const {
    const Attribute* attribute = find(key);
    return attribute ? &attribute->value : nullptr;
}

void AttributeCollection::set(std::string_view name, std::string value) {
    const std::string key = attributeKey(name);
    auto it = std::find_if(attributes_.begin(), attributes_.end(), [&](const Attribute& attribute) { return attribute.key == key; });
    if (it != attributes_.end()) {
        // Keep the spelling already in the file; the author chose it.
        it->value = std::move(value);
        return;
    }
    attributes_.push_back(Attribute{std::string(name), key, std::move(value)});
}

bool AttributeCollection::erase(std::string_view key) {
    const std::string normalized = attributeKey(key);
    auto it = std::find_if(attributes_.begin(), attributes_.end(), [&](const Attribute& attribute) { return attribute.key == normalized; });
    if (it == attributes_.end()) return false;
    attributes_.erase(it);
    return true;
}

std::string Category::displayName() const {
    if (const std::string* display = attributes.value("displayname")) {
        if (!display->empty()) return *display;
    }
    return name;
}

const char* toString(PathableType type) {
    switch (type) {
    case PathableType::Poi: return "POI";
    case PathableType::Trail: return "Trail";
    case PathableType::Route: return "Route";
    }
    return "Unknown";
}

const Category* Pack::findCategory(std::string_view id) const {
    const std::string key = attributeKey(id);
    auto it = std::find_if(categories.begin(), categories.end(), [&](const Category& c) { return c.id == key; });
    return it == categories.end() ? nullptr : &*it;
}

Category* Pack::findCategory(std::string_view id) {
    return const_cast<Category*>(static_cast<const Pack*>(this)->findCategory(id));
}

const std::string* Pack::explicitValue(const Pathable& pathable, std::string_view key) const {
    return pathable.attributes.value(key);
}

const std::string* Pack::effectiveValue(const Pathable& pathable, std::string_view key) const {
    if (const std::string* own = pathable.attributes.value(key)) return own;
    if (pathable.category == kNoIndex) return nullptr;
    return effectiveValue(categories[pathable.category], key);
}

const std::string* Pack::effectiveValue(const Category& category, std::string_view key) const {
    for (const Category* current = &category; current != nullptr;) {
        if (const std::string* value = current->attributes.value(key)) return value;
        current = current->parent == kNoIndex ? nullptr : &categories[current->parent];
    }
    return nullptr;
}

float Pack::effectiveFloat(const Pathable& pathable, std::string_view key) const {
    const float fallback = defaultFloat(key);
    const std::string* text = effectiveValue(pathable, key);
    float value = fallback;
    if (text && parseFloat(*text, &value)) return value;
    return fallback;
}

bool Pack::effectiveBool(const Pathable& pathable, std::string_view key) const {
    const bool fallback = defaultBool(key);
    const std::string* text = effectiveValue(pathable, key);
    bool value = fallback;
    if (text && parseBool(*text, &value)) return value;
    return fallback;
}

int Pack::effectiveInt(const Pathable& pathable, std::string_view key) const {
    const int fallback = defaultInt(key);
    const std::string* text = effectiveValue(pathable, key);
    int value = fallback;
    if (text && parseInt(*text, &value)) return value;
    return fallback;
}

size_t Pack::countByType(PathableType type) const {
    return static_cast<size_t>(std::count_if(pathables.begin(), pathables.end(), [&](const Pathable& p) { return p.type == type; }));
}

void Pack::addWarning(std::string file, std::string message) {
    warnings.push_back(Warning{std::move(file), std::move(message)});
}

const Pack::AddedAsset* Pack::findAddedAsset(std::string_view path) const {
    const std::string key = normalizePath(path);
    auto it = std::find_if(added_assets.begin(), added_assets.end(), [&](const AddedAsset& asset) { return asset.path == key; });
    return it == added_assets.end() ? nullptr : &*it;
}

bool Pack::isAssetRemoved(std::string_view path) const {
    const std::string key = normalizePath(path);
    return std::find(removed_assets.begin(), removed_assets.end(), key) != removed_assets.end();
}

void Pack::addAsset(std::string original_path, std::vector<uint8_t> bytes) {
    const std::string key = normalizePath(original_path);
    restoreAsset(key);

    auto it = std::find_if(added_assets.begin(), added_assets.end(), [&](const AddedAsset& asset) { return asset.path == key; });
    if (it != added_assets.end()) {
        it->original_path = std::move(original_path);
        it->bytes = std::move(bytes);
        return;
    }

    added_assets.push_back(AddedAsset{key, std::move(original_path), std::move(bytes)});
}

void Pack::removeAsset(std::string_view path) {
    const std::string key = normalizePath(path);

    auto pending = std::find_if(added_assets.begin(), added_assets.end(), [&](const AddedAsset& asset) { return asset.path == key; });
    if (pending != added_assets.end()) {
        added_assets.erase(pending);
        return;
    }

    if (std::find(removed_assets.begin(), removed_assets.end(), key) == removed_assets.end()) removed_assets.push_back(key);
}

void Pack::restoreAsset(std::string_view path) {
    const std::string key = normalizePath(path);
    removed_assets.erase(std::remove(removed_assets.begin(), removed_assets.end(), key), removed_assets.end());
}

void Pack::clearAssetChanges() {
    added_assets.clear();
    removed_assets.clear();
}

} // namespace pack
