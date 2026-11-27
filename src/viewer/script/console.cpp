#include "viewer/script/console.h"

namespace viewer::script {

void Console::push(Level level, std::string source, std::string text) {
    if (level == Level::Error) ++error_count;
    if (kept_messages.size() == kCapacity) kept_messages.pop_front();
    kept_messages.push_back({level, std::move(source), std::move(text)});
    ++current_revision;
}

void Console::clear() {
    kept_messages.clear();
    ++current_revision;
}

std::string Console::text() const {
    std::string out;
    for (const Message& message : kept_messages) {
        if (message.level == Level::Error) out += "ERROR ";
        if (message.level == Level::Warn) out += "WARN ";
        out += '[' + message.source + "] " + message.text + '\n';
    }
    return out;
}

bool Census::record(std::string member, std::string source, int line) {
    std::string key = member + '@' + source + ':' + std::to_string(line);
    if (!seen.insert(std::move(key)).second) return false;
    missing_members.push_back({std::move(member), std::move(source), line});
    return true;
}

} // namespace viewer::script
