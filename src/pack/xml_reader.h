#pragma once

#include <string>

#include "pack/model.h"
#include "pack/source.h"

namespace pack {

// Every .xml in the source as a Pack. Works the same for a zip and for an unzipped folder.
// Never fails: a file that cannot be read or parsed, and a dangling category reference, become
// warnings so the rest of a pack still opens.
Pack loadPack(const DataSource& source);

// Parses one already-read XML document into an existing Pack. `file_index` is the index
// of the matching entry in Pack::files.
bool readXmlFile(Pack* pack, size_t file_index);

} // namespace pack
