// How long loading each corpus pack takes, phase by phase, as registry::loadPack does it: the
// archive's index, inflating the XML, pugixml's parse, the model built from it, the bake, the .trl
// files, and the trail textures sampled for their colour. Hidden from ctest; run before and after
// a change to anything loadPack calls, on a Release build, on one machine:
//
//     bin/Release/markus_viewer_test.exe "loading each pack\, timed"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <pugixml.hpp>
#include <unordered_map>

#include "corpus.h"
#include "pack/source.h"
#include "pack/trl.h"
#include "pack/xml_reader.h"
#include "util/path_text.h"
#include "viewer/live/bake.h"
#include "viewer/packs/registry.h"
#include "viewer/render/trail_colour.h"
#include "viewer/ui/glyphs.h"

TEST_CASE("loading each pack, timed", "[.bench]") {
    const std::vector<std::filesystem::path> paths = corpus::packs();
    if (paths.empty()) FAIL(corpus::missingMessage());

    using Clock = std::chrono::steady_clock;
    const auto ms = [](Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); };

    std::printf(
        "%-24s %8s %8s %8s %8s %8s %8s %8s %8s %8s | %8s\n",
        "pack",
        "open",
        "inflate",
        "parse",
        "model",
        "bake",
        "trl",
        "colour",
        "scripts",
        "other",
        "loadPack");
    for (const std::filesystem::path& path : paths) {
        Clock::time_point at = Clock::now();
        std::string error;
        std::unique_ptr<pack::DataSource> source = pack::open(path, &error);
        REQUIRE(source);
        const double open_ms = ms(Clock::now() - at);

        // The XML alone, inflated and parsed as loadPack does, to split its time.
        std::vector<std::string> xml = source->entriesWithExtension(".xml");
        double inflate_ms = 0.0;
        double parse_ms = 0.0;
        size_t xml_bytes = 0;
        for (const std::string& entry : xml) {
            std::string text;
            at = Clock::now();
            if (!source->readText(entry, &text, &error)) continue;
            inflate_ms += ms(Clock::now() - at);
            xml_bytes += text.size();
            at = Clock::now();
            pugi::xml_document document;
            document.load_buffer(text.data(), text.size(), pugi::parse_full);
            parse_ms += ms(Clock::now() - at);
        }

        at = Clock::now();
        pack::Pack model = pack::loadPack(*source);
        const double model_ms = std::max(0.0, ms(Clock::now() - at) - inflate_ms - parse_ms);

        at = Clock::now();
        std::unordered_map<std::string, std::string> scripts;
        for (const std::string& key : source->entriesWithExtension(".lua")) {
            std::string text;
            if (source->readText(key, &text, &error)) scripts.emplace(key, std::move(text));
        }
        viewer::ui::glyphs::Set glyphs = viewer::ui::glyphs::of(model);
        for (const auto& [key, text] : scripts) glyphs.add(text);
        const double scripts_ms = ms(Clock::now() - at);

        at = Clock::now();
        viewer::BakedPathables pathables = viewer::bake(model);
        std::vector<viewer::BakedCategory> categories = viewer::bakeCategories(model);
        const double bake_ms = ms(Clock::now() - at);

        at = Clock::now();
        std::unordered_map<std::string, bool> read;
        for (const viewer::BakedTrail& trail : pathables.trails) {
            const std::string key = pack::normalizePath(trail.data);
            if (!read.try_emplace(key).second) continue;
            std::vector<uint8_t> bytes;
            pack::trl::Trail parsed;
            if (source->read(key, &bytes, &error)) (void)pack::trl::read(bytes, &parsed, &error);
        }
        const double trl_ms = ms(Clock::now() - at);

        at = Clock::now();
        std::unordered_map<std::string, ImU32> sampled;
        for (const viewer::BakedTrail& trail : pathables.trails) {
            if (!trail.sample_pending) continue;
            const std::string key = pack::normalizePath(trail.texture);
            if (!sampled.try_emplace(key).second) continue;
            std::vector<uint8_t> bytes;
            if (source->read(key, &bytes, &error)) sampled[key] = viewer::trail_colour::sample(bytes);
        }
        const double colour_ms = ms(Clock::now() - at);

        // The real thing, once more, for its own total.
        at = Clock::now();
        const viewer::registry::LoadedPack loaded = viewer::registry::loadPack(path);
        const double total_ms = ms(Clock::now() - at);

        const double parts = open_ms + inflate_ms + parse_ms + model_ms + bake_ms + trl_ms + colour_ms + scripts_ms;
        std::printf(
            "%-24.24s %8.1f %8.1f %8.1f %8.1f %8.1f %8.1f %8.1f %8.1f %8.1f | %8.1f   %zu xml, %.1f MB, %zu categories, "
            "%zu pathables, %zu trl, %zu textures\n",
            util::pathText(path.filename()).c_str(),
            open_ms,
            inflate_ms,
            parse_ms,
            model_ms,
            bake_ms,
            trl_ms,
            colour_ms,
            scripts_ms,
            total_ms - parts,
            total_ms,
            xml.size(),
            xml_bytes / 1e6,
            model.categories.size(),
            model.pathables.size(),
            read.size(),
            sampled.size());
    }
}
