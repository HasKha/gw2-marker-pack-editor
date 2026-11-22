#include "viewer/registry.h"

#include <ShlObj.h>

#include <algorithm>
#include <cctype>

#include "pack/trl.h"
#include "pack/xml_reader.h"

namespace viewer::registry {

namespace {

std::string lowercaseExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return extension;
}

} // namespace

std::filesystem::path blishMarkersFolder() {
    PWSTR documents = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documents))) return {};
    const std::filesystem::path path = std::filesystem::path(documents) / "Guild Wars 2" / "addons" / "blishhud" / "markers";
    CoTaskMemFree(documents);
    return path;
}

std::vector<std::filesystem::path> discoverPacks(const std::filesystem::path& folder) {
    std::vector<std::filesystem::path> archives;
    bool has_loose_xml = false;

    std::error_code ec;
    for (auto it = std::filesystem::recursive_directory_iterator(folder, std::filesystem::directory_options::skip_permission_denied, ec);
         !ec && it != std::filesystem::recursive_directory_iterator();
         it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string extension = lowercaseExtension(it->path());
        if (extension == ".zip" || extension == ".taco") archives.push_back(it->path());
        if (extension == ".xml") has_loose_xml = true;
    }

    std::sort(archives.begin(), archives.end());
    if (has_loose_xml) archives.push_back(folder);
    return archives;
}

LoadedPack loadPack(const std::filesystem::path& path) {
    LoadedPack loaded;
    loaded.path = path;

    loaded.source = pack::open(path, &loaded.error);
    if (!loaded.source) return loaded;

    pack::Pack model;
    if (!pack::loadPack(*loaded.source, &model, &loaded.error)) {
        loaded.source.reset();
        return loaded;
    }

    loaded.pathables = bake(model);
    loaded.categories = bakeCategories(model);

    // Every trail's points, now, on the worker: the map a trail is on is in its .trl header
    // and nowhere else, and the live map has to know it the moment the player arrives. Two
    // trails naming one file share its points.
    std::unordered_map<std::string, std::shared_ptr<const pack::trl::Trail>> read;
    for (BakedTrail& trail : loaded.pathables.trails) {
        const std::string key = pack::normalizePath(trail.data);
        auto [it, inserted] = read.try_emplace(key);
        if (inserted) {
            std::vector<uint8_t> bytes;
            std::string error;
            auto parsed = std::make_shared<pack::trl::Trail>();
            if (loaded.source->read(key, &bytes, &error) && pack::trl::read(bytes, parsed.get(), &error)) it->second = std::move(parsed);
        }
        trail.geometry = it->second;
        if (trail.geometry) trail.map_id = static_cast<int>(trail.geometry->map_id);
    }
    return loaded;
}

size_t CategoryTree::find(std::string_view id) const {
    const auto it = by_id.find(std::string(id));
    return it == by_id.end() ? pack::kNoIndex : it->second;
}

std::vector<size_t> mergeCategories(CategoryTree* tree, const std::vector<BakedCategory>& categories) {
    std::vector<size_t> node_of(categories.size(), pack::kNoIndex);

    // A pack's categories come parent first, so the parent's node is always known here.
    for (size_t i = 0; i < categories.size(); ++i) {
        const BakedCategory& baked = categories[i];

        size_t node = tree->find(baked.id);
        if (node == pack::kNoIndex) {
            node = tree->nodes.size();
            Category added;
            added.id = baked.id;
            added.label = baked.name;
            added.parent = baked.parent == pack::kNoIndex ? pack::kNoIndex : node_of[baked.parent];
            tree->nodes.push_back(std::move(added));
            tree->by_id.emplace(baked.id, node);
            if (tree->nodes[node].parent == pack::kNoIndex) {
                tree->roots.push_back(node);
            } else {
                tree->nodes[tree->nodes[node].parent].children.push_back(node);
            }
        }
        node_of[i] = node;

        Category& merged = tree->nodes[node];
        if (baked.display_name && !baked.display_name->empty()) merged.label = *baked.display_name;
        if (baked.is_separator) merged.is_separator = *baked.is_separator;
        if (baked.default_toggle) merged.default_toggle = *baked.default_toggle;
        if (baked.is_hidden) merged.is_hidden = *baked.is_hidden;
    }
    return node_of;
}

Loader::~Loader() {
    stop();
}

void Loader::start(std::vector<std::filesystem::path> paths) {
    stop();
    stopping_ = false;
    remaining_ = paths.size();

    worker_ = std::thread([this, paths = std::move(paths)] {
        for (const std::filesystem::path& path : paths) {
            if (stopping_) break;
            auto loaded = std::make_unique<LoadedPack>(loadPack(path));
            const std::lock_guard<std::mutex> lock(mutex_);
            done_.push_back(std::move(loaded));
        }
    });
}

void Loader::stop() {
    stopping_ = true;
    if (worker_.joinable()) worker_.join();

    const std::lock_guard<std::mutex> lock(mutex_);
    done_.clear();
    remaining_ = 0;
}

std::unique_ptr<LoadedPack> Loader::take() {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (done_.empty()) return nullptr;

    std::unique_ptr<LoadedPack> next = std::move(done_.front());
    done_.pop_front();
    --remaining_;
    return next;
}

bool Loader::busy() const {
    return remaining_ > 0;
}

} // namespace viewer::registry
