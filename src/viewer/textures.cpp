#include "viewer/textures.h"

#include <stb_image.h>

#include <algorithm>

namespace viewer {

void TextureUploads::handed(AddonAPI* api, std::string identifier, int64_t pixels) {
    out_.push_back(Out{api, std::move(identifier), pixels, 0});
    pixels_ += pixels;
}

void TextureUploads::nextFrame() {
    // Nexus drops what it could not make after a minute, and never makes an image whose header
    // reads but whose pixels do not decode. Past this an image like that stops holding the others
    // back: about ten seconds at 60 fps, far past the frame or two a texture takes to be made.
    constexpr int kGiveUpFrames = 600;
    std::erase_if(out_, [this](Out& out) {
        const Texture* made = out.api->Textures.Get(out.identifier.c_str());
        const bool gone = (made && made->Resource) || ++out.frames > kGiveUpFrames;
        if (gone) pixels_ -= out.pixels;
        return gone;
    });
}

Image TextureCache::get(const std::string& key, const std::function<Reader()>& reader_for) {
    if (!api_ || !jobs_ || !uploads_ || key.empty()) return {};

    auto [it, missed] = images_.try_emplace(key);
    Entry& entry = it->second;

    // A miss: read on the worker, with the size the image decodes to from its header alone, so
    // the frame knows what handing it over will cost.
    if (missed) {
        entry.identifier = prefix_ + std::to_string(generation_) + "_" + key;
        entry.reading = jobs_->ask([key, read = reader_for(), wanted = std::weak_ptr<char>(wanted_)]() -> std::optional<Read> {
            if (wanted.expired()) return std::nullopt;
            Read file;
            if (!read(key, &file.bytes) || file.bytes.empty()) return std::nullopt;
            int width = 0;
            int height = 0;
            int channels = 0;
            if (!stbi_info_from_memory(file.bytes.data(), static_cast<int>(file.bytes.size()), &width, &height, &channels))
                return std::nullopt;
            file.pixels = int64_t{width} * height;
            return file;
        });
        return {};
    }
    if (entry.done) return entry.image;

    // Read: a file that is not there, or that is no image, is remembered, so it is not read again.
    if (entry.reading.valid()) {
        if (!util::ready(entry.reading)) return {};
        std::optional<Read> read = util::take(entry.reading).value_or(std::nullopt);
        if (!read) {
            entry.done = true;
            return {};
        }
        entry.read = std::move(*read);
    }

    // Waiting for room: then decoded on the worker. GetOrCreateFromMemory decodes on the thread
    // that calls it and leaves making the texture to Nexus's next Present. Handed over whatever
    // happens to the cache after, since the room it takes is given back only once Nexus makes it.
    if (!entry.handed) {
        if (!uploads_->allows(entry.read.pixels)) return {};
        uploads_->handed(api_, entry.identifier, entry.read.pixels);
        entry.handed = true;
        jobs_->post([api = api_, identifier = entry.identifier, bytes = std::move(entry.read.bytes)]() mutable {
            api->Textures.GetOrCreateFromMemory(identifier.c_str(), bytes.data(), bytes.size());
        });
        entry.read = {};
        return {};
    }

    // Decoded: there once Nexus has made it.
    if (const Texture* made = api_->Textures.Get(entry.identifier.c_str()); made && made->Resource) {
        entry.image = Image{static_cast<ImTextureID>(made->Resource), static_cast<int>(made->Width), static_cast<int>(made->Height)};
        entry.done = true;
    }
    return entry.image;
}

void TextureCache::forget(const std::string& key) {
    // Only bumped when something was actually cached, so forgetting a path that never held
    // a texture — a trail point being dragged rewrites its .trl — does not push every later
    // image into a fresh identifier.
    if (images_.erase(key) > 0) ++generation_;
}

void TextureCache::clear() {
    images_.clear();
    wanted_ = std::make_shared<char>();
    ++generation_;
}

} // namespace viewer
