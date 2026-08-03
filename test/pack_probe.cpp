// Console harness for the pack layer. Point it at real marker packs and it reports what
// parsed, what did not, and whether the XML survives a byte-exact round trip.
//
//   mpe_probe <pack.zip> [more.zip ...]

#include <pugixml.hpp>

#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "pack/model.h"
#include "pack/source.h"
#include "pack/trl.h"
#include "pack/xml_reader.h"

namespace {

struct RoundTripStats {
    int files = 0;
    int parse_failed = 0;
    int identical = 0;
    int identical_modulo_slash_space = 0;
    int differs = 0;
    int elements = 0;
    int elements_anchored = 0;
};

// pugixml does not model whitespace *inside* a tag, so `<POI ... />` comes back as
// `<POI .../>`. That is the only intra-tag formatting the parser can lose. Collapsing it
// in the original tells us whether it accounts for the whole residual diff or whether
// something else is also being dropped.
std::string collapseSpaceBeforeSelfClose(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == ' ' || text[i] == '\t') {
            size_t j = i;
            while (j < text.size() && (text[j] == ' ' || text[j] == '\t')) ++j;
            if (j + 1 < text.size() && text[j] == '/' && text[j + 1] == '>') {
                i = j - 1;
                continue;
            }
        }
        out.push_back(text[i]);
    }
    return out;
}

// Parse flags chosen to keep everything a conformant parser is allowed to throw away:
// comments, processing instructions, the declaration, and whitespace-only text (which is
// what indentation is). Without parse_ws_pcdata the reserialized file loses every line
// break between elements.
constexpr unsigned int kParseFlags = pugi::parse_full | pugi::parse_ws_pcdata;

// pugixml normalizes CRLF to LF while parsing (the XML spec requires it) and writes an
// XML declaration whether or not the source had one. Neither is recoverable from the
// document tree, so we record what the original looked like and undo both on the way
// out.
struct SourceShape {
    bool had_declaration = false;
    bool crlf = false;
};

SourceShape shapeOf(const std::string& text) {
    SourceShape shape;
    shape.had_declaration = text.compare(0, 5, "<?xml") == 0;
    shape.crlf = text.find("\r\n") != std::string::npos;
    return shape;
}

std::string serialize(const pugi::xml_document& doc, const SourceShape& shape) {
    struct StringWriter : pugi::xml_writer {
        std::string result;
        void write(const void* data, size_t size) override { result.append(static_cast<const char*>(data), size); }
    } writer;

    // format_raw: emit exactly the nodes we hold, adding no indentation of our own.
    unsigned int flags = pugi::format_raw;
    if (!shape.had_declaration) flags |= pugi::format_no_declaration;

    doc.save(writer, "", flags);

    if (!shape.crlf) return writer.result;

    std::string out;
    out.reserve(writer.result.size());
    for (char c : writer.result) {
        if (c == '\n') out.push_back('\r');
        out.push_back(c);
    }
    return out;
}

// Index of the first differing byte, or -1 when equal.
long long firstDifference(const std::string& a, const std::string& b) {
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) return static_cast<long long>(i);
    }
    if (a.size() != b.size()) return static_cast<long long>(n);
    return -1;
}

std::string excerpt(const std::string& text, long long at, size_t span = 60) {
    if (at < 0) return {};
    const size_t begin = static_cast<size_t>(at) > span / 2 ? static_cast<size_t>(at) - span / 2 : 0;
    std::string out = text.substr(begin, span);
    for (char& c : out) {
        if (c == '\n') c = '`';
        if (c == '\r') c = '~';
    }
    return out;
}

// If every element's offset_debug() lands on its own '<', we can edit a pack by splicing
// replacement text into the original bytes instead of reserializing the tree — which is
// the only way to keep intra-tag whitespace that pugixml does not model.
struct OffsetStats {
    int elements = 0;
    int anchored = 0;
    int bad = 0;
};

void checkOffsets(const pugi::xml_node& node, const std::string& source, OffsetStats* stats) {
    for (pugi::xml_node child : node.children()) {
        if (child.type() == pugi::node_element) {
            ++stats->elements;
            const ptrdiff_t at = child.offset_debug();
            // offset_debug() points at the tag name, one past '<'.
            if (at > 0 && static_cast<size_t>(at) <= source.size() && source[at - 1] == '<' &&
                source.compare(at, std::char_traits<char>::length(child.name()), child.name()) == 0) {
                ++stats->anchored;
            } else {
                ++stats->bad;
            }
        }
        checkOffsets(child, source, stats);
    }
}

