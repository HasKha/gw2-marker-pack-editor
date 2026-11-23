#pragma once

#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "viewer/utc.h"

// The public pack catalog Pathing installs from (MarkerPackRepo/): what it lists, which of its
// packs are on disk, and which of those are older than the catalog's.
namespace viewer::catalog {

// MarkerPackRepo.PUBLIC_REPOURL.
constexpr const char* REPO_URL = "https://mp-repo.blishhud.com/repo.json";

// The per-pack "Keep updated" choices, in the addon's folder.
constexpr const char* STATE_FILE_NAME = "catalog.json";

// PackHandlingUtil.DOWNLOAD_UA: "at least one pack will deny us from downloading it without a
// more typical UA set".
constexpr const wchar_t* kDownloadUserAgent =
    L"Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/51.0.2704.103 Safari/537.36";

// One entry of the catalog: MarkerPackPkg, less what Pathing keeps there about its own state.
struct Package {
    std::string name;
    std::string description;
    std::string download; // the archive's URL
    std::string info; // a page about the pack; may be empty
    std::string file_name; // what the archive is saved as
    std::string categories; // comma-separated, as the catalog writes them
    std::string author;
    float size_mb = 0.0f;
    int total_downloads = 0; // about this many a day, as Pathing's tooltip says
    UtcMillis last_update = 0; // 0 when the catalog gives none
};

// The catalog's packages, in its order. An entry is left out when its file name is not one
// safe to write — see safeFileName — or it has no web address to download from. Nothing when
// the body is not a list.
std::optional<std::vector<Package>> parse(std::string_view json);

// Whether the catalog's `name` may be written into our folder: a bare .zip or .taco file
// name, no folder in it. The catalog is someone else's file, and its FileName is a path.
bool safeFileName(std::string_view name);

// A pack on disk, as far as the catalog cares.
struct LocalPack {
    std::filesystem::path path;
    UtcMillis written = 0; // last written, which an update makes now
};

// The packs at `paths`, as discovery found them. A folder pack is left out: the catalog only
// ships archives.
std::vector<LocalPack> localPacks(const std::vector<std::filesystem::path>& paths);

// The local copy of `package`: the pack whose file name, less its extension, is the
// package's, ignoring case. Pathing takes the first file whose path merely contains that name,
// which finds TehsTrails in TehsTrails-HeroPoints.taco.
const LocalPack* findLocal(const Package& package, const std::vector<LocalPack>& locals);

enum class State {
    Absent, // not on disk
    Current,
    Stale, // the catalog's is newer: Pathing's LastUpdate > CurrentDownloadDate
};

State state(const Package& package, const LocalPack* local);

// Pathing's per-pack "Keep Updated", on unless the player turned it off: only those are kept.
class AutoUpdate
{
public:
    bool enabled(const std::string& package) const { return !off_.count(package); }
    void set(const std::string& package, bool on);

    void load(const std::filesystem::path& file);
    bool save(const std::filesystem::path& file) const;

private:
    std::set<std::string> off_;
};

} // namespace viewer::catalog
