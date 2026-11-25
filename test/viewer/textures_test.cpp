// The texture cache against a fake Nexus: what it asks Nexus for, under which identifier, on
// which thread, how much at once, and what it remembers. A real Nexus hands back the existing
// texture for an identifier it has seen and makes a new one only at its next Present, which is the
// behaviour the fake keeps and the cache has to work around.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <future>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "fake_png.h"
#include "util/job_queue.h"
#include "viewer/textures.h"

namespace {

// Nexus's half, as a test can see it. One instance at a time: the API is a table of plain
// function pointers, so the fake has to reach its state through a global.
struct FakeNexus {
    std::mutex mutex; // the cache hands over from its worker, the test looks from its own thread
    std::map<std::string, Texture> queued; // decoded, waiting for a Present
    std::map<std::string, Texture> created; // by identifier, as Nexus keeps them
    std::vector<std::string> requests;
    std::vector<int64_t> made_per_present; // the pixels each Present made
    bool device_ready = true;

    static FakeNexus* current;

    static Texture* getOrCreate(const char* identifier, void* data, size_t size) {
        std::lock_guard<std::mutex> lock(current->mutex);
        current->requests.push_back(identifier);
        if (const auto it = current->created.find(identifier); it != current->created.end()) return &it->second;
        Texture texture{};
        if (!pngSize(static_cast<const uint8_t*>(data), size, &texture.Width, &texture.Height)) return nullptr;
        current->queued.emplace(identifier, texture);
        return nullptr;
    }

    static Texture* get(const char* identifier) {
        std::lock_guard<std::mutex> lock(current->mutex);
        const auto it = current->created.find(identifier);
        return it == current->created.end() ? nullptr : &it->second;
    }

    // Nexus's Present: everything queued is made at once, once there is a device to make it with.
    void present() {
        std::lock_guard<std::mutex> lock(mutex);
        if (!device_ready) return;
        int64_t pixels = 0;
        for (const auto& [identifier, texture] : queued) {
            Texture& made = created[identifier];
            made = texture;
            made.Resource = &made; // any unique non-null pointer; nothing dereferences it
            pixels += int64_t{texture.Width} * texture.Height;
        }
        if (!queued.empty()) made_per_present.push_back(pixels);
        queued.clear();
    }

    AddonAPI api{};

    FakeNexus() {
        current = this;
        api.Textures.GetOrCreateFromMemory = &FakeNexus::getOrCreate;
        api.Textures.Get = &FakeNexus::get;
    }
    ~FakeNexus() { current = nullptr; }
};

FakeNexus* FakeNexus::current = nullptr;

// A pack whose files a test can change, read from whichever thread the cache reads on.
struct FakeFiles {
    std::mutex mutex;
    std::map<std::string, std::vector<uint8_t>> files;
    int reads = 0;

    std::function<viewer::TextureCache::Reader()> reader() {
        return [this] {
            return viewer::TextureCache::Reader([this](const std::string& key, std::vector<uint8_t>* bytes) {
                std::lock_guard<std::mutex> lock(mutex);
                ++reads;
                const auto it = files.find(key);
                if (it == files.end()) return false;
                *bytes = it->second;
                return true;
            });
        };
    }
};

// One cache over a fake Nexus, with the worker and the upload budget the viewer gives it.
struct Harness {
    FakeNexus nexus;
    FakeFiles files;
    util::JobQueue jobs;
    viewer::TextureUploads uploads;
    viewer::TextureCache cache{"T_"};

    Harness() { cache.setApi(&nexus.api, &jobs, &uploads); }

    viewer::Image get(const std::string& key) { return cache.get(key, files.reader()); }

    // The end of a frame: the worker catches up, Nexus presents, and the next frame starts as
    // the viewer's does.
    void endFrame() {
        jobs.wait();
        nexus.present();
        uploads.nextFrame();
    }

    // Looks `key` up once a frame until it is drawn, for at most `frames` frames: read, then
    // handed over, then made.
    viewer::Image drawn(const std::string& key, int frames = 3) {
        viewer::Image image;
        for (int i = 0; i < frames && !image.texture; ++i) {
            image = get(key);
            if (!image.texture) endFrame();
        }
        return image;
    }
};

std::string iconName(int icon) {
    return "icon" + std::to_string(icon) + ".png";
}

} // namespace

