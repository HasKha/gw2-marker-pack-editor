#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "pack/model.h"
#include "pack/source.h"

namespace pack {

// Indentation for written XML. Two spaces matches what hand-authored packs use.
constexpr const char* kIndent = "  ";

// Serializes one XML file from the model.
//
// The model's attributes are written back into the parsed document, then the document is
// serialized. Comments, processing instructions, the declaration, attribute order and the
// original attribute spelling all survive because the document still holds them; the BOM
// and CRLF are reapplied from FileFormat.
//
// What is deliberately *not* preserved is whitespace inside a tag — wrapped attribute
// lists and `<POI ... />` come back on one line with no space before the slash. pugixml
// cannot represent that, so a pack reformats once on its first save. In exchange the
// writer is a plain serializer, which is what makes adding, removing and creating nodes
// possible at all. See docs/architecture.md.
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

// Saving a zip is two steps on purpose.
//
// buildZip writes the complete new archive to `<target>.writing`, reading everything the
// editor did not touch — icons, .trl files, the README — straight out of `source`, so a
// save never loses what it did not understand. `source` must still be open for that.
// Pack::added_assets are written in as well, and Pack::removed_assets are left out.
//
// commitZip then swaps it into place. It is separate because the usual target *is* the
// pack we have open, and on Windows a live read handle blocks the rename: the caller has
// to release the source in between. Building first also means a failure never leaves a
// half-written pack where a working one used to be.
SaveResult buildZip(const Pack& pack, const DataSource& source, const std::filesystem::path& target);

// Moves `<target>.writing` over `target`, keeping any previous file as `<target>.bak`.
bool commitZip(const std::filesystem::path& target, std::string* error);

// Throws away a build that will not be committed.
void abortZip(const std::filesystem::path& target);

// Writes every XML file back into a folder in place. Existing resources are left alone
// because they are already there; added assets are written and removed ones are deleted
// from disk, which for a folder pack is the one genuinely destructive part of a save.
SaveResult saveToDirectory(const Pack& pack, const std::filesystem::path& root);

} // namespace pack
