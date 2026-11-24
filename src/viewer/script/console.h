#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_set>
#include <vector>

// What scripts tell the player, and what the viewer tells the player about them: the script
// console's messages, and the census of what scripts asked for that does not exist.
namespace viewer::script {

// Pathing's ScriptMessageLogLevel.
enum class Level : uint8_t { Info, Warn, Error, System };

struct Message {
    Level level = Level::Info;
    std::string source; // "<pack>/<file>", or "system"
    std::string text;
};

// The newest messages, oldest first. Bounded: a script printing every tick would otherwise
// grow it without end.
class Console
{
public:
    static constexpr size_t kCapacity = 2000;

    void push(Level level, std::string source, std::string text);
    void clear();

    const std::deque<Message>& messages() const { return messages_; }
    size_t errors() const { return errors_; } // every error pushed, including those since dropped

    // Every message as plain text, a line each with its traceback below it, for the player to
    // copy or save into a report: "[source] text", with "ERROR " or "WARN " in front of those,
    // since the colours that mark them on screen do not survive the copy.
    std::string text() const;

    // Moves on every push and clear, so a window knows to scroll.
    uint64_t revision() const { return revision_; }

private:
    std::deque<Message> messages_;
    size_t errors_ = 0;
    uint64_t revision_ = 0;
};

// One member a script indexed on a Pathing object that the API has no row for.
struct CensusEntry {
    std::string member; // "Type.Member"
    std::string source; // where it was first asked for, as Message::source
    int line = 0;
};

// Run over the corpus offline, this is the list of what is left to implement, measured from
// what packs call rather than guessed from Pathing's source — see docs/viewer.md#the-census.
// Each member at each place is recorded once.
class Census
{
public:
    // False when this member was already recorded at this place.
    bool record(std::string member, std::string source, int line);

    const std::vector<CensusEntry>& entries() const { return entries_; }

private:
    std::vector<CensusEntry> entries_;
    std::unordered_set<std::string> seen_;
};

} // namespace viewer::script
