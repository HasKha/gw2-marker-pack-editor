#include "pack/writer.h"

#include <miniz.h>

#include <algorithm>
#include <fstream>
#include <unordered_map>

namespace pack {

namespace {

// Pushes the model's attributes back onto the document node they came from.
//
// Order follows the model, which preserves the order the file used and appends anything
// newly added. Names the user removed disappear because the node is cleared first.
void syncAttributes(pugi::xml_node node, const AttributeCollection& attributes) {
    while (pugi::xml_attribute first = node.first_attribute()) node.remove_attribute(first);

    for (const Attribute& attribute : attributes.all()) {
        node.append_attribute(attribute.name.c_str()) = attribute.value.c_str();
    }
}

// A category can be declared in several files and the model holds the *merged* result, so
// a declaration only owns the names it originally spelled out. Values are refreshed,
// removed names disappear, and anything newly added lands on the last declaration — the
// one that wins when the pack is read back.
void syncCategoryDeclaration(pugi::xml_node node, const AttributeCollection& model, bool is_last_declaration) {
    std::vector<std::string> original_names;
    for (pugi::xml_attribute attribute : node.attributes()) original_names.emplace_back(attribute.name());

    while (pugi::xml_attribute first = node.first_attribute()) node.remove_attribute(first);

    std::vector<std::string> written;
    for (const std::string& name : original_names) {
        const Attribute* current = model.find(name);
        if (!current) continue; // removed by the user
        node.append_attribute(current->name.c_str()) = current->value.c_str();
        written.push_back(current->key);
    }

    if (!is_last_declaration) return;

    for (const Attribute& attribute : model.all()) {
        if (std::find(written.begin(), written.end(), attribute.key) != written.end()) continue;
        node.append_attribute(attribute.name.c_str()) = attribute.value.c_str();
    }
}

struct StringWriter : pugi::xml_writer {
    std::string result;
    void write(const void* data, size_t size) override { result.append(static_cast<const char*>(data), size); }
};

std::string toCrlf(const std::string& text) {
    std::string out;
    out.reserve(text.size() + text.size() / 20);
    for (char c : text) {
        if (c == '\n') out.push_back('\r');
        out.push_back(c);
    }
    return out;
}

} // namespace

bool writeXmlFile(const Pack& pack, size_t file_index, std::string* out, std::string* error) {
    if (file_index >= pack.files.size()) {
        if (error) *error = "No such file index";
        return false;
    }

    const XmlFile& file = pack.files[file_index];
    if (!file.document) {
        if (error) *error = file.path + " has no parsed document";
        return false;
    }

    for (const Category& category : pack.categories) {
        for (size_t d = 0; d < category.declarations.size(); ++d) {
            const SourceNode& declaration = category.declarations[d];
            if (declaration.file != file_index || !declaration.node) continue;

            const bool is_last = d + 1 == category.declarations.size();
            syncCategoryDeclaration(declaration.node, category.attributes, is_last);
        }
    }

    for (const Pathable& pathable : pack.pathables) {
        if (pathable.source.file != file_index || !pathable.source.node) continue;
        syncAttributes(pathable.source.node, pathable.attributes);
    }

    unsigned int flags = pugi::format_indent;
    if (!file.format.declaration) flags |= pugi::format_no_declaration;
    if (file.format.bom) flags |= pugi::format_write_bom;

    StringWriter writer;
    file.document->save(writer, kIndent, flags);

    *out = file.format.crlf ? toCrlf(writer.result) : std::move(writer.result);
    return true;
}

namespace {

// Serialized bytes for every XML file, keyed by normalized path.
struct RewrittenFiles {
    std::unordered_map<std::string, std::string> by_path;
    bool ok = true;
    std::string error;
};

RewrittenFiles rewriteAll(const Pack& pack) {
    RewrittenFiles out;
    for (size_t i = 0; i < pack.files.size(); ++i) {
        std::string text;
        if (!writeXmlFile(pack, i, &text, &out.error)) {
            out.ok = false;
            return out;
        }
        out.by_path.emplace(pack.files[i].path, std::move(text));
    }
    return out;
}

std::filesystem::path pendingPath(const std::filesystem::path& target) {
    std::filesystem::path pending = target;
    pending += ".writing";
    return pending;
}

} // namespace

SaveResult buildZip(const Pack& pack, const DataSource& source, const std::filesystem::path& target) {
    SaveResult result;

    RewrittenFiles rewritten = rewriteAll(pack);
    if (!rewritten.ok) {
        result.error = rewritten.error;
        return result;
    }
    result.files_rewritten = rewritten.by_path.size();

    // Build beside the target so a failure never leaves a half-written pack in its place.
    const std::filesystem::path temporary = pendingPath(target);

    mz_zip_archive zip{};
    if (!mz_zip_writer_init_file(&zip, temporary.string().c_str(), 0)) {
        result.error = "Could not create " + temporary.string();
        return result;
    }

    bool failed = false;
    for (const std::string& entry : source.entries()) {
        std::string name = source.originalName(entry);
        if (name.empty()) name = entry;

        const std::string* replacement = nullptr;
        auto it = rewritten.by_path.find(entry);
        if (it != rewritten.by_path.end()) replacement = &it->second;

        std::vector<uint8_t> bytes;
        const void* data = nullptr;
        size_t size = 0;

        if (replacement) {
            data = replacement->data();
            size = replacement->size();
        } else {
            std::string read_error;
            if (!source.read(entry, &bytes, &read_error)) {
                result.warnings.push_back("Skipped " + entry + ": " + read_error);
                continue;
            }
            data = bytes.data();
            size = bytes.size();
        }

        if (!mz_zip_writer_add_mem(&zip, name.c_str(), data, size, MZ_DEFAULT_COMPRESSION)) {
            result.error = "Could not write " + name + " to the archive";
            failed = true;
            break;
        }
        ++result.entries_written;
    }

    if (!failed && !mz_zip_writer_finalize_archive(&zip)) {
        result.error = "Could not finalize the archive";
        failed = true;
    }
    mz_zip_writer_end(&zip);

    if (failed) {
        std::error_code ec;
        std::filesystem::remove(temporary, ec);
        return result;
    }

    result.ok = true;
    return result;
}

bool commitZip(const std::filesystem::path& target, std::string* error) {
    const std::filesystem::path temporary = pendingPath(target);

    std::error_code ec;
    if (!std::filesystem::exists(temporary, ec)) {
        if (error) *error = "Nothing was built for " + target.string();
        return false;
    }

    if (std::filesystem::exists(target, ec)) {
        std::filesystem::path backup = target;
        backup += ".bak";
        std::filesystem::remove(backup, ec);
        std::filesystem::rename(target, backup, ec);
        if (ec) {
            if (error) *error = "Could not move the previous pack aside: " + ec.message();
            return false;
        }
    }

    std::filesystem::rename(temporary, target, ec);
    if (ec) {
        if (error) *error = "Could not move the new pack into place: " + ec.message();
        return false;
    }

    return true;
}

void abortZip(const std::filesystem::path& target) {
    std::error_code ec;
    std::filesystem::remove(pendingPath(target), ec);
}

SaveResult saveToDirectory(const Pack& pack, const std::filesystem::path& root) {
    SaveResult result;

    RewrittenFiles rewritten = rewriteAll(pack);
    if (!rewritten.ok) {
        result.error = rewritten.error;
        return result;
    }
    result.files_rewritten = rewritten.by_path.size();

    for (size_t i = 0; i < pack.files.size(); ++i) {
        auto it = rewritten.by_path.find(pack.files[i].path);
        if (it == rewritten.by_path.end()) continue;

        // The name the pack spells it with, not our lowercased lookup key.
        const std::filesystem::path destination = root / pack.files[i].original_path;

        std::error_code ec;
        std::filesystem::create_directories(destination.parent_path(), ec);

        std::ofstream file(destination, std::ios::binary | std::ios::trunc);
        if (!file) {
            result.error = "Could not write " + destination.string();
            return result;
        }
        file.write(it->second.data(), static_cast<std::streamsize>(it->second.size()));
        if (!file) {
            result.error = "Could not write " + destination.string();
            return result;
        }
        ++result.entries_written;
    }

    result.ok = true;
    return result;
}

} // namespace pack
