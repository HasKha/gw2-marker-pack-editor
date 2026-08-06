// Invariants that must hold for every real pack in the corpus.
//
// These are the claims docs/plan.md makes about the reader and the writer, turned into
// assertions. They ran as printf output from mpe_probe before, which meant reading nine
// blocks of numbers and remembering what they were last time.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <pugixml.hpp>

#include <memory>
#include <string>
#include <vector>

#include "corpus.h"
#include "pack/source.h"
#include "pack/writer.h"
#include "pack/xml_reader.h"

namespace {

struct Loaded {
    std::unique_ptr<pack::DataSource> source;
    pack::Pack model;
};

Loaded load(const std::filesystem::path& path) {
    Loaded out;

    std::string error;
    out.source = pack::open(path, &error);
    REQUIRE(out.source != nullptr);
    REQUIRE(pack::loadPack(*out.source, &out.model, &error));

    return out;
}

// Every XML file of the pack, serialized through the writer.
std::vector<std::string> rewriteAll(const pack::Pack& model) {
    std::vector<std::string> out;
    out.reserve(model.files.size());

    for (size_t i = 0; i < model.files.size(); ++i) {
        std::string text;
        std::string error;
        REQUIRE(pack::writeXmlFile(model, i, &text, &error));
        out.push_back(std::move(text));
    }
    return out;
}

size_t countComments(const pugi::xml_node& node) {
    size_t found = 0;
    for (pugi::xml_node child : node.children()) {
        if (child.type() == pugi::node_comment) ++found;
        found += countComments(child);
    }
    return found;
}

} // namespace

TEST_CASE("the pack corpus is present", "[corpus]") {
    INFO(corpus::missingMessage());
    REQUIRE_FALSE(corpus::dir().empty());
    REQUIRE_FALSE(corpus::packs().empty());
}

TEST_CASE("every pack in the corpus holds its invariants", "[corpus]") {
    const std::vector<std::filesystem::path> packs = corpus::packs();
    if (packs.empty()) SKIP(corpus::missingMessage());

    for (const std::filesystem::path& path : packs) {
        // One section per pack, so a failure names the pack rather than an index.
        DYNAMIC_SECTION("pack " << path.filename().string()) {
            const Loaded loaded = load(path);
            const pack::Pack& model = loaded.model;

            // Reading a pack is not allowed to fail outright. Warnings are fine — packs in
            // the wild have plenty — but a file that would not parse is a reader bug.
            CHECK_FALSE(model.files.empty());
            for (const pack::XmlFile& file : model.files) {
                INFO("file " << file.path);
                CHECK(file.document != nullptr);
            }

            // Every marker has to land under a category or in the orphan list; a category
            // index pointing past the end would crash the tree on the first draw.
            for (const pack::Pathable& pathable : model.pathables) {
                CHECK((pathable.category == pack::kNoIndex || pathable.category < model.categories.size()));
            }
            for (size_t i = 0; i < model.categories.size(); ++i) {
                const size_t parent = model.categories[i].parent;
                CHECK((parent == pack::kNoIndex || parent < model.categories.size()));
                // A child always follows its parent, which is what lets rebuildVisibility
                // resolve the whole tree in one forward pass.
                if (parent != pack::kNoIndex) CHECK(parent < i);
            }

            // ── The writer is stable ──────────────────────────────────────────
            // A pack reformats once, on its first save, and is byte-identical from then
            // on. That is the deal docs/architecture.md strikes in "Saving serializes, it
            // does not diff", and it is what makes the reformat acceptable.
            const std::vector<std::string> first = rewriteAll(model);

            pack::Pack reparsed;
            reparsed.files.resize(model.files.size());
            for (size_t i = 0; i < model.files.size(); ++i) {
                reparsed.files[i].path = model.files[i].path;
                reparsed.files[i].original_path = model.files[i].original_path;
                reparsed.files[i].format = model.files[i].format;
                reparsed.files[i].document = std::make_unique<pugi::xml_document>();
                REQUIRE(reparsed.files[i].document->load_buffer(first[i].data(), first[i].size(), pugi::parse_full));
            }
            for (size_t i = 0; i < reparsed.files.size(); ++i) pack::readXmlFile(&reparsed, i);

            const std::vector<std::string> second = rewriteAll(reparsed);

            REQUIRE(second.size() == first.size());
            for (size_t i = 0; i < first.size(); ++i) {
                INFO("file " << model.files[i].path);
                CHECK(first[i] == second[i]);
            }

            // ── What a save must not lose ─────────────────────────────────────
            for (size_t i = 0; i < model.files.size(); ++i) {
                INFO("file " << model.files[i].path);

                // Comments are the thing a hand-authored pack cannot afford to lose, and
                // the reason the writer is not TmfLib's PackWriter.
                CHECK(countComments(*reparsed.files[i].document) == countComments(*model.files[i].document));

                // pugixml cannot tell us these from the tree, so they are carried on
                // FileFormat and have to come back out the other side.
                const bool declared = first[i].find("<?xml") != std::string::npos;
                CHECK(declared == model.files[i].format.declaration);

                const bool has_bom = first[i].size() >= 3 && static_cast<unsigned char>(first[i][0]) == 0xEF &&
                                     static_cast<unsigned char>(first[i][1]) == 0xBB && static_cast<unsigned char>(first[i][2]) == 0xBF;
                CHECK(has_bom == model.files[i].format.bom);

                if (model.files[i].format.crlf) {
                    // Every newline paired, none left bare.
                    for (size_t at = first[i].find('\n'); at != std::string::npos; at = first[i].find('\n', at + 1)) {
                        REQUIRE(at > 0);
                        REQUIRE(first[i][at - 1] == '\r');
                    }
                }
            }

            // The model that comes back must be the same model, or the round trip has
            // quietly changed the pack's meaning.
            CHECK(reparsed.categories.size() == model.categories.size());
            CHECK(reparsed.pathables.size() == model.pathables.size());
            CHECK(reparsed.roots.size() == model.roots.size());

            for (size_t i = 0; i < model.categories.size() && i < reparsed.categories.size(); ++i) {
                INFO("category " << model.categories[i].id);
                CHECK(reparsed.categories[i].id == model.categories[i].id);
                // Order and original spelling are part of what round-trips, so comparing
                // sizes alone would miss a reordering.
                REQUIRE(reparsed.categories[i].attributes.size() == model.categories[i].attributes.size());
                for (size_t a = 0; a < model.categories[i].attributes.all().size(); ++a) {
                    CHECK(reparsed.categories[i].attributes.all()[a].name == model.categories[i].attributes.all()[a].name);
                    CHECK(reparsed.categories[i].attributes.all()[a].value == model.categories[i].attributes.all()[a].value);
                }
            }

            for (size_t i = 0; i < model.pathables.size() && i < reparsed.pathables.size(); ++i) {
                INFO("pathable " << i << " of type " << model.pathables[i].category_id);
                CHECK(reparsed.pathables[i].type == model.pathables[i].type);
                CHECK(reparsed.pathables[i].category_id == model.pathables[i].category_id);
                REQUIRE(reparsed.pathables[i].attributes.size() == model.pathables[i].attributes.size());
                for (size_t a = 0; a < model.pathables[i].attributes.all().size(); ++a) {
                    CHECK(reparsed.pathables[i].attributes.all()[a].name == model.pathables[i].attributes.all()[a].name);
                    CHECK(reparsed.pathables[i].attributes.all()[a].value == model.pathables[i].attributes.all()[a].value);
                }
            }
        }
    }
}

