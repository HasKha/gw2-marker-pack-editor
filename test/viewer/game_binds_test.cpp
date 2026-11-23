// The game's key binds as Nexus keeps them, read from a file laid out as Nexus writes it.

#include <catch2/catch_test_macros.hpp>

#include "corpus.h"
#include "viewer/game_binds.h"

using viewer::game_binds::Key;

// The action names end in `))"`, so the raw strings need a delimiter of their own.
TEST_CASE("game binds: Interact's keys, both of them, with their modifiers", "[game_binds]") {
    const corpus::TempDir temp("game_binds");
    corpus::writeFile(temp / "GameBinds.xml", R"xml(<?xml version="1.0"?>
<InputBindings>
	<action name="((MoveForward))" id="0" device="Keyboard" button="87" device2="Keyboard" button2="31" />
	<action name="((MiscInteract))" id="65" device="Keyboard" button="70" device2="Keyboard" button2="71" mod2="3" />
</InputBindings>
)xml");

    const std::vector<Key> keys = viewer::game_binds::read(temp / "GameBinds.xml", viewer::game_binds::ACTION_INTERACT);
    REQUIRE(keys.size() == 2);
    CHECK(keys[0] == Key{'F'});
    CHECK(keys[1] == Key{'G', true, true, false});
}

TEST_CASE("game binds: a mouse bind, an unknown key or no file name no key", "[game_binds]") {
    const corpus::TempDir temp("game_binds_none");
    corpus::writeFile(temp / "GameBinds.xml", R"xml(<InputBindings>
	<action id="65" device="Mouse" button="3" device2="Keyboard" button2="21" />
</InputBindings>
)xml");
    CHECK(viewer::game_binds::read(temp / "GameBinds.xml", viewer::game_binds::ACTION_INTERACT).empty());
    CHECK(viewer::game_binds::read(temp / "missing.xml", viewer::game_binds::ACTION_INTERACT).empty());
}

TEST_CASE("game binds: the game's codes for letters and digits are Windows' keys", "[game_binds]") {
    CHECK(viewer::game_binds::virtualKey(70) == 'F');
    CHECK(viewer::game_binds::virtualKey(48) == '0');
    // Nothing else is known to be any key in particular.
    CHECK(viewer::game_binds::virtualKey(21) == 0);
    CHECK(viewer::game_binds::virtualKey(97) == 0);
}
