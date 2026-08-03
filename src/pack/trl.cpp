#include "pack/trl.h"

#include <cstring>

namespace pack::trl {

namespace {

constexpr size_t kHeaderSize = 8; // int32 version + int32 map id
constexpr size_t kPointSize = 12; // three float32

template <typename T>
T readLittleEndian(const uint8_t* at) {
    // Every platform we target is little-endian, but going through memcpy keeps this
    // free of alignment and strict-aliasing assumptions.
    T value;
    std::memcpy(&value, at, sizeof(T));
    return value;
}

void appendLittleEndian(std::vector<uint8_t>* out, int32_t value) {
    uint8_t bytes[sizeof(value)];
    std::memcpy(bytes, &value, sizeof(value));
    out->insert(out->end(), bytes, bytes + sizeof(bytes));
}

void appendLittleEndian(std::vector<uint8_t>* out, float value) {
    uint8_t bytes[sizeof(value)];
    std::memcpy(bytes, &value, sizeof(value));
    out->insert(out->end(), bytes, bytes + sizeof(bytes));
}

bool isTerminator(const glm::vec3& point) {
    return point.x == 0.0f && point.y == 0.0f && point.z == 0.0f;
}

} // namespace

size_t Trail::pointCount() const {
    size_t total = 0;
    for (const Segment& segment : segments) total += segment.points.size();
    return total;
}

bool read(const std::vector<uint8_t>& data, Trail* out, std::string* error) {
    *out = Trail{};

    if (data.size() < kHeaderSize) {
        // An empty or truncated file is not an error; TmfLib yields no segments.
        return true;
    }

    const int32_t version = readLittleEndian<int32_t>(data.data());
    if (version != 0) {
        if (error) *error = "Unsupported .trl version " + std::to_string(version) + " (only 0 is known)";
        return false;
    }

    out->map_id = readLittleEndian<int32_t>(data.data() + 4);

    Segment current;

    for (size_t at = kHeaderSize; at + kPointSize <= data.size(); at += kPointSize) {
        const glm::vec3 point(
            readLittleEndian<float>(data.data() + at),
            readLittleEndian<float>(data.data() + at + 4),
            readLittleEndian<float>(data.data() + at + 8));

        if (isTerminator(point)) {
            if (current.points.empty()) {
                // A terminator with no segment in front of it: either leading, or part of
                // a run following the segment we just closed.
                if (out->segments.empty()) {
                    ++out->leading_terminators;
                } else {
                    ++out->segments.back().terminators_after;
                }
            } else {
                current.terminators_after = 1;
                out->segments.push_back(std::move(current));
                current = Segment{};
            }
            continue;
        }

        current.points.push_back(point);
    }

    if (!current.points.empty()) out->segments.push_back(std::move(current));

    return true;
}

std::vector<uint8_t> write(const Trail& trail) {
    std::vector<uint8_t> out;
    out.reserve(kHeaderSize + (trail.pointCount() + trail.segments.size() + trail.leading_terminators) * kPointSize);

    appendLittleEndian(&out, static_cast<int32_t>(0)); // version
    appendLittleEndian(&out, trail.map_id);

    auto appendTerminators = [&](uint32_t count) {
        for (uint32_t i = 0; i < count; ++i) {
            appendLittleEndian(&out, 0.0f);
            appendLittleEndian(&out, 0.0f);
            appendLittleEndian(&out, 0.0f);
        }
    };

    appendTerminators(trail.leading_terminators);

    for (size_t i = 0; i < trail.segments.size(); ++i) {
        const Segment& segment = trail.segments[i];
        if (segment.points.empty()) continue;

        for (const glm::vec3& point : segment.points) {
            appendLittleEndian(&out, point.x);
            appendLittleEndian(&out, point.y);
            appendLittleEndian(&out, point.z);
        }

        // A segment inserted by an edit has no recorded terminator, but every segment
        // except the last still needs one to stay separate on reload.
        const uint32_t terminators = segment.terminators_after > 0 ? segment.terminators_after : (i + 1 < trail.segments.size() ? 1 : 0);
        appendTerminators(terminators);
    }

    return out;
}

} // namespace pack::trl
