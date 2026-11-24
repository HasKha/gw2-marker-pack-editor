#pragma once

#include <pugixml.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "pack/attributes.h"

namespace pack {

constexpr size_t kNoIndex = static_cast<size_t>(-1);

// One attribute exactly as it appeared in the file. The value stays text, so xpos="-118.300" is
// never rewritten as "-118.3" in a file nobody edited.
struct Attribute {
    std::string name; // original spelling, e.g. "fadeNear"
    std::string key; // lowercased lookup key, e.g. "fadenear"
    std::string value; // verbatim
};

// In source order, so a vector rather than a map.
class AttributeCollection
{
public:
    const Attribute* find(std::string_view key) const;
    const std::string* value(std::string_view key) const;

    // Adds or replaces. `name` supplies the spelling for a newly added attribute; an
    // existing one keeps the spelling already in the file.
    void set(std::string_view name, std::string value);
    bool erase(std::string_view key);

    const std::vector<Attribute>& all() const { return attributes_; }
    bool empty() const { return attributes_.empty(); }
    size_t size() const { return attributes_.size(); }

private:
    std::vector<Attribute> attributes_;
};

// Where a model node lives in the parsed document, which lives as long as the pack.
struct SourceNode {
    size_t file = kNoIndex; // index into Pack::files
    pugi::xml_node node;
};

// Deleting leaves the slot behind, so no index into `categories` or `pathables` ever moves and undo
// is a flag flip. Everything that walks either list skips these.
// Where an element sat, so undo can put it back in its place rather than at the end.
struct Anchor {
    pugi::xml_node parent;
    pugi::xml_node previous; // the sibling it followed; null when it was first
};

struct Tombstone {
    bool deleted = false;

    // The element, lifted whole out of the file's document and kept: putting it back is a
    // copy rather than a reconstruction, so comments and spelling survive a delete/undo.
    // Only set once deleted.
    std::unique_ptr<pugi::xml_document> detached;

    // One per element in `detached` — a category can be declared in several files.
    std::vector<Anchor> anchors;
};

struct Category {
    std::string name; // as written, e.g. "Gorseval"
    std::string key; // lowercased
    std::string id; // full dot path, lowercase, e.g. "haskha.w1.gorseval"
    AttributeCollection attributes;

    size_t parent = kNoIndex;
    std::vector<size_t> children;

    // A category may be declared in several files, or several times in one. Each
    // declaration contributes attributes and has to be editable in place.
    std::vector<SourceNode> declarations;

    Tombstone grave;

    std::string displayName() const;
};

enum class PathableType { Poi, Trail, Route };

const char* toString(PathableType type);

struct Pathable {
    PathableType type = PathableType::Poi;
    std::string category_id; // lowercased `type` attribute
    size_t category = kNoIndex; // resolved index, kNoIndex when the type is dangling
    AttributeCollection attributes;
    SourceNode source;
    Tombstone grave;
};

// How the file was written, so a save can put it back the same way. These are the only
// things pugixml cannot tell us from the tree alone.
struct FileFormat {
    bool bom = false;
    bool crlf = false;
    bool declaration = false;
};

struct XmlFile {
    std::string path; // normalized entry name, used for lookups
    std::string original_path; // the name as the pack spells it; what a save writes back to
    FileFormat format;

    // The parsed document, owned for the lifetime of the pack. Saving serializes this, and
    // adding or removing a marker is a node insert or erase on it.
    std::unique_ptr<pugi::xml_document> document;
};

// A warning that does not stop the load. Packs in the wild have plenty of these.
struct Warning {
    std::string file;
    std::string message;
};

// Move-only: it owns the parsed documents the model's handles point into.
struct Pack {
    Pack() = default;
    Pack(Pack&&) = default;
    Pack& operator=(Pack&&) = default;
    Pack(const Pack&) = delete;
    Pack& operator=(const Pack&) = delete;

    // The .zip/.taco file, or the folder, this pack was read from.
    std::filesystem::path source_path;

    // source_path is a folder: a save writes files in place rather than rebuilding an archive.
    bool from_directory = false;

