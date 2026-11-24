// Console harness for the pack layer. Point it at real marker packs and it reports what
// parsed, what did not, and whether the XML survives a byte-exact round trip.
//
//   markus_probe <pack.zip> [more.zip ...]

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
#include "pack/writer.h"
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

struct StringWriter : pugi::xml_writer {
    std::string result;
    void write(const void* data, size_t size) override { result.append(static_cast<const char*>(data), size); }
};

std::string serialize(const pugi::xml_document& doc, const SourceShape& shape) {
    StringWriter writer;

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

// Defined further down, next to the other save machinery.
std::filesystem::path save_test_dir;
int probeSaveRoundTrip(const pack::Pack& loaded, const pack::DataSource& source, const std::filesystem::path& out_dir);
int probeAssetChanges(pack::Pack& loaded, const pack::DataSource& source, const std::filesystem::path& out_dir);
int probeAddPathable(pack::Pack& loaded, const pack::DataSource& source, const std::filesystem::path& out_dir);
int probeRemove(pack::Pack& loaded, const pack::DataSource& source, const std::filesystem::path& out_dir);
int probeAddCategory(pack::Pack& loaded, const pack::DataSource& source, const std::filesystem::path& out_dir);

// The writer reformats a pack once, so byte equality with the original is not the test.
// What must hold is that writing is a fixed point: serialize, parse the result, serialize
// again, and the two must match. If they did not, every save would churn the file afresh.
void probeWriter(const pack::Pack& loaded) {
    size_t stable = 0;
    size_t unstable = 0;

    for (size_t i = 0; i < loaded.files.size(); ++i) {
        std::string once;
        std::string error;
        if (!pack::writeXmlFile(loaded, i, &once, &error)) {
            std::printf("  WRITE FAIL %-28s %s\n", loaded.files[i].path.c_str(), error.c_str());
            ++unstable;
            continue;
        }

        pugi::xml_document reparsed;
        if (!reparsed.load_buffer(once.data(), once.size(), pugi::parse_full)) {
            std::printf("  UNREADABLE OUTPUT %-22s what we wrote does not parse\n", loaded.files[i].path.c_str());
            ++unstable;
            continue;
        }

        StringWriter writer;
        unsigned int flags = pugi::format_indent;
        if (!loaded.files[i].format.declaration) flags |= pugi::format_no_declaration;
        if (loaded.files[i].format.bom) flags |= pugi::format_write_bom;
        reparsed.save(writer, pack::kIndent, flags);

        std::string twice = writer.result;
        if (loaded.files[i].format.crlf) {
            std::string crlf;
            for (char c : twice) {
                if (c == '\n') crlf.push_back('\r');
                crlf.push_back(c);
            }
            twice.swap(crlf);
        }

        if (twice == once) {
            ++stable;
            continue;
        }

        ++unstable;
        const long long at = firstDifference(once, twice);
        std::printf("  NOT A FIXED POINT %-22s at byte %lld\n", loaded.files[i].path.c_str(), at);
        std::printf("             once:  %s\n", excerpt(once, at).c_str());
        std::printf("             twice: %s\n", excerpt(twice, at).c_str());
    }

    std::printf("  writer: %zu/%zu files stable on rewrite\n", stable, loaded.files.size());
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

    // Real attributes sitting where nothing reads them — an iconSize on a trail, a
    // trailScale on a marker. Harmless, but dead, and the property panel flags the same
    // thing, so this is where to see how often authors actually do it.
    std::map<std::string, int> misplaced;
    auto scanMisplaced = [&](const pack::AttributeCollection& attributes, uint8_t mask, const char* where) {
        for (const pack::Attribute& attribute : attributes.all()) {
            const pack::AttributeDef* def = pack::findAttribute(attribute.key);
            if (!def || (pack::settableOn(*def) & mask) != 0) continue;
            ++misplaced[attribute.name + " on " + where];
        }
    };
    for (const pack::Category& category : loaded.categories) scanMisplaced(category.attributes, pack::kAppliesCategory, "a category");
    for (const pack::Pathable& pathable : loaded.pathables) {
        const bool trail = pathable.type == pack::PathableType::Trail;
        scanMisplaced(pathable.attributes, trail ? pack::kAppliesTrail : pack::kAppliesPoi, trail ? "a trail" : "a marker");
    }
    for (const auto& [name, count] : misplaced) {
        report("attribute does nothing", name + " (x" + std::to_string(count) + ")");
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
        for (const pack::SourceNode& declaration : category.declarations) files.insert(declaration.file);
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

    probeWriter(loaded);

    int failures = 0;
    if (!save_test_dir.empty()) {
        failures += probeSaveRoundTrip(loaded, archive, save_test_dir);
        failures += probeAssetChanges(loaded, archive, save_test_dir);
    }

    lintPack(loaded, archive);

    // Last, because these leave the model changed.
    if (!save_test_dir.empty()) {
        failures += probeAddPathable(loaded, archive, save_test_dir);
        failures += probeAddCategory(loaded, archive, save_test_dir);
        failures += probeRemove(loaded, archive, save_test_dir);
    }

    return failures;
}

// End-to-end: write the pack out as a zip, read it straight back, and require the result
// to be indistinguishable from what went in. writeXmlFile being byte-exact does not by
// itself prove the archive assembly keeps every entry.
//
// Opt-in, because a diagnostic tool should not write files unless asked.
int probeSaveRoundTrip(const pack::Pack& loaded, const pack::DataSource& source, const std::filesystem::path& out_dir) {
    std::error_code ec;
    std::filesystem::create_directories(out_dir, ec);

    const std::filesystem::path target = out_dir / (source.root().stem().string() + "-resaved.zip");
    std::filesystem::remove(target, ec);

    const pack::SaveResult saved = pack::buildZip(loaded, &source, target);
    if (!saved.ok) {
        std::printf("  SAVE FAIL  %s\n", saved.error.c_str());
        return 1;
    }

    std::string error;
    if (!pack::commitZip(target, &error)) {
        std::printf("  SAVE FAIL  %s\n", error.c_str());
        return 1;
    }

    std::unique_ptr<pack::DataSource> reopened = pack::open(target, &error);
    if (!reopened) {
        std::printf("  REOPEN FAIL  %s\n", error.c_str());
        return 1;
    }

    pack::Pack reloaded;
    if (!pack::loadPack(*reopened, &reloaded, &error)) {
        std::printf("  RELOAD FAIL  %s\n", error.c_str());
        return 1;
    }

    int failures = 0;

    if (reopened->entries().size() != source.entries().size()) {
        std::printf("  SAVE: entry count %zu -> %zu\n", source.entries().size(), reopened->entries().size());
        ++failures;
    }

    // Serialize both sides and compare: what we wrote must survive being read back and
    // written again unchanged.
    size_t identical = 0;
    for (size_t i = 0; i < loaded.files.size(); ++i) {
        size_t match = pack::kNoIndex;
        for (size_t j = 0; j < reloaded.files.size(); ++j) {
            if (reloaded.files[j].path == loaded.files[i].path) match = j;
        }
        if (match == pack::kNoIndex) {
            std::printf("  SAVE: %s missing after reload\n", loaded.files[i].path.c_str());
            ++failures;
            continue;
        }

        std::string before;
        std::string after;
        std::string ignored;
        pack::writeXmlFile(loaded, i, &before, &ignored);
        pack::writeXmlFile(reloaded, match, &after, &ignored);

        if (before == after) {
            ++identical;
        } else {
            std::printf("  SAVE: %s differs after a round trip\n", loaded.files[i].path.c_str());
            ++failures;
        }
    }

    const bool counts_match =
        reloaded.categories.size() == loaded.categories.size() && reloaded.pathables.size() == loaded.pathables.size();
    if (!counts_match) {
        std::printf(
            "  SAVE: model changed — %zu/%zu categories, %zu/%zu pathables\n",
            reloaded.categories.size(),
            loaded.categories.size(),
            reloaded.pathables.size(),
            loaded.pathables.size());
        ++failures;
    }

    std::printf(
        "  save round trip: %zu entries, %zu/%zu xml identical, model %s\n",
        reopened->entries().size(),
        identical,
        loaded.files.size(),
        counts_match ? "unchanged" : "CHANGED");

    // Left on disk on purpose: the point of --save-test is to be able to look at what the
    // writer produced.
    reopened.reset();
    return failures;
}

// Adding and removing a file is not an XML edit, so the round trip above says nothing
// about it: buildZip has to bring Pack::added_assets in and leave Pack::removed_assets out.
//
// Mutates `loaded` and puts it back the way it was, so the checks that follow are not
// looking at a pack with pending changes on it.
int probeAssetChanges(pack::Pack& loaded, const pack::DataSource& source, const std::filesystem::path& out_dir) {
    // Any non-XML entry will do; the writer does not care what is inside it.
    std::string victim;
    for (const std::string& entry : source.entries()) {
        if (entry.size() > 4 && entry.compare(entry.size() - 4, 4, ".xml") != 0) {
            victim = entry;
            break;
        }
    }
    if (victim.empty()) {
        std::printf("  asset changes: skipped, no non-xml entry to remove\n");
        return 0;
    }

    const std::string added = "markus-probe-added.bin";
    const std::vector<uint8_t> payload = {'m', 'p', 'e', 0x00, 0x01};

    loaded.addAsset(added, payload);
    loaded.removeAsset(victim);

    std::error_code ec;
    std::filesystem::create_directories(out_dir, ec);
    const std::filesystem::path target = out_dir / (source.root().stem().string() + "-assets.zip");
    std::filesystem::remove(target, ec);

    int failures = 0;
    const pack::SaveResult saved = pack::buildZip(loaded, &source, target);

    std::string error;
    if (!saved.ok) {
        std::printf("  ASSET FAIL  %s\n", saved.error.c_str());
        ++failures;
    } else if (!pack::commitZip(target, &error)) {
        std::printf("  ASSET FAIL  %s\n", error.c_str());
        ++failures;
    } else if (std::unique_ptr<pack::DataSource> reopened = pack::open(target, &error)) {
        if (!reopened->contains(added)) {
            std::printf("  ASSET: %s did not make it into the archive\n", added.c_str());
            ++failures;
        }
        if (reopened->contains(victim)) {
            std::printf("  ASSET: %s survived being removed\n", victim.c_str());
            ++failures;
        }

        std::vector<uint8_t> bytes;
        std::string read_error;
        if (reopened->read(added, &bytes, &read_error) && bytes != payload) {
            std::printf("  ASSET: %s came back with different bytes\n", added.c_str());
            ++failures;
        }

        const size_t expected = source.entries().size(); // one added, one removed
        if (reopened->entries().size() != expected) {
            std::printf("  ASSET: entry count %zu, expected %zu\n", reopened->entries().size(), expected);
            ++failures;
        }

        std::printf(
            "  asset changes: +%zu -%zu, %zu entries, removed %s\n",
            saved.assets_added,
            saved.assets_removed,
            reopened->entries().size(),
            victim.c_str());
    } else {
        std::printf("  ASSET REOPEN FAIL  %s\n", error.c_str());
        ++failures;
    }

    loaded.clearAssetChanges();
    return failures;
}

// Adding a marker is a node insert, which is the thing the writer became a serializer for.
// The check that matters is that it comes back: same count plus one, under the category it
// was put in, carrying the GUID it was given.
int probeAddPathable(pack::Pack& loaded, const pack::DataSource& source, const std::filesystem::path& out_dir) {
    // A category that already has markers, so the insert goes in beside a sibling rather
    // than into a fresh <POIs> block — the path the editor takes most of the time.
    size_t category = pack::kNoIndex;
    size_t after = pack::kNoIndex;
    for (size_t i = 0; i < loaded.pathables.size(); ++i) {
        if (loaded.pathables[i].category == pack::kNoIndex) continue;
        category = loaded.pathables[i].category;
        after = i;
    }
    if (category == pack::kNoIndex) {
        std::printf("  add pathable: skipped, no categorised marker to add beside\n");
        return 0;
    }

    const std::string guid = pack::generateGuid();

    pack::AttributeCollection attributes;
    attributes.set("type", loaded.categories[category].id);
    attributes.set("xpos", "1.5");
    attributes.set("ypos", "2.5");
    attributes.set("zpos", "3.5");
    attributes.set("GUID", guid);

    const size_t before = loaded.pathables.size();
    const size_t added =
        loaded.addPathable(pack::PathableType::Poi, category, after, loaded.pathables[after].source.file, std::move(attributes));
    if (added == pack::kNoIndex) {
        std::printf("  ADD FAIL  addPathable returned nothing\n");
        return 1;
    }

    std::error_code ec;
    std::filesystem::create_directories(out_dir, ec);
    const std::filesystem::path target = out_dir / (source.root().stem().string() + "-added.zip");
    std::filesystem::remove(target, ec);

    int failures = 0;
    const pack::SaveResult saved = pack::buildZip(loaded, &source, target);

    std::string error;
    if (!saved.ok) {
        std::printf("  ADD FAIL  %s\n", saved.error.c_str());
        return 1;
    }
    if (!pack::commitZip(target, &error)) {
        std::printf("  ADD FAIL  %s\n", error.c_str());
        return 1;
    }

    std::unique_ptr<pack::DataSource> reopened = pack::open(target, &error);
    pack::Pack reloaded;
    if (!reopened || !pack::loadPack(*reopened, &reloaded, &error)) {
        std::printf("  ADD REOPEN FAIL  %s\n", error.c_str());
        return 1;
    }

    if (reloaded.pathables.size() != before + 1) {
        std::printf("  ADD: %zu pathables after reload, expected %zu\n", reloaded.pathables.size(), before + 1);
        ++failures;
    }

    const pack::Pathable* found = nullptr;
    for (const pack::Pathable& pathable : reloaded.pathables) {
        const std::string* value = pathable.attributes.value("guid");
        if (value && *value == guid) found = &pathable;
    }

    if (!found) {
        std::printf("  ADD: the new marker did not come back\n");
        ++failures;
    } else {
        if (found->type != pack::PathableType::Poi) {
            std::printf("  ADD: came back as a %s\n", pack::toString(found->type));
            ++failures;
        }
        if (found->category != category) {
            std::printf("  ADD: landed in category %zu, expected %zu\n", found->category, category);
            ++failures;
        }
    }

    std::printf(
        "  add pathable: %zu -> %zu markers, new POI in %s\n",
        before,
        reloaded.pathables.size(),
        loaded.categories[category].id.c_str());
    return failures;
}

// Deleting tombstones the model entry and lifts the element out of the document. What has
// to hold: the marker is gone from the file, its category's *other* markers are not, and a
// deleted category takes its nested children with it.
int probeRemove(pack::Pack& loaded, const pack::DataSource& source, const std::filesystem::path& out_dir) {
    // A marker whose category holds more than one, so the check can tell "removed the one"
    // from "removed the lot".
    size_t victim = pack::kNoIndex;
    for (size_t i = 0; i < loaded.pathables.size() && victim == pack::kNoIndex; ++i) {
        if (loaded.pathables[i].grave.deleted || loaded.pathables[i].category == pack::kNoIndex) continue;
        size_t siblings = 0;
        for (const pack::Pathable& other : loaded.pathables) {
            if (!other.grave.deleted && other.category == loaded.pathables[i].category) ++siblings;
        }
        if (siblings > 1) victim = i;
    }
    if (victim == pack::kNoIndex) {
        std::printf("  remove: skipped, no category with two markers\n");
        return 0;
    }

    // A category with children, to prove the subtree goes as one.
    size_t category = pack::kNoIndex;
    for (size_t i = 0; i < loaded.categories.size(); ++i) {
        if (!loaded.categories[i].grave.deleted && loaded.categories[i].children.size() > 1) category = i;
    }

    const size_t victim_category = loaded.pathables[victim].category;
    const std::string* victim_guid = loaded.pathables[victim].attributes.value("guid");
    const std::string guid = victim_guid ? *victim_guid : std::string();

    // Every file as it stands now, to compare against after the delete is undone. Putting
    // an element back in the wrong place would show up here and nowhere else.
    std::vector<std::string> before_delete(loaded.files.size());
    for (size_t i = 0; i < loaded.files.size(); ++i) {
        std::string error;
        pack::writeXmlFile(loaded, i, &before_delete[i], &error);
    }

    size_t expected_pathables = 0;
    for (const pack::Pathable& pathable : loaded.pathables) {
        if (!pathable.grave.deleted) ++expected_pathables;
    }
    --expected_pathables; // the victim

    loaded.removePathable(victim);

    size_t removed_categories = 0;
    if (category != pack::kNoIndex) {
        std::vector<size_t> gone;
        // The editor removes a category's markers first; here the point is the elements, so
        // the markers are left to dangle in <POIs> exactly as the format allows.
        loaded.removeCategorySubtree(category, &gone);
        removed_categories = gone.size();
    }

    std::error_code ec;
    std::filesystem::create_directories(out_dir, ec);
    const std::filesystem::path target = out_dir / (source.root().stem().string() + "-removed.zip");
    std::filesystem::remove(target, ec);

    const pack::SaveResult saved = pack::buildZip(loaded, &source, target);
    std::string error;
    if (!saved.ok) {
        std::printf("  REMOVE FAIL  %s\n", saved.error.c_str());
        return 1;
    }
    if (!pack::commitZip(target, &error)) {
        std::printf("  REMOVE FAIL  %s\n", error.c_str());
        return 1;
    }

    std::unique_ptr<pack::DataSource> reopened = pack::open(target, &error);
    pack::Pack reloaded;
    if (!reopened || !pack::loadPack(*reopened, &reloaded, &error)) {
        std::printf("  REMOVE REOPEN FAIL  %s\n", error.c_str());
        return 1;
    }

    int failures = 0;

    if (!guid.empty()) {
        for (const pack::Pathable& pathable : reloaded.pathables) {
            const std::string* value = pathable.attributes.value("guid");
            if (value && *value == guid) {
                std::printf("  REMOVE: the deleted marker came back\n");
                ++failures;
                break;
            }
        }
    }

    // Only what was asked for: its siblings have to survive.
    if (category == pack::kNoIndex && reloaded.pathables.size() != expected_pathables) {
        std::printf("  REMOVE: %zu markers after reload, expected %zu\n", reloaded.pathables.size(), expected_pathables);
        ++failures;
    }

    if (category != pack::kNoIndex) {
        const std::string& id = loaded.categories[category].id;
        if (reloaded.findCategory(id) != nullptr) {
            std::printf("  REMOVE: category %s came back\n", id.c_str());
            ++failures;
        }
    }

    // Undo: everything goes back exactly where it was, which is the whole point of keeping
    // the element rather than rebuilding it.
    loaded.restorePathable(victim);
    if (category != pack::kNoIndex) loaded.restoreCategorySubtree(category, nullptr);

    size_t identical = 0;
    for (size_t i = 0; i < loaded.files.size(); ++i) {
        std::string after;
        std::string error;
        pack::writeXmlFile(loaded, i, &after, &error);
        if (after == before_delete[i]) {
            ++identical;
        } else {
            std::printf("  REMOVE: %s differs after delete + undo\n", loaded.files[i].path.c_str());
            ++failures;
        }
    }

    std::printf(
        "  remove: 1 marker (of %zu in %s) and %zu categories, reloads to %zu markers, %zu categories; undo restores %zu/%zu files\n",
        expected_pathables + 1,
        loaded.categories[victim_category].id.c_str(),
        removed_categories,
        reloaded.pathables.size(),
        reloaded.categories.size(),
        identical,
        loaded.files.size());
    return failures;
}

// A new category is only useful if things can be filed under it, so this adds one and puts
// a marker in it — the way a pack actually grows a branch — and requires both to come back
// with the marker resolving to the category.
int probeAddCategory(pack::Pack& loaded, const pack::DataSource& source, const std::filesystem::path& out_dir) {
    size_t parent = pack::kNoIndex;
    for (size_t i = 0; i < loaded.categories.size(); ++i) {
        if (!loaded.categories[i].grave.deleted && !loaded.categories[i].declarations.empty()) parent = i;
    }
    if (parent == pack::kNoIndex) {
        std::printf("  add category: skipped, no category to nest under\n");
        return 0;
    }

    const size_t file_index = loaded.categories[parent].declarations.back().file;
    const size_t added = loaded.addCategory(parent, "markusprobe", file_index);
    if (added == pack::kNoIndex) {
        std::printf("  ADD CATEGORY FAIL  addCategory returned nothing\n");
        return 1;
    }

    const std::string id = loaded.categories[added].id;

    pack::AttributeCollection attributes;
    attributes.set("type", id);
    attributes.set("xpos", "4.5");
    attributes.set("ypos", "5.5");
    attributes.set("zpos", "6.5");
    if (loaded.addPathable(pack::PathableType::Poi, added, pack::kNoIndex, file_index, std::move(attributes)) == pack::kNoIndex) {
        std::printf("  ADD CATEGORY FAIL  could not add a marker to the new category\n");
        return 1;
    }

    std::error_code ec;
    std::filesystem::create_directories(out_dir, ec);
    const std::filesystem::path target = out_dir / (source.root().stem().string() + "-category.zip");
    std::filesystem::remove(target, ec);

    const pack::SaveResult saved = pack::buildZip(loaded, &source, target);
    std::string error;
    if (!saved.ok) {
        std::printf("  ADD CATEGORY FAIL  %s\n", saved.error.c_str());
        return 1;
    }
    if (!pack::commitZip(target, &error)) {
        std::printf("  ADD CATEGORY FAIL  %s\n", error.c_str());
        return 1;
    }

    std::unique_ptr<pack::DataSource> reopened = pack::open(target, &error);
    pack::Pack reloaded;
    if (!reopened || !pack::loadPack(*reopened, &reloaded, &error)) {
        std::printf("  ADD CATEGORY REOPEN FAIL  %s\n", error.c_str());
        return 1;
    }

    int failures = 0;

    const pack::Category* found = reloaded.findCategory(id);
    if (!found) {
        std::printf("  ADD CATEGORY: %s did not come back\n", id.c_str());
        return failures + 1;
    }

    size_t filed_under_it = 0;
    for (const pack::Pathable& pathable : reloaded.pathables) {
        if (pathable.category_id == id) ++filed_under_it;
    }
    if (filed_under_it != 1) {
        std::printf("  ADD CATEGORY: %zu markers under %s, expected 1\n", filed_under_it, id.c_str());
        ++failures;
    }

    std::printf("  add category: %s, %zu marker under it\n", id.c_str(), filed_under_it);
    return failures;
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

// A pack created in the editor has no DataSource, so its save is the one path that writes
// an archive out of nothing but the model. Needs no input pack, so it runs once.
int probeNewPack(const std::filesystem::path& out_dir) {
    pack::Pack created = pack::createPack("markers.xml", "markusprobe", "Markus Probe");
    if (created.categories.size() != 1 || created.files.size() != 1) {
        std::printf("  NEW PACK FAIL  createPack gave %zu files, %zu categories\n", created.files.size(), created.categories.size());
        return 1;
    }

    const size_t child = created.addCategory(0, "branch", 0);
    if (child == pack::kNoIndex) {
        std::printf("  NEW PACK FAIL  could not add a category to it\n");
        return 1;
    }

    pack::AttributeCollection attributes;
    attributes.set("type", created.categories[child].id);
    attributes.set("xpos", "1");
    attributes.set("ypos", "2");
    attributes.set("zpos", "3");
    if (created.addPathable(pack::PathableType::Poi, child, pack::kNoIndex, 0, std::move(attributes)) == pack::kNoIndex) {
        std::printf("  NEW PACK FAIL  could not add a marker to it\n");
        return 1;
    }

    std::error_code ec;
    std::filesystem::create_directories(out_dir, ec);
    const std::filesystem::path target = out_dir / "new-pack.zip";
    std::filesystem::remove(target, ec);

    // No source: everything in the archive comes from the model.
    const pack::SaveResult saved = pack::buildZip(created, nullptr, target);
    std::string error;
    if (!saved.ok) {
        std::printf("  NEW PACK FAIL  %s\n", saved.error.c_str());
        return 1;
    }
    if (!pack::commitZip(target, &error)) {
        std::printf("  NEW PACK FAIL  %s\n", error.c_str());
        return 1;
    }

    std::unique_ptr<pack::DataSource> reopened = pack::open(target, &error);
    pack::Pack reloaded;
    if (!reopened || !pack::loadPack(*reopened, &reloaded, &error)) {
        std::printf("  NEW PACK REOPEN FAIL  %s\n", error.c_str());
        return 1;
    }

    int failures = 0;
    if (reloaded.categories.size() != 2 || reloaded.pathables.size() != 1) {
        std::printf(
            "  NEW PACK: reloaded %zu categories and %zu markers, expected 2 and 1\n",
            reloaded.categories.size(),
            reloaded.pathables.size());
        ++failures;
    }
    if (reloaded.pathables.size() == 1 && reloaded.pathables[0].category == pack::kNoIndex) {
        std::printf("  NEW PACK: the marker's type did not resolve\n");
        ++failures;
    }

    const pack::Category* root = reloaded.findCategory("markusprobe");
    if (!root) {
        std::printf("  NEW PACK: the root category did not come back\n");
        ++failures;
    } else if (const std::string* display = root->attributes.value("displayname"); !display || *display != "Markus Probe") {
        std::printf("  NEW PACK: DisplayName did not survive\n");
        ++failures;
    }

    std::printf(
        "\n=== new pack ===\n  %zu entries, %zu categories, %zu markers, reopens clean\n",
        reopened->entries().size(),
        reloaded.categories.size(),
        reloaded.pathables.size());
    return failures;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::filesystem::path> packs;

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--save-test" && i + 1 < argc) {
            save_test_dir = argv[++i];
            continue;
        }
        packs.push_back(argument);
    }

    if (packs.empty()) {
        std::printf("usage: markus_probe [--save-test <dir>] <pack.zip|folder> [more ...]\n");
        return 2;
    }

    int failures = 0;
    for (const std::filesystem::path& pack : packs) {
        failures += probePack(pack);
    }

    if (!save_test_dir.empty()) failures += probeNewPack(save_test_dir);

    std::printf("\n");
    return failures == 0 ? 0 : 1;
}
