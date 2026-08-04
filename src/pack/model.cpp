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
    return static_cast<size_t>(
        std::count_if(pathables.begin(), pathables.end(), [&](const Pathable& p) { return p.type == type && !p.grave.deleted; }));
}

void Pack::addWarning(std::string file, std::string message) {
    warnings.push_back(Warning{std::move(file), std::move(message)});
}

namespace {

// The element name to write for a new pathable. Packs spell these in mixed case and the
// reader is case-insensitive, so this only has to be readable and consistent.
const char* elementName(PathableType type) {
    switch (type) {
    case PathableType::Trail: return "Trail";
    case PathableType::Route: return "Route";
    case PathableType::Poi: break;
    }
    return "POI";
}

// The file's <OverlayData>. Null when it has none, which the reader would already have
// warned about.
pugi::xml_node overlayRoot(pugi::xml_document* document) {
    if (!document) return {};

    for (pugi::xml_node child : document->children()) {
        if (child.type() != pugi::node_element) continue;
        if (attributeKey(child.name()) == "overlaydata") return child;
    }
    return {};
}

// The <POIs> block a new element goes into, created at the end of <OverlayData> when the
// file has none. Returns a null node when the document has no root to hang it off.
pugi::xml_node poisBlock(pugi::xml_document* document) {
    pugi::xml_node root = overlayRoot(document);
    if (!root) return {};

    for (pugi::xml_node child : root.children()) {
        if (child.type() == pugi::node_element && attributeKey(child.name()) == "pois") return child;
    }
    return root.append_child("POIs");
}

} // namespace

size_t Pack::addPathable(PathableType type, size_t category, size_t after, size_t file, AttributeCollection attributes) {
    pugi::xml_node node;
    size_t target_file = file;

    if (after < pathables.size() && pathables[after].source.node) {
        // Directly behind its sibling, so the file reads in the order the editor shows.
        const SourceNode& sibling = pathables[after].source;
        node = sibling.node.parent().insert_child_after(sibling.node.name(), sibling.node);
        target_file = sibling.file;
    } else {
        if (target_file >= files.size()) return kNoIndex;
        pugi::xml_node pois = poisBlock(files[target_file].document.get());
        if (!pois) return kNoIndex;
        node = pois.append_child(elementName(type));
    }

    if (!node) return kNoIndex;

    Pathable pathable;
    pathable.type = type;
    pathable.attributes = std::move(attributes);
    pathable.category = category;
    if (const std::string* id = pathable.attributes.value("type")) pathable.category_id = attributeKey(*id);
    pathable.source = SourceNode{target_file, node};

    pathables.push_back(std::move(pathable));
    return pathables.size() - 1;
}

namespace {

// Moves `node` out of its document into one the tombstone owns, remembering where it was.
void detach(pugi::xml_node node, Tombstone* grave) {
    if (!grave->detached) grave->detached = std::make_unique<pugi::xml_document>();
    if (!node) return;

    grave->anchors.push_back(Anchor{node.parent(), node.previous_sibling()});
    grave->detached->append_copy(node);
    node.parent().remove_child(node);
}

// Puts the `index`-th detached element back where it came from. Null when the anchor no
// longer exists — its parent was removed too — in which case the caller decides.
pugi::xml_node reattach(const Tombstone& grave, size_t index) {
    if (!grave.detached || index >= grave.anchors.size()) return {};

    pugi::xml_node element = grave.detached->first_child();
    for (size_t i = 0; i < index && element; ++i) element = element.next_sibling();
    if (!element) return {};

    // By value: a pugi handle is a pointer in a wrapper, and the insert methods are
    // non-const on it.
    pugi::xml_node parent = grave.anchors[index].parent;
    pugi::xml_node previous = grave.anchors[index].previous;
    if (!parent) return {};
    return previous ? parent.insert_copy_after(element, previous) : parent.prepend_copy(element);
}

// Finds the declaration handles of every category nested inside `node`, which has just come
// back as a copy. Matched by name against the model's own tree — the same lookup the reader
// does, so anything that was readable is findable again.
void repointDeclarations(Pack& pack, size_t category, pugi::xml_node node, size_t file) {
    if (category >= pack.categories.size()) return;

    for (pugi::xml_node child : node.children()) {
        if (child.type() != pugi::node_element) continue;
        if (attributeKey(child.name()) != "markercategory") continue;

        const std::string key = attributeKey(child.attribute("name").value());
        if (key.empty()) continue;

        for (size_t index : pack.categories[category].children) {
            if (index >= pack.categories.size() || pack.categories[index].key != key) continue;

            for (SourceNode& declaration : pack.categories[index].declarations) {
                if (declaration.file != file || declaration.node) continue;
                declaration.node = child;
                break;
            }
            repointDeclarations(pack, index, child, file);
            break;
        }
    }
}

} // namespace

