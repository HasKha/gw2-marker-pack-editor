// Saving: rebuild the archive, put it in place, read it back, and check the pack that
// comes out is the pack that went in.
//
// This is the path that touches real files, so every case writes into a temporary
// directory of its own and cleans up after itself.

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

#include "corpus.h"
#include "pack/source.h"
#include "pack/trl.h"
#include "pack/writer.h"
#include "pack/xml_reader.h"
#include "util/path_text.h"

namespace {

using corpus::TempDir;

struct Loaded {
    std::unique_ptr<pack::DataSource> source;
    pack::Pack model;
};

Loaded load(const std::filesystem::path& path) {
    Loaded out;

    std::string error;
    out.source = pack::open(path, &error);
    INFO("opening " << util::pathText(path) << ": " << error);
    REQUIRE(out.source != nullptr);
    out.model = pack::loadPack(*out.source);

    return out;
}

// buildZip then commitZip, which is what the editor does — building beside the target
// first is what stops a failure replacing a working pack with a half-written one.
void saveTo(const pack::Pack& model, const pack::DataSource* source, const std::filesystem::path& target) {
    const pack::SaveResult result = pack::buildZip(model, source, target);
    INFO("buildZip: " << result.error);
    REQUIRE(result.ok);

    std::string error;
    INFO("commitZip: " << error);
    REQUIRE(pack::commitZip(target, &error));
}

} // namespace

TEST_CASE("a pack survives a save and reopen", "[save]") {
    // The hand-authored one: comments throughout, ten files at the root, and the pack the
    // editor exists to edit. If anything is going to be lost, it is lost here.
    const std::filesystem::path path = corpus::pack("haskha_markers.zip");
    if (path.empty()) SKIP("haskha_markers.zip is not in the corpus");

    const TempDir temp("save_reopen");
    const std::filesystem::path target = temp / "resaved.zip";

    const Loaded original = load(path);
    saveTo(original.model, original.source.get(), target);

    const Loaded reopened = load(target);

    CHECK(reopened.model.categories.size() == original.model.categories.size());
    CHECK(reopened.model.pathables.size() == original.model.pathables.size());
    CHECK(reopened.model.files.size() == original.model.files.size());

    // Not just the counts: every marker has to come back under the same category, or the
    // save has rearranged the pack.
    for (size_t i = 0; i < original.model.pathables.size() && i < reopened.model.pathables.size(); ++i) {
        INFO("pathable " << i);
        CHECK(reopened.model.pathables[i].category_id == original.model.pathables[i].category_id);
        CHECK(reopened.model.pathables[i].type == original.model.pathables[i].type);
    }

    // Everything else in the archive has to come along too, or the icons stop resolving.
    CHECK(reopened.source->entries().size() == original.source->entries().size());
}

TEST_CASE("saving twice produces the same archive contents", "[save]") {
    // The reformat happens once. A second save of an untouched pack must not keep
    // rewriting it, or every save would show up as a diff in the author's git history.
    const std::filesystem::path path = corpus::pack("haskha_markers.zip");
    if (path.empty()) SKIP("haskha_markers.zip is not in the corpus");

    const TempDir temp("save_twice");

    const Loaded original = load(path);
    saveTo(original.model, original.source.get(), temp / "once.zip");

    const Loaded once = load(temp / "once.zip");
    saveTo(once.model, once.source.get(), temp / "twice.zip");

    const Loaded twice = load(temp / "twice.zip");

    REQUIRE(once.source->entries().size() == twice.source->entries().size());
    for (const std::string& entry : once.source->entries()) {
        INFO("entry " << entry);

        std::vector<uint8_t> before;
        std::vector<uint8_t> after;
        std::string error;
        REQUIRE(once.source->read(entry, &before, &error));
        REQUIRE(twice.source->read(entry, &after, &error));
        CHECK(before == after);
    }
}

TEST_CASE("a pack open for reading can be saved over, and still reads what it opened", "[save][regression]") {
    // The viewer keeps every installed pack open while the editor saves the same file: the save
    // moves it aside, which Windows refuses while any handle does not share deletion.
    const TempDir temp("save_over_open");
    const std::filesystem::path target = temp / "pack.zip";
    pack::Pack created = pack::createPack("markers.xml", "first", "First");
    saveTo(created, nullptr, target);

    const Loaded open = load(target);
    pack::Pack changed = pack::createPack("markers.xml", "second", "Second");
    saveTo(changed, nullptr, target);

    CHECK(load(target).model.findCategory("second") != nullptr);
    std::vector<uint8_t> bytes;
    std::string error;
    CHECK(open.source->read("markers.xml", &bytes, &error));
    CHECK(std::string(bytes.begin(), bytes.end()).find("first") != std::string::npos);
}

