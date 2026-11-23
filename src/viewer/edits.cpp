#include "viewer/edits.h"

#include "viewer/registry.h"

namespace viewer::edits {

std::filesystem::path target(const std::filesystem::path& pack, const std::filesystem::path& edits_folder) {
    if (registry::isInside(pack, edits_folder)) return pack;
    std::error_code ec;
    if (std::filesystem::is_directory(pack, ec)) return {};
    return edits_folder / pack.filename();
}

std::string copy(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::filesystem::path part = to;
    part += ".part";

    std::error_code ec;
    std::filesystem::create_directories(to.parent_path(), ec);
    if (ec) return "could not make " + to.parent_path().string() + ": " + ec.message();

    std::filesystem::copy_file(from, part, std::filesystem::copy_options::overwrite_existing, ec);
    if (!ec) std::filesystem::rename(part, to, ec);
    if (!ec) return {};

    const std::string error = "could not copy " + from.filename().string() + ": " + ec.message();
    std::filesystem::remove(part, ec);
    return error;
}

} // namespace viewer::edits
