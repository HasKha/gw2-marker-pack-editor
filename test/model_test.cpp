// The attribute table, value parsing, and the model operations the editor is built out of.
// Needs no corpus: these are the pack layer's own rules.

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "pack/attributes.h"
#include "pack/model.h"
#include "pack/source.h"

TEST_CASE("attribute lookup is case-insensitive whatever spelling it is given", "[attributes]") {
    pack::AttributeCollection attributes;
    attributes.set("fadeNear", "10");

    // find() takes a fast path when the name is already a lookup key and a normalizing one
    // when it is not. The two have to agree, or a lookup would depend on how the caller
    // happened to spell its literal.
    for (const char* spelling : {"fadenear", "fadeNear", "FADENEAR", "  fadeNear  "}) {
        INFO("spelling " << spelling);
        const pack::Attribute* found = attributes.find(spelling);
        REQUIRE(found != nullptr);
        CHECK(found->value == "10");
    }

    CHECK(attributes.find("fadefar") == nullptr);
}

TEST_CASE("isAttributeKey only claims names attributeKey would leave alone", "[attributes][regression]") {
    // The whole point of the fast path is that it is safe: anything it says yes to must
    // already equal attributeKey's output, or a lookup skips normalization it needed.
    const std::vector<std::string> names =
        {"fadenear", "fadeNear", "FadeNear", "rotate-x", "  spaced  ", "", "bh-color", "tip-description", "TIP-DESCRIPTION", "a b"};

    for (const std::string& name : names) {
        INFO("name '" << name << "'");
        if (pack::isAttributeKey(name)) CHECK(pack::attributeKey(name) == name);
    }
}

TEST_CASE("setting an attribute keeps the spelling already in the file", "[attributes]") {
    pack::AttributeCollection attributes;
    attributes.set("fadeNear", "10");
    attributes.set("FADENEAR", "20");

    REQUIRE(attributes.size() == 1);
    // The author chose "fadeNear"; an edit changes the value, not how it is written.
    CHECK(attributes.all()[0].name == "fadeNear");
    CHECK(attributes.all()[0].value == "20");
}

TEST_CASE("attributes keep their source order", "[attributes]") {
    pack::AttributeCollection attributes;
    attributes.set("type", "a.b");
    attributes.set("xpos", "1");
    attributes.set("guid", "x");

    REQUIRE(attributes.size() == 3);
    CHECK(attributes.all()[0].name == "type");
    CHECK(attributes.all()[1].name == "xpos");
    CHECK(attributes.all()[2].name == "guid");

    // Replacing a value must not move it to the end.
    attributes.set("xpos", "99");
    CHECK(attributes.all()[1].name == "xpos");
    CHECK(attributes.all()[1].value == "99");

    CHECK(attributes.erase("XPOS"));
    REQUIRE(attributes.size() == 2);
    CHECK(attributes.all()[1].name == "guid");
    CHECK_FALSE(attributes.erase("xpos"));
}

TEST_CASE("every attribute table row is well formed", "[attributes]") {
    // The table is the one place an attribute is defined, so a typo in it is a bug
    // everywhere at once.
    std::vector<std::string_view> seen;
    for (const pack::AttributeDef& def : pack::attributeTable()) {
        INFO("attribute " << def.key);

        CHECK_FALSE(def.key.empty());
        CHECK_FALSE(def.display.empty());
        CHECK_FALSE(def.doc.empty());
        CHECK(def.applies_to != 0);

        // The key is what lookups hash on; it has to be the canonical form.
        CHECK(pack::attributeKey(def.key) == std::string(def.key));
        // And the display spelling has to be the same attribute, differently cased.
        CHECK(pack::attributeKey(def.display) == std::string(def.key));

        CHECK(std::find(seen.begin(), seen.end(), def.key) == seen.end());
        seen.push_back(def.key);

        CHECK(pack::findAttribute(def.key) == &def);
        CHECK(pack::findAttribute(def.display) == &def);
    }
}

TEST_CASE("declared defaults parse as their own kind", "[attributes]") {
    for (const pack::AttributeDef& def : pack::attributeTable()) {
        if (def.default_value.empty()) continue;
        INFO("attribute " << def.key << " default '" << def.default_value << "'");

        switch (def.kind) {
        case pack::ValueKind::Int: {
            int value = 0;
            CHECK(pack::parseInt(def.default_value, &value));
            break;
        }
        case pack::ValueKind::Float: {
            float value = 0.0f;
            CHECK(pack::parseFloat(def.default_value, &value));
            break;
        }
        case pack::ValueKind::Bool: {
            bool value = false;
            CHECK(pack::parseBool(def.default_value, &value));
            break;
        }
        case pack::ValueKind::Color: {
            uint32_t value = 0;
            CHECK(pack::parseColor(def.default_value, &value));
            break;
        }
        default: break;
        }
    }
}

