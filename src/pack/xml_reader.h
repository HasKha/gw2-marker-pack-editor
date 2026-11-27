#pragma once

#include <string>

#include "pack/model.h"
#include "pack/source.h"

namespace pack {

// Every .xml in the source as a Pack. Works the same for a zip and for an unzipped folder.
// Never fails: a file that cannot be read or parsed, and a dangling category reference, become
// warnings so the rest of a pack still opens.
Pack loadPack(const DataSource& source);

// Builds the model from the documents already parsed into `pack->files`, in their order, into a
// pack with no categories or pathables yet.
void readDocuments(Pack* pack);

} // namespace pack