TEST_CASE("a folder pack and the zip built from it give the same model", "[corpus]") {
    // The point of the DirectorySource is editing a pack's source tree in place, which is
    // only safe if it resolves to exactly what a zip of that same tree does.
    //
    // The zip is built here rather than picked out of the corpus. Comparing a checkout to a
    // separately shipped archive of "the same" pack compares two copies that drift the
    // moment either is edited, and a red test that means "you have not re-exported lately"
    // is a test nobody can read. Zipping the folder we just loaded cannot drift.
    const std::filesystem::path folder = corpus::folderPack();
    if (folder.empty()) SKIP("no unzipped pack available; set MPE_TEST_FOLDER_PACK");

    const Loaded from_folder = load(folder);
    CHECK(from_folder.model.from_directory);

    const corpus::TempDir temp("folder_vs_zip");
    const std::filesystem::path target = temp / "from_folder.zip";

    // The editor's own save path: everything the reader did not touch is copied across out
    // of the folder source, which is exactly the part a DirectorySource has to get right.
    const pack::SaveResult built = pack::buildZip(from_folder.model, from_folder.source.get(), target);
    INFO("buildZip: " << built.error);
    REQUIRE(built.ok);

    std::string error;
    INFO("commitZip: " << error);
    REQUIRE(pack::commitZip(target, &error));

    const Loaded from_zip = load(target);
    CHECK_FALSE(from_zip.model.from_directory);

    CHECK(from_folder.model.categories.size() == from_zip.model.categories.size());
    CHECK(from_folder.model.files.size() == from_zip.model.files.size());
    CHECK(from_folder.model.pathables.size() == from_zip.model.pathables.size());

    // Category ids are what every reference into the pack resolves through, so they are
    // the part that has to agree exactly. Order counts too: a folder is walked and a zip is
    // indexed, and if those two disagree the pack comes out shuffled.
    for (size_t i = 0; i < from_zip.model.categories.size() && i < from_folder.model.categories.size(); ++i) {
        CHECK(from_folder.model.categories[i].id == from_zip.model.categories[i].id);
    }

    for (size_t i = 0; i < from_zip.model.pathables.size() && i < from_folder.model.pathables.size(); ++i) {
        INFO("pathable " << i);
        CHECK(from_folder.model.pathables[i].type == from_zip.model.pathables[i].type);
        CHECK(from_folder.model.pathables[i].category_id == from_zip.model.pathables[i].category_id);
    }
}
