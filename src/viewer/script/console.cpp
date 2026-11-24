#include "viewer/script/console.h"

namespace viewer::script {

void Console::push(Level level, std::string source, std::string text) {
    if (level == Level::Error) ++errors_;
    if (messages_.size() == kCapacity) messages_.pop_front();
    messages_.push_back({level, std::move(source), std::move(text)});
    ++revision_;
}

void Console::clear() {
    messages_.clear();
    ++revision_;
}

std::string Console::text() const {
    std::string out;
    for (const Message& message : messages_) {
        if (message.level == Level::Error) out += "ERROR ";
        if (message.level == Level::Warn) out += "WARN ";
        out += '[' + message.source + "] " + message.text + '\n';
    }
    return out;
}

bool Census::record(std::string member, std::string source, int line) {
    std::string key = member + '@' + source + ':' + std::to_string(line);
    if (!seen_.insert(std::move(key)).second) return false;
    entries_.push_back({std::move(member), std::move(source), line});
    return true;
}

} // namespace viewer::script