void probeXmlRoundTrip(const pack::DataSource& archive, RoundTripStats* stats) {
    for (const std::string& entry : archive.entriesWithExtension(".xml")) {
        ++stats->files;

        std::string original;
        std::string error;
        if (!archive.readText(entry, &original, &error)) {
            std::printf("  READ FAIL  %-28s %s\n", entry.c_str(), error.c_str());
            ++stats->parse_failed;
            continue;
        }

        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_buffer(original.data(), original.size(), kParseFlags);
        if (!result) {
            std::printf("  PARSE FAIL %-28s %s (offset %td)\n", entry.c_str(), result.description(), result.offset);
            std::printf("             near: %s\n", excerpt(original, result.offset).c_str());
            ++stats->parse_failed;
            continue;
        }

        OffsetStats offsets;
        checkOffsets(doc, original, &offsets);
        stats->elements += offsets.elements;
        stats->elements_anchored += offsets.anchored;
        if (offsets.bad > 0) {
            std::printf("  OFFSETS    %-28s %d of %d elements not anchored to source\n", entry.c_str(), offsets.bad, offsets.elements);
        }

        const std::string rewritten = serialize(doc, shapeOf(original));
        const long long diff = firstDifference(original, rewritten);
        if (diff < 0) {
            ++stats->identical;
            continue;
        }

        const long long relaxed = firstDifference(collapseSpaceBeforeSelfClose(original), rewritten);
        if (relaxed < 0) {
            ++stats->identical_modulo_slash_space;
            continue;
        }

        ++stats->differs;
        std::printf("  DIFFERS    %-28s first at byte %lld (%zu -> %zu bytes)\n", entry.c_str(), diff, original.size(), rewritten.size());
        std::printf("             was: %s\n", excerpt(original, diff).c_str());
        std::printf("             now: %s\n", excerpt(rewritten, diff).c_str());
        std::printf("             (still differs at %lld after collapsing space-before-/>)\n", relaxed);
        std::printf("             was: %s\n", excerpt(collapseSpaceBeforeSelfClose(original), relaxed).c_str());
        std::printf("             now: %s\n", excerpt(rewritten, relaxed).c_str());
    }
}

