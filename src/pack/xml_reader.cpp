#include "pack/xml_reader.h"

#include <pugixml.hpp>

#include <algorithm>
#include <unordered_map>

namespace pack {

namespace {

// parse_full keeps comments, processing instructions and the declaration; parse_ws_pcdata
// keeps whitespace-only text, which is what indentation is. Together they let the writer
// reproduce the file. See docs/format.md.
constexpr unsigned int kParseFlags = pugi::parse_full | pugi::parse_ws_pcdata;

std::string lowered(std::string_view text) {
    return attributeKey(text);
}

// Offset one past the '>' that ends this element's *start tag*. That is the span the
// writer needs: rewriting attributes never touches an element's children.
size_t findStartTagEnd(const std::string& text, size_t begin) {
    bool in_quotes = false;
    char quote = '\0';

    for (size_t at = begin; at < text.size(); ++at) {
        const char c = text[at];
        if (in_quotes) {
            if (c == quote) in_quotes = false;
            continue;
        }
        if (c == '"' || c == '\'') {
            in_quotes = true;
            quote = c;
            continue;
        }
        if (c == '>') return at + 1;
    }
    return text.size();
}

SourceSpan spanOf(const pugi::xml_node& node, const std::string& text, size_t file_index) {
    SourceSpan span;
    span.file = file_index;
    // offset_debug() points at the tag name, one past '<'.
    const ptrdiff_t at = node.offset_debug();
    span.begin = at > 0 ? static_cast<size_t>(at) - 1 : 0;
    span.end = findStartTagEnd(text, span.begin);
    return span;
}

void collectAttributes(const pugi::xml_node& node, AttributeCollection* out) {
    for (pugi::xml_attribute attribute : node.attributes()) {
        out->set(attribute.name(), attribute.value());
    }
}

// Category ids are compared case-insensitively and written dot-joined. Surrounding
// whitespace shows up in hand-authored packs often enough to be worth trimming.
std::string categorySegment(std::string_view raw) {
    return lowered(raw);
}

struct Reader {
    Pack* pack = nullptr;
    size_t file_index = kNoIndex;

    const std::string& text() const { return pack->files[file_index].text; }
    const std::string& path() const { return pack->files[file_index].path; }

    // Finds or creates the category at `parent`/`name`, merging into any existing
    // declaration of the same id.
    size_t upsertCategory(const pugi::xml_node& node, size_t parent) {
        const char* raw_name = node.attribute("name").value();
        std::string key = categorySegment(raw_name);

        if (key.empty()) {
            pack->addWarning(path(), "MarkerCategory without a name attribute; skipped along with its children");
            return kNoIndex;
        }

        std::string id = parent == kNoIndex ? key : pack->categories[parent].id + "." + key;

        size_t index = kNoIndex;
        for (size_t i = 0; i < pack->categories.size(); ++i) {
            if (pack->categories[i].id == id) {
                index = i;
                break;
            }
        }

        if (index == kNoIndex) {
            Category category;
            category.name = raw_name;
            category.key = key;
            category.id = id;
            category.parent = parent;
            index = pack->categories.size();
            pack->categories.push_back(std::move(category));

            if (parent == kNoIndex) {
                pack->roots.push_back(index);
            } else {
                pack->categories[parent].children.push_back(index);
            }
        }

        // A category split across files contributes attributes from each declaration;
        // the later one wins on a conflict, matching how the readers merge.
        collectAttributes(node, &pack->categories[index].attributes);
        pack->categories[index].declarations.push_back(spanOf(node, text(), file_index));

        return index;
    }

    void readCategoryTree(const pugi::xml_node& node, size_t parent) {
        const size_t index = upsertCategory(node, parent);
        if (index == kNoIndex) return;

        for (pugi::xml_node child : node.children()) {
            if (child.type() != pugi::node_element) continue;
            if (lowered(child.name()) == "markercategory") readCategoryTree(child, index);
        }
    }

