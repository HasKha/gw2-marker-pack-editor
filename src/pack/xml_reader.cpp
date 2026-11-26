#include "pack/xml_reader.h"

#include <pugixml.hpp>

#include <algorithm>
#include <unordered_map>

namespace pack {

namespace {

// parse_full keeps comments, processing instructions and the declaration — all of which
// carry meaning in a hand-authored pack and must survive a save.
//
// parse_ws_pcdata is deliberately NOT set: whitespace-only text nodes are the file's
// existing indentation, and keeping them would fight with the indentation pugixml applies
// on the way out. Dropping them lets the writer re-indent cleanly. See docs/format.md.
constexpr unsigned int kParseFlags = pugi::parse_full;

std::string lowered(std::string_view text) {
    return attributeKey(text);
}

// The things pugixml cannot recover from the tree, so we note them on the way in.
FileFormat formatOf(const std::string& text) {
    FileFormat format;
    format.bom = text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
                 static_cast<unsigned char>(text[2]) == 0xBF;
    format.crlf = text.find("\r\n") != std::string::npos;
    format.declaration = text.find("<?xml") != std::string::npos;
    return format;
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

    const std::string& path() const { return pack->files[file_index].path; }

    SourceNode anchor(const pugi::xml_node& node) const { return SourceNode{file_index, node}; }

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
        pack->categories[index].declarations.push_back(anchor(node));

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
        pathable.source = anchor(node);

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
    XmlFile& file = pack->files[file_index];
    const std::string& path = file.path;
    if (!file.document) return false;

    // The root is <OverlayData>, but be forgiving about what wraps it.
    bool found_root = false;
    for (pugi::xml_node child : file.document->children()) {
        if (child.type() != pugi::node_element) continue;
        if (lowered(child.name()) != "overlaydata") continue;

        Reader reader{pack, file_index};
        reader.readRoot(child);
        found_root = true;
    }

    if (!found_root) pack->addWarning(path, "No <OverlayData> root element");

    return true;
}

Pack loadPack(const DataSource& source) {
    Pack loaded;

    loaded.source_path = source.root();
    loaded.from_directory = source.isDirectory();

    std::vector<std::string> xml_entries = source.entriesWithExtension(".xml");
    // Deterministic order matters: a category declared in two files merges in load order,
    // and neither zip order nor directory-iteration order is guaranteed stable.
    std::sort(xml_entries.begin(), xml_entries.end());

    loaded.files.reserve(xml_entries.size());
    for (const std::string& entry : xml_entries) {
        XmlFile file;
        file.path = entry;
        file.original_path = source.originalName(entry);
        if (file.original_path.empty()) file.original_path = entry;

        std::string text;
        std::string read_error;
        if (!source.readText(entry, &text, &read_error)) {
            loaded.addWarning(entry, "Could not read: " + read_error);
            continue;
        }

        file.format = formatOf(text);

        // load_buffer copies, so the document owns its bytes and `text` can go away.
        file.document = std::make_unique<pugi::xml_document>();
        pugi::xml_parse_result result = file.document->load_buffer(text.data(), text.size(), kParseFlags);
        if (!result) {
            loaded.addWarning(
                entry,
                std::string("XML parse failed: ") + result.description() + " at offset " + std::to_string(result.offset));
            continue;
        }

        loaded.files.push_back(std::move(file));
    }

    for (size_t i = 0; i < loaded.files.size(); ++i) readXmlFile(&loaded, i);

    resolveCategories(&loaded);

    return loaded;
}

} // namespace pack