Pack createPack(std::string_view file_name, std::string_view category_name, std::string_view display_name) {
    Pack pack;

    XmlFile file;
    file.path = normalizePath(file_name);
    file.original_path = std::string(file_name);
    // What a hand-authored pack looks like on this platform. The BOM is left off: nothing
    // needs it, and it is the kind of thing that surprises a text editor later.
    file.format.declaration = true;
    file.format.crlf = true;
    file.document = std::make_unique<pugi::xml_document>();

    pugi::xml_node declaration = file.document->append_child(pugi::node_declaration);
    declaration.append_attribute("version") = "1.0";
    declaration.append_attribute("encoding") = "utf-8";
    file.document->append_child("OverlayData");

    pack.files.push_back(std::move(file));

    const size_t root = pack.addCategory(kNoIndex, category_name, 0);
    if (root != kNoIndex && !display_name.empty()) {
        // Model-only: the writer appends anything the declaration does not already spell
        // out, and this is the category's last (and only) declaration.
        pack.categories[root].attributes.set("DisplayName", std::string(display_name));
    }

    return pack;
}

size_t Pack::addCategory(size_t parent, std::string_view name, size_t file) {
    const std::string key = attributeKey(name);
    // A dot would split the id into two levels that do not exist, and an empty segment
    // would give the category no name at all.
    if (key.empty() || key.find('.') != std::string::npos) return kNoIndex;

    const std::string id = parent < categories.size() ? categories[parent].id + "." + key : key;
    if (findCategory(id) != nullptr) return kNoIndex;

    pugi::xml_node node;
    size_t target_file = file;

    if (parent < categories.size()) {
        // Inside the parent's declaration: nesting is what makes it a child. Prefer the
        // requested file, but any declaration will do rather than nothing.
        pugi::xml_node under;
        for (const SourceNode& declaration : categories[parent].declarations) {
            if (!declaration.node) continue;
            if (!under || declaration.file == file) {
                under = declaration.node;
                target_file = declaration.file;
            }
            if (declaration.file == file) break;
        }
        if (!under) return kNoIndex;
        node = under.append_child("MarkerCategory");
    } else {
        if (target_file >= files.size()) return kNoIndex;
        pugi::xml_node root = overlayRoot(files[target_file].document.get());
        if (!root) return kNoIndex;

        // Before the markers, where a reader expects the categories to be.
        pugi::xml_node pois;
        for (pugi::xml_node child : root.children()) {
            if (child.type() == pugi::node_element && attributeKey(child.name()) == "pois") {
                pois = child;
                break;
            }
        }
        node = pois ? root.insert_child_before("MarkerCategory", pois) : root.append_child("MarkerCategory");
    }

    if (!node) return kNoIndex;
    node.append_attribute("name") = std::string(name).c_str();

    const size_t resolved_parent = parent < categories.size() ? parent : kNoIndex;

    Category category;
    category.name = std::string(name);
    category.key = key;
    category.id = id;
    category.parent = resolved_parent;
    category.attributes.set("name", std::string(name));
    category.declarations.push_back(SourceNode{target_file, node});

    const size_t index = categories.size();
    categories.push_back(std::move(category));

    if (resolved_parent == kNoIndex) {
        roots.push_back(index);
    } else {
        categories[resolved_parent].children.push_back(index);
    }

    return index;
}