// Problems a pack author would want to fix. Distinct from the reader's warnings: nothing
// here stops a pack loading, it is just wrong or suspicious.
void lintPack(const pack::Pack& loaded, const pack::DataSource& archive) {
    std::printf("  -- lint --\n");
    bool clean = true;
    auto report = [&](const char* what, const std::string& detail) {
        clean = false;
        std::printf("    %-24s %s\n", what, detail.c_str());
    };

    // Attribute names we do not recognise. Usually a typo, since a misspelled attribute
    // is silently ignored by every loader.
    std::map<std::string, int> unknown;
    auto scanUnknown = [&](const pack::AttributeCollection& attributes) {
        for (const pack::Attribute& attribute : attributes.all()) {
            if (pack::findAttribute(attribute.key)) continue;
            if (attribute.key.rfind("bh-", 0) == 0) continue; // vendor extension, expected
            ++unknown[attribute.name];
        }
    };
    for (const pack::Category& category : loaded.categories) scanUnknown(category.attributes);
    for (const pack::Pathable& pathable : loaded.pathables) scanUnknown(pathable.attributes);
    for (const auto& [name, count] : unknown) {
        report("unknown attribute", name + " (x" + std::to_string(count) + ")");
    }

    // Duplicate GUIDs. A GUID is meant to identify a marker across pack versions, so two
    // markers sharing one breaks per-marker state.
    std::map<std::string, int> guids;
    int missing_guid = 0;
    for (const pack::Pathable& pathable : loaded.pathables) {
        const std::string* guid = pathable.attributes.value("guid");
        if (!guid || guid->empty()) {
            ++missing_guid;
            continue;
        }
        ++guids[*guid];
    }
    for (const auto& [guid, count] : guids) {
        if (count > 1) report("duplicate GUID", guid + " used by " + std::to_string(count) + " markers");
    }

    // TmfLib refuses a POI without a position or map id. Packs do contain position-less
    // POIs on purpose, as invisible anchors for toggles and achievements — those get
    // dropped by the loader too, but the author clearly does not care. Only the ones
    // meant to be drawn are worth reporting.
    int invisible_without_position = 0;
    for (const pack::Pathable& pathable : loaded.pathables) {
        if (pathable.type != pack::PathableType::Poi) continue;

        const bool has_position =
            loaded.effectiveValue(pathable, "xpos") && loaded.effectiveValue(pathable, "ypos") && loaded.effectiveValue(pathable, "zpos");
        if (!has_position) {
            if (loaded.effectiveBool(pathable, "ingamevisibility")) {
                report("POI without a position", "type=" + pathable.category_id + " — the loader will drop it");
            } else {
                ++invisible_without_position;
            }
        }

        if (!loaded.effectiveValue(pathable, "mapid")) {
            report("POI without a map id", "type=" + pathable.category_id + " — the loader will drop it");
        }
    }
    if (invisible_without_position > 0) {
        std::printf("    %-24s %d invisible POIs have no position (anchors; dropped by the loader)\n", "note", invisible_without_position);
    }

    for (const pack::Pathable& pathable : loaded.pathables) {
        if (pathable.category == pack::kNoIndex) {
            report(
                "unknown category",
                pathable.category_id.empty() ? std::string(pack::toString(pathable.type)) + " has no type attribute"
                                             : "type=\"" + pathable.category_id + "\" is not declared anywhere");
        }
    }

    // Trails whose traildata is missing, or whose map id disagrees with the .trl header.
    for (const pack::Pathable& pathable : loaded.pathables) {
        if (pathable.type != pack::PathableType::Trail) continue;
        const std::string* data = loaded.effectiveValue(pathable, "traildata");
        if (!data || data->empty()) {
            report("trail without traildata", "type=" + pathable.category_id);
            continue;
        }

        std::vector<uint8_t> bytes;
        std::string read_error;
        if (!archive.read(*data, &bytes, &read_error)) continue;

        pack::trl::Trail trail;
        std::string trl_error;
        if (!pack::trl::read(bytes, &trail, &trl_error)) continue;

        // The .trl header wins: UnpackTrailPoi overwrites the XML mapid with it. So a
        // disagreement is a dead attribute, not a broken trail.
        int xml_map = 0;
        const std::string* map = pathable.attributes.value("mapid");
        if (map && pack::parseInt(*map, &xml_map) && trail.map_id != 0 && xml_map != trail.map_id) {
            report(
                "trail mapid is ignored",
                *data + " header says " + std::to_string(trail.map_id) + ", xml says " + std::to_string(xml_map) + " — the header wins");
        }
    }

    // Images in the archive that nothing references.
    std::set<std::string> referenced;
    for (const pack::Pathable& pathable : loaded.pathables) {
        for (const char* key : {"iconfile", "texture", "traildata"}) {
            const std::string* path = loaded.effectiveValue(pathable, key);
            if (path && !path->empty()) referenced.insert(pack::normalizePath(*path));
        }
    }
    std::vector<std::string> unused;
    for (const std::string& entry : archive.entries()) {
        const bool is_asset =
            entry.size() > 4 && (entry.compare(entry.size() - 4, 4, ".png") == 0 || entry.compare(entry.size() - 4, 4, ".trl") == 0);
        if (is_asset && referenced.find(entry) == referenced.end()) unused.push_back(entry);
    }
    if (!unused.empty()) {
        report("unreferenced asset", std::to_string(unused.size()) + " files ship but nothing points at them");
        for (size_t i = 0; i < unused.size() && i < 12; ++i) std::printf("      %s\n", unused[i].c_str());
        if (unused.size() > 12) std::printf("      ... and %zu more\n", unused.size() - 12);
    }

    if (missing_guid > 0) {
        report("markers without GUID", std::to_string(missing_guid) + " (fine, but they cannot carry per-marker state)");
    }

    if (clean) std::printf("    nothing to report\n");
}

