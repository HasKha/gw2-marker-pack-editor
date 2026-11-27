// Which characters our windows' font is built with beyond Latin: reading them out of text, and
// out of a pack as the registry loads it.

#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "corpus.h"
#include "viewer/packs/registry.h"
#include "viewer/ui/glyphs.h"

using viewer::ui::glyphs::Block;
using viewer::ui::glyphs::Set;

TEST_CASE("only characters beyond Latin are wanted, each once", "[glyphs]") {
    Set set;
    CHECK_FALSE(set.add("Gorseval, Sabetha \xC3\xA9t\xC3\xA9 \xC5\x81\xC3\xB3" "d\xC5\xBA")); // été Łódź
    CHECK(set.empty());

    CHECK(set.add("\xE9\xBE\x99 \xED\x95\x9C \xD0\x96")); // 龙 한 Ж
    CHECK(set.size() == 3);
    CHECK(set.contains(char32_t{0x9F99}));
    CHECK(set.contains(char32_t{0xD55C}));
    CHECK(set.contains(char32_t{0x0416}));
    CHECK_FALSE(set.add("\xE9\xBE\x99"));
    CHECK(set.size() == 3);
}

TEST_CASE("what is not drawn as a glyph is never wanted", "[glyphs]") {
    Set set;
    // A Font Awesome icon (private use), an emoji beyond the plane, a C1 control, a noncharacter.
    CHECK_FALSE(set.add("\xEF\x80\x8D \xF0\x9F\x98\x80 \xC2\x85 \xEF\xBF\xBF"));
    // Bytes that are not UTF-8: a stray continuation, an overlong slash, a truncated sequence.
    CHECK_FALSE(set.add("\x80 \xC0\xAF \xE9\xBE"));
    CHECK(set.empty());
    // A bad byte costs only itself: the character after it still reads.
    CHECK(set.add("\xFF\xE9\xBE\x99"));
    CHECK(set.contains(char32_t{0x9F99}));
}

TEST_CASE("the wanted characters are handed to ImGui as runs, within the blocks asked for", "[glyphs]") {
    Set set;
    CHECK(set.ranges() == std::vector<uint16_t>{0});

    set.add("\xE4\xB8\x80\xE4\xB8\x81\xE4\xB8\x83 \xD0\x96"); // 一丁七, Ж
    CHECK(set.ranges() == std::vector<uint16_t>{0x0416, 0x0416, 0x4E00, 0x4E01, 0x4E03, 0x4E03, 0});

    const Block cjk[] = {{0x4E00, 0x9FFF}};
    CHECK(set.ranges(cjk) == std::vector<uint16_t>{0x4E00, 0x4E01, 0x4E03, 0x4E03, 0});
    const Block hangul[] = {{0xAC00, 0xD7AF}};
    CHECK(set.ranges(hangul) == std::vector<uint16_t>{0});

    Set more;
    more.add("\xE4\xB8\x82\xE4\xB8\x80"); // 丂, already-wanted 一
    CHECK(set.add(more));
    CHECK_FALSE(set.add(more));
    CHECK(set.ranges(cjk) == std::vector<uint16_t>{0x4E00, 0x4E03, 0});
}

TEST_CASE("a loaded pack carries the characters of its categories, tooltips and scripts", "[glyphs]") {
    const corpus::TempDir temp("glyphs_pack");
    // 龙 as a display name, 한 as a tooltip, Ж in a script.
    corpus::writeFile(
        temp / "pack" / "markers.xml",
        "<OverlayData>"
        "<MarkerCategory name=\"dragons\" DisplayName=\"\xE9\xBE\x99\"/>"
        "<POIs><POI type=\"dragons\" MapID=\"15\" xpos=\"0\" ypos=\"0\" zpos=\"0\" tip-description=\"\xED\x95\x9C\"/></POIs>"
        "</OverlayData>");
    corpus::writeFile(temp / "pack" / "pack.lua", "Debug:Print(\"\xD0\x96\")");

    const viewer::registry::LoadedPack loaded = viewer::registry::loadPack(temp / "pack");
    REQUIRE(loaded.error.empty());
    CHECK(loaded.glyphs.size() == 3);
    CHECK(loaded.glyphs.contains(char32_t{0x9F99}));
    CHECK(loaded.glyphs.contains(char32_t{0xD55C}));
    CHECK(loaded.glyphs.contains(char32_t{0x0416}));
}
