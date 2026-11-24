// The colour a trail without a tint is drawn with on the map: its texture's dominant colour, as
// Pathing's ColorThief picks it, and the loaders that sample it.

#include <catch2/catch_test_macros.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <array>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "corpus.h"
#include "viewer/registry.h"
#include "viewer/trail_colour.h"

namespace {

using viewer::trail_colour::NO_COLOUR;

// `count` RGBA pixels of one colour.
std::vector<uint8_t> solid(int count, std::array<uint8_t, 4> rgba) {
    std::vector<uint8_t> pixels;
    for (int i = 0; i < count; ++i) pixels.insert(pixels.end(), rgba.begin(), rgba.end());
    return pixels;
}

// `pixels`, `width` wide, as a PNG file's bytes.
std::vector<uint8_t> png(const std::vector<uint8_t>& pixels, int width) {
    std::vector<uint8_t> file;
    const int height = static_cast<int>(pixels.size() / 4) / width;
    stbi_write_png_to_func(
        [](void* context, void* data, int size) {
            auto* out = static_cast<std::vector<uint8_t>*>(context);
            out->insert(out->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
        },
        &file,
        width,
        height,
        4,
        pixels.data(),
        width * 4);
    return file;
}

void writeBytes(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

} // namespace

TEST_CASE("a texture of one colour gives the middle of that colour's histogram cell", "[trail_colour]") {
    // Five bits a channel: 255 is cell 31, whose middle is 252; 200 is cell 25, 204; 0, 4.
    const std::vector<uint8_t> yellow = solid(16, {255, 200, 0, 255});
    CHECK(viewer::trail_colour::dominant(yellow.data(), 4, 4) == IM_COL32(252, 204, 4, 255));
}

TEST_CASE("a texture is premultiplied before it is sampled", "[trail_colour]") {
    // 255 at an alpha of 200 is 200 premultiplied, cell 25.
    const std::vector<uint8_t> faded = solid(16, {255, 255, 0, 200});
    CHECK(viewer::trail_colour::dominant(faded.data(), 4, 4) == IM_COL32(204, 204, 4, 255));
}

TEST_CASE("transparent and white pixels give no colour", "[trail_colour]") {
    const std::vector<uint8_t> white = solid(16, {255, 255, 255, 255});
    CHECK(viewer::trail_colour::dominant(white.data(), 4, 4) == NO_COLOUR);
    // Under ColorThief's 125, a pixel is left out however coloured.
    const std::vector<uint8_t> faint = solid(16, {255, 0, 0, 124});
    CHECK(viewer::trail_colour::dominant(faint.data(), 4, 4) == NO_COLOUR);
}

TEST_CASE("the colour most of a texture is wins", "[trail_colour]") {
    // Every tenth pixel is read: seven of the ten read are red, three blue.
    std::vector<uint8_t> pixels;
    for (int i = 0; i < 100; ++i) {
        const std::array<uint8_t, 4> rgba = i < 70 ? std::array<uint8_t, 4>{255, 0, 0, 255} : std::array<uint8_t, 4>{0, 0, 255, 255};
        pixels.insert(pixels.end(), rgba.begin(), rgba.end());
    }
    CHECK(viewer::trail_colour::dominant(pixels.data(), 10, 10) == IM_COL32(252, 4, 4, 255));
}

TEST_CASE("an image file is decoded to be sampled, and one that will not decode gives no colour", "[trail_colour]") {
    CHECK(viewer::trail_colour::sample(png(solid(16, {255, 200, 0, 255}), 4)) == IM_COL32(252, 204, 4, 255));
    CHECK(viewer::trail_colour::sample({1, 2, 3}) == NO_COLOUR);
    CHECK(viewer::trail_colour::sample({}) == NO_COLOUR);
}

TEST_CASE("a loaded pack's trails take their texture's colour on the map, unless tinted", "[trail_colour][registry]") {
    const corpus::TempDir temp("trail_colour_pack");
    writeBytes(temp / "yellow.png", png(solid(16, {255, 200, 0, 255}), 4));
    corpus::writeFile(
        temp / "trails.xml",
        "<OverlayData>\n"
        "<MarkerCategory name=\"c\"/>\n"
        "<POIs>\n"
        "<Trail type=\"c\" trailData=\"t.trl\" texture=\"Yellow.png\"/>\n"
        "<Trail type=\"c\" trailData=\"t.trl\" texture=\"yellow.png\" color=\"ff0000\"/>\n"
        "<Trail type=\"c\" trailData=\"t.trl\" texture=\"yellow.png\" color=\"ffffff\"/>\n"
        "<Trail type=\"c\" trailData=\"t.trl\" texture=\"missing.png\"/>\n"
        "<Trail type=\"c\" trailData=\"t.trl\"/>\n"
        "</POIs>\n"
        "</OverlayData>\n");

    const viewer::registry::LoadedPack loaded = viewer::registry::loadPack(temp.path());
    REQUIRE(loaded.error.empty());
    const std::vector<viewer::BakedTrail>& trails = loaded.pathables.trails;
    REQUIRE(trails.size() == 5);
    CHECK(trails[0].sample_colour == IM_COL32(252, 204, 4, 255));
    CHECK(trails[1].sample_colour == trails[1].tint);
    // White is Pathing's "no tint", so a white one samples as well.
    CHECK(trails[2].sample_colour == IM_COL32(252, 204, 4, 255));
    // A texture Pathing cannot load is its missing-texture image and DarkGray.
    CHECK(trails[3].sample_colour == NO_COLOUR);
    // No texture at all leaves Pathing's TrailSampleColor at white.
    CHECK(trails[4].sample_colour == IM_COL32_WHITE);
    for (const viewer::BakedTrail& trail : trails) CHECK_FALSE(trail.sample_pending);
}
