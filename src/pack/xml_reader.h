#pragma once

#include <string>

#include "pack/model.h"
#include "pack/source.h"

namespace pack {

// Loads every .xml in the source into a Pack. Works the same for a zip and for an
// unzipped folder. Never fails on a bad node: unparseable files and dangling category
// references become warnings so the rest of a pack still opens. Returns false only when
// the source itself cannot be read.
bool loadPack(const DataSource& source, Pack* out, std::string* error);

// Parses one already-read XML document into an existing Pack. `file_index` is the index
// of the matching entry in Pack::files.
bool readXmlFile(Pack* pack, size_t file_index);

} // namespace pack
