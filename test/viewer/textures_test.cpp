// The texture cache against a fake Nexus: what it asks Nexus for, under which identifier,
// and what it remembers. A real Nexus hands back the existing texture for an identifier it
// has seen, which is the behaviour the fake keeps and the cache has to work around.

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>
#include <vector>

#include "viewer/textures.h"

namespace {

// Nexus's half, as a test can see it. One instance at a time: the API is a table of plain
// function pointers, so the fake has to reach its state through a global.
struct FakeNexus {
    std::map<std::string, Texture> created; // by identifier, as Nexus keeps them
    std::vector<std::string> requests;
    bool device_ready = true;

    static FakeNexus* current;

    static Texture* getOrCreate(const char* identifier, void* data, size_t size) {
        current->requests.push_back(identifier);
        if (!current->device_ready || !data || size == 0) return nullptr;

        auto [it, inserted] = current->created.try_emplace(identifier);
        if (inserted) {
            it->second.Width = static_cast<unsigned>(size);
            it->second.Height = 1;
            it->second.Resource = &it->second; // any unique non-null pointer; nothing dereferences it;
        }
        return &it->second;
    }

    AddonAPI api{};

    FakeNexus() {
        current = this;
        api.Textures.GetOrCreateFromMemory = &FakeNexus::getOrCreate;
    }
    ~FakeNexus() { current = nullptr; }
};

FakeNexus* FakeNexus::current = nullptr;

// A pack of one file whose contents a test can change.
struct FakeFiles {
    std::map<std::string, std::vector<uint8_t>> files;
    int reads = 0;

    viewer::TextureCache::Reader reader() {
        return [this](const std::string& key, std::vector<uint8_t>* bytes) {
            ++reads;
            const auto it = files.find(key);
            if (it == files.end()) return false;
            *bytes = it->second;
            return true;
        };
    }
};

} // namespace

TEST_CASE("a cached image costs no budget and no read", "[textures]") {
    FakeNexus nexus;
    FakeFiles files;
    files.files["icon.png"] = {1, 2, 3};

    viewer::TextureCache cache("T_");
    cache.setApi(&nexus.api);
    viewer::LoadBudget budget(1);

    const viewer::Image first = cache.get("icon.png", budget, files.reader());
    REQUIRE(first.texture != nullptr);
    CHECK(first.width == 3);

    // The budget is spent, and a hit does not need it.
    const viewer::Image second = cache.get("icon.png", budget, files.reader());
    CHECK(second.texture == first.texture);
    CHECK(files.reads == 1);
    CHECK(nexus.requests.size() == 1);
}

TEST_CASE("a load over budget waits for the next frame without being remembered", "[textures]") {
    FakeNexus nexus;
    FakeFiles files;
    files.files["a.png"] = {1};
    files.files["b.png"] = {2};

    viewer::TextureCache cache("T_");
    cache.setApi(&nexus.api);
    viewer::LoadBudget budget(1);

    CHECK(cache.get("a.png", budget, files.reader()).texture != nullptr);
    CHECK(cache.get("b.png", budget, files.reader()).texture == nullptr);
    CHECK(files.reads == 1);

    budget.reset();
    CHECK(cache.get("b.png", budget, files.reader()).texture != nullptr);
}

TEST_CASE("a missing file is remembered, a texture Nexus could not make yet is not", "[textures][regression]") {
    FakeNexus nexus;
    FakeFiles files;
    files.files["there.png"] = {1, 2};

    viewer::TextureCache cache("T_");
    cache.setApi(&nexus.api);
    viewer::LoadBudget budget(10);

    CHECK(cache.get("missing.png", budget, files.reader()).texture == nullptr);
    CHECK(cache.get("missing.png", budget, files.reader()).texture == nullptr);
    CHECK(files.reads == 1);

    // No device yet: the file is there, so the next frame has to ask again. Caching this miss
    // is what once left a freshly opened pack drawing dots until it was reloaded.
    nexus.device_ready = false;
    CHECK(cache.get("there.png", budget, files.reader()).texture == nullptr);
    nexus.device_ready = true;
    CHECK(cache.get("there.png", budget, files.reader()).texture != nullptr);
}

TEST_CASE("forgetting a texture asks Nexus under a new identifier", "[textures][regression]") {
    FakeNexus nexus;
    FakeFiles files;
    files.files["icon.png"] = {1};

    viewer::TextureCache cache("T_");
    cache.setApi(&nexus.api);
    viewer::LoadBudget budget(10);

    const viewer::Image before = cache.get("icon.png", budget, files.reader());

    // New bytes behind the same path. Under the old identifier Nexus would hand back the old
    // texture, so the reload has to be under a name it has not seen.
    files.files["icon.png"] = {1, 2, 3, 4};
    cache.forget("icon.png");
    const viewer::Image after = cache.get("icon.png", budget, files.reader());

    REQUIRE(nexus.requests.size() == 2);
    CHECK(nexus.requests[0] != nexus.requests[1]);
    CHECK(after.texture != before.texture);
    CHECK(after.width == 4);
}

TEST_CASE("forgetting a path that held no texture keeps every identifier", "[textures]") {
    FakeNexus nexus;
    FakeFiles files;
    files.files["icon.png"] = {1};

    viewer::TextureCache cache("T_");
    cache.setApi(&nexus.api);
    viewer::LoadBudget budget(10);

    // A dragged trail point rewrites its .trl and forgets that path every frame; that must
    // not make every image after it load under a fresh name.
    cache.forget("trail.trl");
    cache.get("icon.png", budget, files.reader());
    cache.clear();
    cache.forget("trail.trl");
    cache.get("icon.png", budget, files.reader());

    REQUIRE(nexus.requests.size() == 2);
    CHECK(nexus.requests[0] == "T_0_icon.png");
    CHECK(nexus.requests[1] == "T_1_icon.png");
}

TEST_CASE("without Nexus nothing is loaded and nothing is spent", "[textures]") {
    FakeFiles files;
    files.files["icon.png"] = {1};

    viewer::TextureCache cache("T_");
    viewer::LoadBudget budget(1);

    CHECK(cache.get("icon.png", budget, files.reader()).texture == nullptr);
    CHECK(files.reads == 0);
    CHECK(budget.take());
}
