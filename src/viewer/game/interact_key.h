#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>

// The game's Interact key, as Nexus keeps it in GameBinds.xml, heard in the window procedure,
// which runs on the game's window thread while the frame runs on its render thread.
namespace viewer {

class InteractKey
{
public:
    // Reads the binds from `file`, Nexus's GameBinds.xml. read() after that runs only on the
    // worker: the write time is its alone.
    void open(std::filesystem::path file);

    // Reads the binds again when the file changed: the player rebinding Interact in Nexus.
    void read();

    // Whether `key`, just pressed, is one of the binds, with exactly its modifiers held: Ctrl+F
    // is not F, and is likely some other action's. `key` is the message's WPARAM.
    bool matches(uintptr_t key) const;

    // The first bind's key as the keyboard writes it, for the prompt. With no key the prompt is
    // still clicked, so that is what it says.
    std::string name() const;

private:
    // Each of the two binds packed as its virtual key and, from bit 8, Shift, Ctrl and Alt; 0 for
    // none. F, the game's default, when Nexus's file does not name Interact at all. Bound to the
    // mouse or to keys not known here, it has no key: F then may well be some other action's, and
    // a marker interacting on it would be a surprise.
    std::atomic<uint32_t> binds_[2] = {'F', 0};
    std::filesystem::path file_;
    std::filesystem::file_time_type written_{};
};

} // namespace viewer