    std::vector<XmlFile> files;
    std::vector<Category> categories; // flat; tree via parent/children indices
    std::vector<size_t> roots;
    std::vector<Pathable> pathables;
    std::vector<Warning> warnings;

    // ── Pending asset changes ─────────────────────────────────────────────────
    // Non-XML files added or marked for deletion, until a save: the DataSource is read-only.
    struct AddedAsset {
        std::string path; // normalized, for lookups
        std::string original_path; // the spelling a save writes
        std::vector<uint8_t> bytes;
    };

    std::vector<AddedAsset> added_assets;
    std::vector<std::string> removed_assets; // normalized

    const AddedAsset* findAddedAsset(std::string_view path) const;
    bool isAssetRemoved(std::string_view path) const;

    // Adds, or replaces an add already pending for the same path. Also cancels a pending
    // removal of it: putting a file back is a replacement, not both at once.
    void addAsset(std::string original_path, std::vector<uint8_t> bytes);

    // Marks a file for deletion on the next save. A file that only ever existed as a
    // pending add just goes away.
    void removeAsset(std::string_view path);

    // Withdraws a pending add without marking anything for deletion — "forget my edits to
    // this file", which leaves whatever the source holds untouched.
    bool dropAddedAsset(std::string_view path);
    void restoreAsset(std::string_view path);
    bool hasAssetChanges() const { return !added_assets.empty() || !removed_assets.empty(); }
    void clearAssetChanges();

    const Category* findCategory(std::string_view id) const;
    Category* findCategory(std::string_view id);

    // Value set on the node itself, ignoring inheritance.
    const std::string* explicitValue(const Pathable& pathable, std::string_view key) const;

    // Value after walking up the category chain, with the node's own value winning.
    // Mirrors TmfLib's GetAggregatedAttributeValue.
    const std::string* effectiveValue(const Pathable& pathable, std::string_view key) const;
    const std::string* effectiveValue(const Category& category, std::string_view key) const;

    // Typed convenience wrappers that fall back to the attribute table's default.
    float effectiveFloat(const Pathable& pathable, std::string_view key) const;
    bool effectiveBool(const Pathable& pathable, std::string_view key) const;
    int effectiveInt(const Pathable& pathable, std::string_view key) const;

    size_t countByType(PathableType type) const;
    void addWarning(std::string file, std::string message);

    // Creates a pathable: a new element in the document plus the model entry that indexes
    // it. Returns its index in `pathables`, or kNoIndex when there is nowhere to put it.
    //
    // Appended to `pathables`. In the file it goes right behind `after`'s element, else at the end
    // of a <POIs> block in `file`, created if needed. Tree order is State::pathableAdded's.
    size_t addPathable(PathableType type, size_t category, size_t after, size_t file, AttributeCollection attributes);

    // Creates a category under `parent` (kNoIndex for a root one) and returns its index, or
    // kNoIndex when the name is unusable or already taken at that level.
    //
    // Appended to `categories`, its element nested inside the parent's declaration, so `file` only
    // matters for a root. `after`, a live sibling under the same parent, places it right behind
    // that sibling in the XML and in `children` (or `roots`); otherwise it goes last.
    size_t addCategory(size_t parent, std::string_view name, size_t file, size_t after = kNoIndex);

    // Tombstones a pathable and lifts its element out of the document.
    void removePathable(size_t index);

    // Tombstones a category and its nested categories, whose elements go with it. Markers live in
    // <POIs> and are the caller's to remove. `out_removed` receives every category tombstoned.
    void removeCategorySubtree(size_t index, std::vector<size_t>* out_removed);

    // The other half of both, for undo. The element goes back where it was — same parent,
    // same sibling — and the model is repointed at it. Restoring a category also re-points
    // every descendant's declaration, since they came back nested inside the copy and their
    // old handles died with the original.
    void restorePathable(size_t index);
    void restoreCategorySubtree(size_t index, std::vector<size_t>* out_restored);
};

// An empty pack in memory: one XML file with an `<OverlayData>` and one root category. It has no
// DataSource, so it is saved with Save As, and anything reading through a source copes without.
Pack createPack(std::string_view file_name, std::string_view category_name, std::string_view display_name);

} // namespace pack