TEST_CASE("a miss is read on the worker, and the lookup does not wait for it", "[textures][regression]") {
    Harness h;
    std::promise<void> disk;
    std::shared_future<void> answered = disk.get_future().share();

    // A disk that does not answer until the test says so: a frame that waited for it would
    // never get past get(), which is the stutter this cache is there to prevent.
    std::thread::id read_on;
    const auto slow = [&] {
        return viewer::TextureCache::Reader([&](const std::string&, std::vector<uint8_t>* bytes) {
            answered.wait();
            read_on = std::this_thread::get_id();
            *bytes = pngHeader(3, 2);
            return true;
        });
    };

    CHECK(h.cache.get("icon.png", slow).texture == nullptr);
    CHECK(h.cache.get("icon.png", slow).texture == nullptr);
    disk.set_value();
    h.endFrame();
    CHECK(h.cache.get("icon.png", slow).texture == nullptr); // handed over now
    h.endFrame();

    const viewer::Image image = h.cache.get("icon.png", slow);
    CHECK(image.texture != nullptr);
    CHECK(image.width == 3);
    CHECK(image.height == 2);
    CHECK(read_on != std::this_thread::get_id());
    CHECK(h.nexus.requests.size() == 1);
}

TEST_CASE("one Present makes no more pixels than the cap, however many images arrive", "[textures][regression]") {
    Harness h;
    // Arriving on a map: every image comes into view in one frame. Nexus makes all it was handed
    // at its next Present, copying each image to the GPU there, so handing it all of them is
    // that Present's hitch. Twelve at 1024×1024 is three times the cap.
    constexpr int kImages = 12;
    for (int image = 0; image < kImages; ++image) h.files.files[iconName(image)] = pngHeader(1024, 1024);

    int drawn = 0;
    for (int frame = 0; frame < 3 * kImages && drawn < kImages; ++frame) {
        drawn = 0;
        for (int image = 0; image < kImages; ++image) drawn += h.get(iconName(image)).texture != nullptr;
        h.endFrame();
    }
    CHECK(drawn == kImages);
    REQUIRE(h.nexus.made_per_present.size() > 1);
    for (const int64_t pixels : h.nexus.made_per_present) CHECK(pixels <= viewer::TextureUploads::MAX_PIXELS);
    CHECK(h.files.reads == kImages);
}

TEST_CASE("small icons arriving together are handed over together", "[textures]") {
    Harness h;
    // What a count of images would hold back: a map's worth of 64×64 icons is a few MB in all.
    constexpr int kIcons = 300;
    for (int icon = 0; icon < kIcons; ++icon) h.files.files[iconName(icon)] = pngHeader(64, 64);

    for (int frame = 0; frame < 2; ++frame) {
        for (int icon = 0; icon < kIcons; ++icon) h.get(iconName(icon));
        h.endFrame();
    }
    CHECK(h.nexus.made_per_present == std::vector<int64_t>{int64_t{kIcons} * 64 * 64});
}

TEST_CASE("an image larger than the cap goes alone", "[textures]") {
    Harness h;
    h.files.files["small.png"] = pngHeader(64, 64);
    h.files.files["shape.png"] = pngHeader(2080, 2080); // Hero's pack's largest
    h.files.files["later.png"] = pngHeader(64, 64);

    // All three read in one frame.
    for (const char* name : {"small.png", "shape.png", "later.png"}) h.get(name);
    h.endFrame();
    // The small one is handed over; the large one waits until nothing is out.
    h.get("small.png");
    h.get("shape.png");
    h.endFrame();
    // Out alone: what comes after it waits for it.
    h.get("shape.png");
    h.get("later.png");
    h.endFrame();
    CHECK(h.drawn("shape.png").texture != nullptr);
    CHECK(h.drawn("later.png").texture != nullptr);

    REQUIRE(h.nexus.made_per_present.size() == 3);
    CHECK(h.nexus.made_per_present[0] == 64 * 64);
    CHECK(h.nexus.made_per_present[1] == 2080 * 2080);
    CHECK(h.nexus.made_per_present[2] == 64 * 64);
}

