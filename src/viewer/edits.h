#pragma once

#include <filesystem>
#include <string>

// Personal copies: where the viewer's Edit takes a pack.
//
// A pack in Blish HUD's folder or in ours came from the catalog, or may have, and the next
// update of it would overwrite whatever was changed. So Edit never changes one in place: it
// copies it into `edits`, whose packs stand in for any other pack of the same name, and edits
// the copy. Deleting the copy brings the original back. The editor's File menu still opens any
// pack anywhere, in place.
namespace viewer::edits {

constexpr const char* FOLDER = "edits";

// What Edit opens for `pack`: the pack itself when it is in `edits_folder` already, else its
// copy there under the same name, which may not exist yet. Empty for a folder pack outside it:
// Blish HUD's markers folder is itself a pack when it holds loose XML, and copying it would
// copy every archive beside the XML too.
std::filesystem::path target(const std::filesystem::path& pack, const std::filesystem::path& edits_folder);

// Copies the archive `from` to `to`, creating `to`'s folder, by way of `<to>.part`, so a copy cut
// short never passes for a pack. Empty when it worked, else why not. Slow for a large pack: a
// worker's job.
std::string copy(const std::filesystem::path& from, const std::filesystem::path& to);

} // namespace viewer::edits