TEST_CASE("a pack built from nothing saves and reopens", "[save]") {
    // A pack created in the editor has no DataSource at all, so this is the one save path
    // that writes an archive out of the model alone.
    const TempDir temp("new_pack");
    const std::filesystem::path target = temp / "new-pack.zip";

    pack::Pack created = pack::createPack("markers.xml", "markustest", "Markus Test");
    REQUIRE(created.files.size() == 1);
    REQUIRE(created.categories.size() == 1);

    const size_t child = created.addCategory(0, "branch", 0);
    REQUIRE(child != pack::kNoIndex);

    pack::AttributeCollection attributes;
    attributes.set("type", created.categories[child].id);
    attributes.set("xpos", "1");
    attributes.set("ypos", "2");
    attributes.set("zpos", "3");
    REQUIRE(created.addPathable(pack::PathableType::Poi, child, pack::kNoIndex, 0, std::move(attributes)) != pack::kNoIndex);

    saveTo(created, nullptr, target);

    const Loaded reopened = load(target);
    CHECK(reopened.model.categories.size() == 2);
    REQUIRE(reopened.model.pathables.size() == 1);
    // The marker's type has to resolve, or it comes back as an orphan under "Unresolved".
    CHECK(reopened.model.pathables[0].category != pack::kNoIndex);

    const pack::Category* root = reopened.model.findCategory("markustest");
    REQUIRE(root != nullptr);
    const std::string* display = root->attributes.value("displayname");
    REQUIRE(display != nullptr);
    CHECK(*display == "Markus Test");
}

TEST_CASE("pending asset changes are applied by a save", "[save]") {
    const std::filesystem::path path = corpus::pack("haskha_markers.zip");
    if (path.empty()) SKIP("haskha_markers.zip is not in the corpus");

    const TempDir temp("assets");
    const std::filesystem::path target = temp / "assets.zip";

    Loaded original = load(path);

    // Something that was there, gone.
    std::string removed;
    for (const std::string& entry : original.source->entries()) {
        if (entry.size() > 4 && entry.compare(entry.size() - 4, 4, ".png") == 0) {
            removed = entry;
            break;
        }
    }
    REQUIRE_FALSE(removed.empty());
    original.model.removeAsset(removed);

    // Something that was not, added.
    const std::vector<uint8_t> bytes{1, 2, 3, 4, 5};
    original.model.addAsset("Data/markus_test_added.bin", bytes);

    saveTo(original.model, original.source.get(), target);

    const Loaded reopened = load(target);
    CHECK_FALSE(reopened.source->contains(removed));
    REQUIRE(reopened.source->contains("data/markus_test_added.bin"));

    std::vector<uint8_t> read_back;
    std::string error;
    REQUIRE(reopened.source->read("data/markus_test_added.bin", &read_back, &error));
    CHECK(read_back == bytes);
}

TEST_CASE("an entry name longer than a fixed buffer survives a save", "[save][regression]") {
    // A name read into a fixed buffer would be truncated by miniz, and a save would write the
    // file back under the truncated name.
    const TempDir temp("long_name");
    const std::filesystem::path target = temp / "long-name.zip";

    pack::Pack created = pack::createPack("markers.xml", "markustest", "Markus Test");

    std::string deep;
    while (deep.size() < 600) deep += "a_reasonably_long_directory_segment/";
    deep += "icon.png";
    REQUIRE(deep.size() > 512);

    const std::vector<uint8_t> bytes{9, 8, 7};
    created.addAsset(deep, bytes);

    saveTo(created, nullptr, target);

    const Loaded reopened = load(target);
    const std::string key = pack::normalizePath(deep);
    REQUIRE(reopened.source->contains(key));

    std::vector<uint8_t> read_back;
    std::string error;
    REQUIRE(reopened.source->read(key, &read_back, &error));
    CHECK(read_back == bytes);
    CHECK(reopened.source->originalName(key) == deep);
}

TEST_CASE("a trail written by the editor reads back point for point", "[save][trl]") {
    pack::trl::Trail trail;
    trail.map_id = 38;

    pack::trl::Segment first;
    first.points = {{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}};
    pack::trl::Segment second;
    second.points = {{7.0f, 8.0f, 9.0f}};
    trail.segments = {first, second};

    const std::vector<uint8_t> bytes = pack::trl::write(trail);

    pack::trl::Trail read_back;
    std::string error;
    REQUIRE(pack::trl::read(bytes, &read_back, &error));

    CHECK(read_back.map_id == 38);
    // Two runs have to stay two runs: they are drawn as separate ribbons, and a lost
    // terminator would join them with a line across the map.
    REQUIRE(read_back.segments.size() == 2);
    CHECK(read_back.segments[0].points == first.points);
    CHECK(read_back.segments[1].points == second.points);
    CHECK(read_back.pointCount() == 3);

    // Writing what was just read must not drift.
    CHECK(pack::trl::write(read_back) == bytes);
}

TEST_CASE("a pack saves into a folder with a non-ASCII name", "[save][regression]") {
    // miniz opens a name as UTF-8. "Jörg Łódź": ö in the ANSI code page is not UTF-8, and Ł is
    // in no Western code page, where path::string() throws.
    const corpus::TempDir temp("non_ascii_folder");
    const std::filesystem::path folder = temp.path() / util::pathFromText(
                                                           "J\xC3\xB6rg \xC5\x81\xC3\xB3"
                                                           "d\xC5\xBA");
    std::filesystem::create_directories(folder);
    const std::filesystem::path target = folder / "pack.zip";

    const pack::Pack created = pack::createPack("markers.xml", "markustest", "Markus Test");
    saveTo(created, nullptr, target);
    CHECK(load(target).model.categories.size() == 1);
}