TEST_CASE("value parsing matches what the references accept", "[attributes]") {
    SECTION("bools") {
        bool value = false;
        for (const char* text : {"1", "true", "TRUE", "yes", "7"}) {
            INFO(text);
            REQUIRE(pack::parseBool(text, &value));
            CHECK(value);
        }
        for (const char* text : {"0", "false", "no"}) {
            INFO(text);
            REQUIRE(pack::parseBool(text, &value));
            CHECK_FALSE(value);
        }
        CHECK_FALSE(pack::parseBool("maybe", &value));
    }

    SECTION("colours") {
        uint32_t argb = 0;
        REQUIRE(pack::parseColor("FF8040", &argb));
        // Six digits is RRGGBB, so alpha is supplied as opaque.
        CHECK(argb == 0xFFFF8040u);

        REQUIRE(pack::parseColor("#80FF8040", &argb));
        CHECK(argb == 0x80FF8040u);

        CHECK_FALSE(pack::parseColor("FF80", &argb));
        CHECK_FALSE(pack::parseColor("GGGGGG", &argb));
    }

    SECTION("floats are locale-independent") {
        float value = 0.0f;
        REQUIRE(pack::parseFloat("-118.300", &value));
        CHECK(value == -118.3f);
        CHECK_FALSE(pack::parseFloat("", &value));
        CHECK_FALSE(pack::parseFloat("abc", &value));
    }

    SECTION("guids round trip through base64") {
        const std::string generated = pack::generateGuid();
        std::array<uint8_t, 16> bytes{};
        REQUIRE(pack::parseGuid(generated, &bytes));
        CHECK(pack::guidToBase64(bytes) == generated);

        // Two markers sharing a GUID would toggle each other away.
        CHECK(pack::generateGuid() != generated);
    }
}

TEST_CASE("resource paths normalize the way TmfLib normalizes them", "[source]") {
    // Blish HUD resolves every icon reference through this, so a mismatch means an icon
    // that works in game silently goes missing here.
    CHECK(pack::normalizePath("Data\\Icons\\X.PNG") == "data/icons/x.png");
    CHECK(pack::normalizePath("Data//Icons//x.png") == "data/icons/x.png");
    CHECK(pack::normalizePath("  Data/x.png  ") == "data/x.png");
    CHECK(pack::normalizePath("./Data/x.png") == "/data/x.png");
    CHECK(pack::normalizePath("") == "");
}

TEST_CASE("a marker inherits through the category chain", "[model]") {
    pack::Pack model = pack::createPack("markers.xml", "root", "Root");
    REQUIRE(model.categories.size() == 1);

    const size_t child = model.addCategory(0, "child", 0);
    REQUIRE(child != pack::kNoIndex);

    model.categories[0].attributes.set("iconFile", "root.png");
    model.categories[0].attributes.set("iconSize", "2");
    model.categories[child].attributes.set("iconFile", "child.png");

    pack::AttributeCollection attributes;
    attributes.set("type", model.categories[child].id);
    attributes.set("xpos", "0");
    attributes.set("ypos", "0");
    attributes.set("zpos", "0");
    const size_t marker = model.addPathable(pack::PathableType::Poi, child, pack::kNoIndex, 0, std::move(attributes));
    REQUIRE(marker != pack::kNoIndex);

    const pack::Pathable& pathable = model.pathables[marker];

    // The nearest category wins.
    const std::string* icon = model.effectiveValue(pathable, "iconfile");
    REQUIRE(icon != nullptr);
    CHECK(*icon == "child.png");

    // And a value only the grandparent has still reaches the marker.
    CHECK(model.effectiveFloat(pathable, "iconsize") == 2.0f);

    // The marker's own value beats both.
    model.pathables[marker].attributes.set("iconFile", "own.png");
    icon = model.effectiveValue(model.pathables[marker], "iconfile");
    REQUIRE(icon != nullptr);
    CHECK(*icon == "own.png");

    // Nothing anywhere falls back to the table's documented default.
    CHECK(model.effectiveFloat(pathable, "heightoffset") == 1.5f);
    CHECK(model.explicitValue(pathable, "iconsize") == nullptr);
}

