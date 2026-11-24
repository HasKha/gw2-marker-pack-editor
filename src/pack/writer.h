#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "pack/model.h"
#include "pack/source.h"

namespace pack {

// Indentation for written XML. Two spaces matches what hand-authored packs use.
constexpr const char* kIndent = "  ";

// Serializes one XML file: the model's attributes written back into the parsed document, which
// still holds everything else, and the BOM and CRLF reapplied from FileFormat. Whitespace inside
// tags is lost, so a pack reformats once on its first save; a serializer rather than a splice into
// the original bytes is what lets nodes be added and removed.
bool writeXmlFile(const Pack& pack, size_t file_index, std::string* out, std::string* error);

struct SaveResult {
    bool ok = false;
    size_t files_rewritten = 0; // XML files whose bytes changed
    size_t entries_written = 0; // total entries in the output
    size_t assets_added = 0; // Pack::added_assets that made it out
    size_t assets_removed = 0; // Pack::removed_assets that were dropped
    std::vector<std::string> warnings;
    std::string error;
};

// Writes the new archive to `<target>.writing`: the XML, Pack::added_assets, and every other entry
// copied from `source` (null for a pack made in the editor), less Pack::removed_assets. commitZip
// is separate because the target is usually `source` itself, which the caller has to release
// before the rename.
SaveResult buildZip(const Pack& pack, const DataSource* source, const std::filesystem::path& target);

// Moves `<target>.writing` over `target`, keeping any previous file as `<target>.bak`.
bool commitZip(const std::filesystem::path& target, std::string* error);

// Throws away a build that will not be committed.
void abortZip(const std::filesystem::path& target);

// Writes every XML file back into a folder in place. Existing resources are left alone
// because they are already there; added assets are written and removed ones are deleted
// from disk, which for a folder pack is the one genuinely destructive part of a save.
SaveResult saveToDirectory(const Pack& pack, const std::filesystem::path& root);

} // namespace pack