// Loads the pack through the real reader and reports what came out, plus the things a
// pack author would want flagged: dangling category references, resources referenced but
// not present in the archive, and .trl files that do not read.
int probeModel(const pack::DataSource& archive) {
    pack::Pack loaded;
    std::string error;
    if (!pack::loadPack(archive, &loaded, &error)) {
        std::printf("  LOAD FAILED: %s\n", error.c_str());
        return 1;
    }

    std::printf(
        "  model: %zu categories (%zu roots), %zu POIs, %zu trails, %zu routes\n",
        loaded.categories.size(),
        loaded.roots.size(),
        loaded.countByType(pack::PathableType::Poi),
        loaded.countByType(pack::PathableType::Trail),
        loaded.countByType(pack::PathableType::Route));

    // How much do categories actually span files? This decides whether the editor's tree
    // can be category-only or has to be file-oriented.
    size_t split_categories = 0;
    size_t max_files_for_one_category = 0;
    std::string most_split;
    for (const pack::Category& category : loaded.categories) {
        std::set<size_t> files;
        for (const pack::SourceSpan& span : category.declarations) files.insert(span.file);
        if (files.size() > 1) ++split_categories;
        if (files.size() > max_files_for_one_category) {
            max_files_for_one_category = files.size();
            most_split = category.id;
        }
    }
    std::printf(
        "  categories declared across >1 file: %zu (worst: %s in %zu files)\n",
        split_categories,
        most_split.empty() ? "-" : most_split.c_str(),
        max_files_for_one_category);

    size_t dangling = 0;
    for (const pack::Pathable& pathable : loaded.pathables) {
        if (pathable.category == pack::kNoIndex) ++dangling;
    }

    // Every resource path any pathable resolves to, effective values so category-level
    // iconFile counts.
    size_t missing_resources = 0;
    std::vector<std::string> missing_examples;
    for (const pack::Pathable& pathable : loaded.pathables) {
        for (const char* key : {"iconfile", "texture", "traildata"}) {
            const std::string* path = loaded.effectiveValue(pathable, key);
            if (!path || path->empty()) continue;
            if (archive.contains(*path)) continue;
            ++missing_resources;
            if (missing_examples.size() < 3) missing_examples.push_back(*path);
        }
    }

    size_t trl_ok = 0;
    size_t trl_failed = 0;
    size_t trl_segments = 0;
    size_t trl_points = 0;
    for (const std::string& entry : archive.entriesWithExtension(".trl")) {
        std::vector<uint8_t> bytes;
        std::string read_error;
        if (!archive.read(entry, &bytes, &read_error)) {
            ++trl_failed;
            continue;
        }

        pack::trl::Trail trail;
        std::string trl_error;
        if (!pack::trl::read(bytes, &trail, &trl_error)) {
            ++trl_failed;
            std::printf("  TRL FAIL   %-28s %s\n", entry.c_str(), trl_error.c_str());
            continue;
        }

        // The writer has to reproduce the file it read, terminators included.
        const std::vector<uint8_t> rewritten = pack::trl::write(trail);
        if (rewritten != bytes) {
            std::printf("  TRL DIFFERS %-27s %zu -> %zu bytes\n", entry.c_str(), bytes.size(), rewritten.size());
        }

        ++trl_ok;
        trl_segments += trail.segments.size();
        trl_points += trail.pointCount();
    }

    std::printf("  trl: %zu read, %zu failed | %zu segments, %zu points\n", trl_ok, trl_failed, trl_segments, trl_points);
    std::printf("  dangling category refs: %zu | missing resources: %zu", dangling, missing_resources);
    for (const std::string& example : missing_examples) std::printf("\n                          e.g. %s", example.c_str());
    std::printf("\n");

    if (!loaded.warnings.empty()) {
        std::printf("  warnings: %zu\n", loaded.warnings.size());
        for (size_t i = 0; i < loaded.warnings.size() && i < 8; ++i) {
            std::printf("    %s: %s\n", loaded.warnings[i].file.c_str(), loaded.warnings[i].message.c_str());
        }
    }

    lintPack(loaded, archive);

    return 0;
}

int probePack(const std::filesystem::path& file) {
    std::printf("\n=== %s ===\n", file.filename().string().c_str());

    std::string error;
    std::unique_ptr<pack::DataSource> source = pack::open(file, &error);
    if (!source) {
        std::printf("  FAILED TO OPEN: %s\n", error.c_str());
        return 1;
    }
    const pack::DataSource& archive = *source;

    const size_t xml = archive.entriesWithExtension(".xml").size();
    const size_t trl = archive.entriesWithExtension(".trl").size();
    const size_t png = archive.entriesWithExtension(".png").size();
    std::printf(
        "  %s | %zu entries: %zu xml, %zu trl, %zu png\n",
        archive.isDirectory() ? "folder" : "archive",
        archive.entries().size(),
        xml,
        trl,
        png);

    RoundTripStats stats;
    probeXmlRoundTrip(archive, &stats);
    std::printf(
        "  xml: %d parsed, %d failed | round trip: %d byte-identical, %d identical except space-before-/>, %d differ\n",
        stats.files - stats.parse_failed,
        stats.parse_failed,
        stats.identical,
        stats.identical_modulo_slash_space,
        stats.differs);
    std::printf("  source anchoring: %d of %d elements resolvable to a byte offset\n", stats.elements_anchored, stats.elements);

    const int model_failures = probeModel(archive);

    return (stats.parse_failed > 0 || model_failures > 0) ? 1 : 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: mpe_probe <pack.zip> [more.zip ...]\n");
        return 2;
    }

    int failures = 0;
    for (int i = 1; i < argc; ++i) {
        failures += probePack(std::filesystem::path(argv[i]));
    }

    std::printf("\n");
    return failures == 0 ? 0 : 1;
}