TEST_CASE("a cached image costs no read", "[textures]") {
    Harness h;
    h.files.files["icon.png"] = pngHeader(3, 1);

    const viewer::Image first = h.drawn("icon.png");
    REQUIRE(first.texture != nullptr);

    const viewer::Image second = h.get("icon.png");
    h.jobs.wait();
    CHECK(second.texture == first.texture);
    CHECK(h.files.reads == 1);
    CHECK(h.nexus.requests.size() == 1);
}

TEST_CASE("a missing file is remembered, a texture Nexus has not made yet is not", "[textures][regression]") {
    Harness h;
    h.files.files["there.png"] = pngHeader(2, 1);
    h.files.files["not_an_image.png"] = {1, 2, 3};

    CHECK(h.drawn("missing.png").texture == nullptr);
    CHECK(h.drawn("not_an_image.png").texture == nullptr);
    h.jobs.wait();
    CHECK(h.files.reads == 2);
    CHECK(h.nexus.requests.empty());

    // No device yet: the file is there, so later frames have to ask Nexus again. Caching this
    // miss is what once left a freshly opened pack drawing dots until it was reloaded.
    h.nexus.device_ready = false;
    CHECK(h.drawn("there.png").texture == nullptr);
    h.nexus.device_ready = true;
    CHECK(h.drawn("there.png").texture != nullptr);
    CHECK(h.files.reads == 3); // once each, the wait for the device included
}

TEST_CASE("forgetting a texture asks Nexus under a new identifier", "[textures][regression]") {
    Harness h;
    h.files.files["icon.png"] = pngHeader(1, 1);
    const viewer::Image before = h.drawn("icon.png");

    // New bytes behind the same path. Under the old identifier Nexus would hand back the old
    // texture, so the reload has to be under a name it has not seen.
    h.files.files["icon.png"] = pngHeader(4, 1);
    h.cache.forget("icon.png");
    const viewer::Image after = h.drawn("icon.png");

    REQUIRE(h.nexus.requests.size() == 2);
    CHECK(h.nexus.requests[0] != h.nexus.requests[1]);
    CHECK(after.texture != before.texture);
    CHECK(after.width == 4);
}

TEST_CASE("forgetting a path that held no texture keeps every identifier", "[textures]") {
    Harness h;
    h.files.files["icon.png"] = pngHeader(1, 1);

    // A dragged trail point rewrites its .trl and forgets that path every frame; that must
    // not make every image after it load under a fresh name.
    h.cache.forget("trail.trl");
    h.drawn("icon.png");
    h.cache.clear();
    h.cache.forget("trail.trl");
    h.drawn("icon.png");

    REQUIRE(h.nexus.requests.size() == 2);
    CHECK(h.nexus.requests[0] == "T_0_icon.png");
    CHECK(h.nexus.requests[1] == "T_1_icon.png");
}

TEST_CASE("what is still queued when the cache is cleared reads nothing", "[textures]") {
    Harness h;
    h.files.files["icon.png"] = pngHeader(1, 1);

    // The worker busy with something else, so the read is still queued at clear().
    std::promise<void> busy;
    std::shared_future<void> done = busy.get_future().share();
    h.jobs.post([done] { done.wait(); });

    h.get("icon.png");
    h.cache.clear();
    busy.set_value();
    h.jobs.wait();

    CHECK(h.files.reads == 0);
    CHECK(h.nexus.requests.empty());
}

TEST_CASE("without Nexus nothing is loaded and nothing is read", "[textures]") {
    FakeFiles files;
    files.files["icon.png"] = pngHeader(1, 1);
    util::JobQueue jobs;
    viewer::TextureUploads uploads;

    viewer::TextureCache cache("T_");
    CHECK(cache.get("icon.png", files.reader()).texture == nullptr);
    cache.setApi(nullptr, &jobs, &uploads);
    CHECK(cache.get("icon.png", files.reader()).texture == nullptr);
    jobs.wait();
    CHECK(files.reads == 0);
}
