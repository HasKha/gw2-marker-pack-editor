#pragma once

#include <string>

#include "pack/model.h"
#include "pack/zip.h"

namespace pack {

// Loads every .xml in the archive into a Pack. Never fails on a bad node: unparseable
// files and dangling category references become warnings so the rest of a pack still
// opens. Returns false only when the archive itself cannot be read.
bool loadPack(const zip::Archive& archive, Pack* out, std::string* error);

// Parses one already-read XML document into an existing Pack. `file_index` is the index
// of the matching entry in Pack::files.
bool readXmlFile(Pack* pack, size_t file_index);

} // namespace pack
