#include "viewer/packs/registry.h"

#include <ShlObj.h>

#include <algorithm>
#include <cctype>
#include <set>

#include "pack/trl.h"
#include "pack/xml_reader.h"
#include "util/path_text.h"
#include "viewer/render/trail_colour.h"

namespace viewer::registry {

namespace {

std::string lowercaseExtension(const std::filesystem::path& path) {
    std::string extension = util::pathText(path.extension());
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

bool samePack(const std::filesystem::path& a, const std::filesystem::path& b) {
    if (a.empty() || b.empty()) return false;
    std::error_code ec;
    // equivalent() needs both to exist; a pack deleted from disk is still the same path.
    return std::filesystem::equivalent(a, b, ec) || a.lexically_normal() == b.lexically_normal();
}

bool isInside(const std::filesystem::path& path, const std::filesystem::path& folder) {
    if (folder.empty()) return false;
    const std::filesystem::path relative = path.lexically_normal().lexically_relative(folder.lexically_normal());
    return !relative.empty() && *relative.begin() != "..";
}

std::filesystem::file_time_type writtenAt(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::is_directory(path, ec)) {
        const std::filesystem::file_time_type written = std::filesystem::last_write_time(path, ec);
        return ec ? std::filesystem::file_time_type{} : written;
    }
    // Stepped with the error_code overload: a range-for's step throws, and this runs on the frame
    // while a save may be rewriting the folder.
    std::filesystem::file_time_type newest{};
    for (auto it = std::filesystem::directory_iterator(path, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        if (lowercaseExtension(it->path()) != ".xml" || !it->is_regular_file(ec)) continue;
        const std::filesystem::file_time_type written = it->last_write_time(ec);
        if (!ec && written > newest) newest = written;
    }
    return newest;
}

LoadedPack loadPack(const std::filesystem::path& path) {
    LoadedPack loaded;
    loaded.path = path;
    loaded.written = writtenAt(path);

    std::unique_ptr<pack::DataSource> source = pack::open(path, &loaded.error);
    if (!source) return loaded;

    pack::Pack model = pack::loadPack(*source);
    loaded.source = std::move(source);

    for (const std::string& key : loaded.source->entriesWithExtension(".lua")) {
        std::string text;
        std::string error;
        if (loaded.source->readText(key, &text, &error)) loaded.scripts.emplace(key, std::move(text));
    }

    // A script's strings too: what it prints, and the names of the markers and menus it makes.
    loaded.glyphs = ui::glyphs::of(model);
    for (const auto& [key, text] : loaded.scripts) loaded.glyphs.add(text);

    loaded.pathables = bake(model);
    loaded.categories = bakeCategories(model);
    loaded.declared.categories = std::move(model.categories);
    loaded.declared.roots = std::move(model.roots);
    // Their nodes point into documents that go with `model`.
    for (pack::Category& category : loaded.declared.categories) category.declarations.clear();

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

    // The colour of every trail without a tint, from its texture, here too: Pathing samples as
    // it loads, and decoding is a worker's job. One texture is sampled once however many
    // trails name it; one that will not read is NO_COLOUR, as Pathing's failed load is.
    std::unordered_map<std::string, ImU32> sampled;
    for (BakedTrail& trail : loaded.pathables.trails) {
        if (!trail.sample_pending) continue;
        const std::string key = pack::normalizePath(trail.texture);
        auto [it, inserted] = sampled.try_emplace(key, trail_colour::NO_COLOUR);
        if (inserted) {
            std::vector<uint8_t> bytes;
            std::string error;
            if (loaded.source->read(key, &bytes, &error)) it->second = trail_colour::sample(bytes);
        }
        trail.sample_colour = it->second;
        trail.sample_pending = false;
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
            added.name = baked.name;
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
        if (baked.achievement_id) {
            merged.achievement_id = baked.achievement_id;
            merged.achievement_bit = baked.achievement_bit;
        }
    }
    return node_of;
}

Loader::~Loader() {
    stop();
}

size_t Loader::workerCount() {
    const unsigned cores = std::thread::hardware_concurrency();
    return std::clamp<size_t>(cores / 2, 1, 4);
}

void Loader::add(std::vector<std::filesystem::path> paths) {
    {
        const std::lock_guard<std::mutex> lock(mutex);
        for (std::filesystem::path& path : paths) queued.emplace_back(next_added++, std::move(path));
        stopping = false;
    }
    wake.notify_all();

    if (!workers.empty()) return;
    for (size_t i = 0; i < workerCount(); ++i) workers.emplace_back([this] { work(); });
}

void Loader::work() {
    std::unique_lock<std::mutex> lock(mutex);
    while (true) {
        wake.wait(lock, [this] { return stopping || !queued.empty(); });
        if (stopping) return;

        const auto [order, path] = std::move(queued.front());
        queued.pop_front();
        ++loading;

        // Unlocked while it reads, which is seconds for a large pack: take() and add() are
        // called every frame and must not wait for it.
        lock.unlock();
        // Nothing may escape a worker, where it is std::terminate: a pack that throws is a pack
        // that did not load, and says why.
        std::unique_ptr<LoadedPack> loaded;
        try {
            loaded = std::make_unique<LoadedPack>(loadPack(path));
        } catch (const std::exception& error) {
            loaded = std::make_unique<LoadedPack>();
            loaded->path = path;
            // As loadPack sets it, or the pack would be read again every time changes are looked for.
            loaded->written = writtenAt(path);
            loaded->error = error.what();
        }
        lock.lock();

        --loading;
        if (!stopping) done.emplace(order, std::move(loaded));
    }
}

void Loader::stop() {
    {
        const std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
        queued.clear();
        done.clear();
        next_taken = next_added;
    }
    wake.notify_all();
    for (std::thread& worker : workers) worker.join();
    workers.clear();
}

std::unique_ptr<LoadedPack> Loader::take() {
    const std::lock_guard<std::mutex> lock(mutex);
    const auto next = done.find(next_taken);
    if (next == done.end()) return nullptr;
    std::unique_ptr<LoadedPack> taken = std::move(next->second);
    done.erase(next);
    ++next_taken;
    return taken;
}

bool Loader::busy() const {
    const std::lock_guard<std::mutex> lock(mutex);
    return !queued.empty() || loading > 0 || !done.empty();
}

} // namespace viewer::registry