void Pack::removePathable(size_t index) {
    if (index >= pathables.size() || pathables[index].grave.deleted) return;

    Pathable& pathable = pathables[index];
    detach(pathable.source.node, &pathable.grave);
    // Point the model at the copy: the original is gone, and a handle into a destroyed node
    // is a crash waiting for whatever walks the list next.
    pathable.source.node = pathable.grave.detached->first_child();
    pathable.grave.deleted = true;
}

void Pack::restorePathable(size_t index) {
    if (index >= pathables.size() || !pathables[index].grave.deleted) return;

    Pathable& pathable = pathables[index];
    pugi::xml_node restored = reattach(pathable.grave, 0);

    // The place it came from is gone. Landing at the end of a <POIs> block beats not coming
    // back at all — the file's order is a convenience, the marker is the content.
    if (!restored && pathable.source.file < files.size()) {
        pugi::xml_node pois = poisBlock(files[pathable.source.file].document.get());
        if (pois) restored = pois.append_copy(pathable.grave.detached->first_child());
    }
    if (!restored) return;

    pathable.source.node = restored;
    pathable.grave = Tombstone{};
}

void Pack::removeCategorySubtree(size_t index, std::vector<size_t>* out_removed) {
    if (index >= categories.size() || categories[index].grave.deleted) return;

    std::vector<size_t> subtree{index};
    for (size_t at = 0; at < subtree.size(); ++at) {
        for (size_t child : categories[subtree[at]].children) {
            if (child < categories.size() && !categories[child].grave.deleted) subtree.push_back(child);
        }
    }

    // Every declaration, since a category can be declared in several files and each one
    // nests that file's share of the children.
    Category& root = categories[index];
    for (SourceNode& declaration : root.declarations) {
        if (!declaration.node) continue;
        detach(declaration.node, &root.grave);
        declaration.node = {};
    }
    root.grave.deleted = true;

    // The descendants' elements died with the root's, so their handles are dangling.
    for (size_t i = 1; i < subtree.size(); ++i) {
        Category& child = categories[subtree[i]];
        for (SourceNode& declaration : child.declarations) declaration.node = {};
        child.grave.deleted = true;
    }

    if (out_removed) *out_removed = std::move(subtree);
}

void Pack::restoreCategorySubtree(size_t index, std::vector<size_t>* out_restored) {
    if (index >= categories.size() || !categories[index].grave.deleted) return;

    Category& root = categories[index];

    // Each declaration goes back into its own file. Order matches: they were detached in
    // declaration order and appended to the tombstone in the same one.
    size_t at = 0;
    for (SourceNode& declaration : root.declarations) {
        pugi::xml_node restored = reattach(root.grave, at);
        ++at;
        if (!restored) continue;

        declaration.node = restored;
        // Its children came back nested inside the copy, so their handles have to be found
        // again — the ones they had died with the original elements.
        repointDeclarations(*this, index, restored, declaration.file);
    }

    root.grave = Tombstone{};

    std::vector<size_t> subtree{index};
    for (size_t i = 0; i < subtree.size(); ++i) {
        for (size_t child : categories[subtree[i]].children) {
            if (child >= categories.size() || !categories[child].grave.deleted) continue;
            categories[child].grave = Tombstone{};
            subtree.push_back(child);
        }
    }

    if (out_restored) *out_restored = std::move(subtree);
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
    if (dropAddedAsset(path)) return;

    const std::string key = normalizePath(path);
    if (std::find(removed_assets.begin(), removed_assets.end(), key) == removed_assets.end()) removed_assets.push_back(key);
}

bool Pack::dropAddedAsset(std::string_view path) {
    const std::string key = normalizePath(path);

    auto pending = std::find_if(added_assets.begin(), added_assets.end(), [&](const AddedAsset& asset) { return asset.path == key; });
    if (pending == added_assets.end()) return false;

    added_assets.erase(pending);
    return true;
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