TEST_CASE("deleting leaves a tombstone and restoring puts it back", "[model]") {
    // Every index into categories and pathables is positional — the selection, the tree,
    // the renderer cache — so a delete must not move anything.
    pack::Pack model = pack::createPack("markers.xml", "root", "Root");
    const size_t child = model.addCategory(0, "child", 0);
    REQUIRE(child != pack::kNoIndex);

    pack::AttributeCollection attributes;
    attributes.set("type", model.categories[child].id);
    attributes.set("xpos", "5");
    const size_t marker = model.addPathable(pack::PathableType::Poi, child, pack::kNoIndex, 0, std::move(attributes));
    REQUIRE(marker != pack::kNoIndex);

    const size_t categories_before = model.categories.size();
    const size_t pathables_before = model.pathables.size();

    model.removePathable(marker);
    CHECK(model.pathables.size() == pathables_before); // the slot stays
    CHECK(model.pathables[marker].grave.deleted);
    CHECK(model.countByType(pack::PathableType::Poi) == 0);

    model.restorePathable(marker);
    CHECK_FALSE(model.pathables[marker].grave.deleted);
    CHECK(model.countByType(pack::PathableType::Poi) == 1);
    // The element came back whole, not reconstructed.
    const std::string* xpos = model.pathables[marker].attributes.value("xpos");
    REQUIRE(xpos != nullptr);
    CHECK(*xpos == "5");

    std::vector<size_t> removed;
    model.removeCategorySubtree(0, &removed);
    CHECK(model.categories.size() == categories_before);
    // The root takes its child with it: children are nested inside the parent's element.
    CHECK(removed.size() == 2);
    CHECK(model.categories[0].grave.deleted);
    CHECK(model.categories[child].grave.deleted);

    std::vector<size_t> restored;
    model.restoreCategorySubtree(0, &restored);
    CHECK(restored.size() == 2);
    CHECK_FALSE(model.categories[0].grave.deleted);
    CHECK_FALSE(model.categories[child].grave.deleted);
    // The descendant's declaration handle has to be found again inside the restored copy,
    // since the original died with its parent.
    REQUIRE_FALSE(model.categories[child].declarations.empty());
    CHECK(model.categories[child].declarations[0].node);
}

TEST_CASE("a category name that cannot be an id segment is refused", "[model]") {
    pack::Pack model = pack::createPack("markers.xml", "root", "Root");

    // A dot would split the id into two levels that do not exist.
    CHECK(model.addCategory(0, "has.dot", 0) == pack::kNoIndex);
    CHECK(model.addCategory(0, "", 0) == pack::kNoIndex);
    CHECK(model.addCategory(0, "   ", 0) == pack::kNoIndex);

    const size_t ok = model.addCategory(0, "fine", 0);
    REQUIRE(ok != pack::kNoIndex);
    CHECK(model.categories[ok].id == "root.fine");

    // Already taken at that level.
    CHECK(model.addCategory(0, "FINE", 0) == pack::kNoIndex);
}

TEST_CASE("adding a marker appends so no existing index moves", "[model]") {
    pack::Pack model = pack::createPack("markers.xml", "root", "Root");

    pack::AttributeCollection first;
    first.set("type", "root");
    const size_t a = model.addPathable(pack::PathableType::Poi, 0, pack::kNoIndex, 0, std::move(first));
    REQUIRE(a != pack::kNoIndex);

    pack::AttributeCollection second;
    second.set("type", "root");
    // Placed after `a` in the file, but still appended to the vector.
    const size_t b = model.addPathable(pack::PathableType::Poi, 0, a, 0, std::move(second));
    REQUIRE(b != pack::kNoIndex);

    CHECK(a == 0);
    CHECK(b == 1);
    CHECK(model.pathables.size() == 2);
}

TEST_CASE("pending assets cancel each other rather than stacking", "[model]") {
    pack::Pack model;

    model.removeAsset("data/x.png");
    CHECK(model.isAssetRemoved("data/x.png"));

    // Putting a file back is a replacement, not both at once.
    model.addAsset("Data/X.png", {1, 2, 3});
    CHECK_FALSE(model.isAssetRemoved("data/x.png"));
    REQUIRE(model.findAddedAsset("data/x.png") != nullptr);
    // The spelling a save writes is the one that was handed in.
    CHECK(model.findAddedAsset("data/x.png")->original_path == "Data/X.png");

    // Removing something that only ever existed as a pending add just drops it.
    model.removeAsset("data/x.png");
    CHECK(model.findAddedAsset("data/x.png") == nullptr);
    CHECK_FALSE(model.isAssetRemoved("data/x.png"));

    CHECK_FALSE(model.hasAssetChanges());
}
