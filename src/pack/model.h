#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "pack/attributes.h"

namespace pack {

constexpr size_t kNoIndex = static_cast<size_t>(-1);

// One attribute exactly as it appeared in the file.
//
// The value is kept as text and never reformatted. That is deliberate: reparsing
// xpos="-118.300" into a float and writing it back would emit "-118.3" and dirty a file
// the user never edited. Typed access goes through the parse* helpers on demand.
struct Attribute {
    std::string name; // original spelling, e.g. "fadeNear"
    std::string key; // lowercased lookup key, e.g. "fadenear"
    std::string value; // verbatim
};

// Source order is preserved, so this is a vector rather than a map. Attribute counts per
// node are small enough that linear lookup is not worth optimizing.
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

// Byte range of a node within its source file, so the writer can splice a replacement in
// rather than reserializing the document and losing intra-tag whitespace.
struct SourceSpan {
    size_t file = kNoIndex; // index into Pack::files
    size_t begin = 0; // offset of '<'
    size_t end = 0; // one past the closing '>'
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
    std::vector<SourceSpan> declarations;

    std::string displayName() const;
};

enum class PathableType { Poi, Trail, Route };

const char* toString(PathableType type);

struct Pathable {
    PathableType type = PathableType::Poi;
    std::string category_id; // lowercased `type` attribute
    size_t category = kNoIndex; // resolved index, kNoIndex when the type is dangling
    AttributeCollection attributes;
    SourceSpan source;
};

struct XmlFile {
    std::string path; // normalized archive entry name
    std::string text; // original bytes, verbatim — the writer splices into this
};

// A warning that does not stop the load. Packs in the wild have plenty of these.
struct Warning {
    std::string file;
    std::string message;
};

struct Pack {
    std::filesystem::path source_file;
    std::vector<XmlFile> files;
    std::vector<Category> categories; // flat; tree via parent/children indices
    std::vector<size_t> roots;
    std::vector<Pathable> pathables;
    std::vector<Warning> warnings;

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
};

} // namespace pack