    void readPathable(const pugi::xml_node& node, PathableType type) {
        Pathable pathable;
        pathable.type = type;
        collectAttributes(node, &pathable.attributes);
        pathable.source = spanOf(node, text(), file_index);

        if (const std::string* category = pathable.attributes.value("type")) {
            pathable.category_id = lowered(*category);
        }

        if (pathable.category_id.empty()) {
            pack->addWarning(path(), std::string(toString(type)) + " without a type attribute");
        }

        pack->pathables.push_back(std::move(pathable));
    }

    void readPois(const pugi::xml_node& node) {
        for (pugi::xml_node child : node.children()) {
            if (child.type() != pugi::node_element) continue;

            const std::string name = lowered(child.name());
            if (name == "poi") {
                readPathable(child, PathableType::Poi);
            } else if (name == "trail") {
                readPathable(child, PathableType::Trail);
            } else if (name == "route") {
                readPathable(child, PathableType::Route);
            } else {
                pack->addWarning(path(), "Unknown element <" + std::string(child.name()) + "> inside <POIs>");
            }
        }
    }

    void readRoot(const pugi::xml_node& root) {
        for (pugi::xml_node child : root.children()) {
            if (child.type() != pugi::node_element) continue;

            const std::string name = lowered(child.name());
            if (name == "markercategory") {
                readCategoryTree(child, kNoIndex);
            } else if (name == "pois") {
                readPois(child);
            } else {
                pack->addWarning(path(), "Unknown element <" + std::string(child.name()) + "> under <OverlayData>");
            }
        }
    }
};

void resolveCategories(Pack* pack) {
    std::unordered_map<std::string_view, size_t> by_id;
    by_id.reserve(pack->categories.size());
    for (size_t i = 0; i < pack->categories.size(); ++i) by_id.emplace(pack->categories[i].id, i);

    for (Pathable& pathable : pack->pathables) {
        if (pathable.category_id.empty()) continue;
        auto it = by_id.find(pathable.category_id);
        pathable.category = it == by_id.end() ? kNoIndex : it->second;
    }
}

} // namespace

bool readXmlFile(Pack* pack, size_t file_index) {
    const std::string& text = pack->files[file_index].text;
    const std::string& path = pack->files[file_index].path;

    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_buffer(text.data(), text.size(), kParseFlags);
    if (!result) {
        pack->addWarning(path, std::string("XML parse failed: ") + result.description() + " at offset " + std::to_string(result.offset));
        return false;
    }

    // The root is <OverlayData>, but be forgiving about what wraps it.
    bool found_root = false;
    for (pugi::xml_node child : doc.children()) {
        if (child.type() != pugi::node_element) continue;
        if (lowered(child.name()) != "overlaydata") continue;

        Reader reader{pack, file_index};
        reader.readRoot(child);
        found_root = true;
    }

    if (!found_root) pack->addWarning(path, "No <OverlayData> root element");

    return true;
}

bool loadPack(const zip::Archive& archive, Pack* out, std::string* error) {
    *out = Pack{};

    if (!archive.isOpen()) {
        if (error) *error = "Archive is not open";
        return false;
    }

    out->source_file = archive.sourceFile();

    std::vector<std::string> xml_entries = archive.entriesWithExtension(".xml");
    // Deterministic order matters: a category declared in two files merges in load order,
    // and archive order is not guaranteed to be stable across zip writers.
    std::sort(xml_entries.begin(), xml_entries.end());

    out->files.reserve(xml_entries.size());
    for (const std::string& entry : xml_entries) {
        XmlFile file;
        file.path = entry;

        std::string read_error;
        if (!archive.readText(entry, &file.text, &read_error)) {
            out->addWarning(entry, "Could not read from archive: " + read_error);
            continue;
        }

        out->files.push_back(std::move(file));
    }

    for (size_t i = 0; i < out->files.size(); ++i) readXmlFile(out, i);

    resolveCategories(out);

    return true;
}

} // namespace pack
