#pragma once

#include <glm/vec3.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pack {

// How an attribute's text is meant to be interpreted. The value itself is always stored
// verbatim as written in the file; this only says how to read it and how the property
// panel should present it.
enum class ValueKind : uint8_t {
    String,
    Int,
    Float,
    Bool, // "1"/"0", also accepts true/false
    Color, // RRGGBB or AARRGGBB, optional leading '#'
    Path, // resource path into the archive
    Guid, // base64 of 16 raw bytes
    Vec3, // "x,y,z"
    Enum, // one of a fixed set of words
};

enum AppliesTo : uint8_t {
    kAppliesCategory = 1 << 0,
    kAppliesPoi = 1 << 1,
    kAppliesTrail = 1 << 2,
    kAppliesPathable = kAppliesPoi | kAppliesTrail,
    kAppliesAll = kAppliesCategory | kAppliesPoi | kAppliesTrail,
};

// One row of the attribute table. Adding an attribute to the format means adding exactly
// one of these — parsing, serialization, the property form and the "add attribute"
// picker all read from the same table.
struct AttributeDef {
    std::string_view key; // canonical lowercase lookup key
    std::string_view display; // spelling used when *we* write a new attribute

    ValueKind kind = ValueKind::String;

    // Which node kinds *consume* the attribute — the ones where Pathing reads it off the
    // node's own aggregated collection. Not the same question as where it may be written:
    // see settableOn.
    uint8_t applies_to = kAppliesAll;

    // Resolved through the category chain, so setting it on a category is how an author
    // gives every pathable beneath it a value. Also what makes the attribute settable on a
    // category in the first place — see settableOn.
    bool inherits = false;

    std::string_view default_value; // as it would appear in XML; empty means none
    std::string_view doc;

    // For an Enum holding one word, every word a reader acts on, as we write it; the property
    // panel offers them in a dropdown. Empty for free text and for the list-valued filters
    // (`festival`, `mount`...), which take several words at once.
    std::vector<std::string_view> choices;
};

const std::vector<AttributeDef>& attributeTable();

// The node kinds an attribute may be *written* on: `applies_to`, plus categories whenever it
// inherits, since a category is where an author sets `iconsize` for a subtree
// (docs/format.md). What anything filtering attributes by node kind wants.
uint8_t settableOn(const AttributeDef& def);

// Case-insensitive lookup. Returns nullptr for attributes we do not know about; those
// are still kept verbatim on the node so they survive a round trip.
const AttributeDef* findAttribute(std::string_view name);

// Lowercases a name to get its lookup key.
//
// Vendor prefixes ("bh-...") are deliberately NOT stripped: TmfLib folds them into the
// base attribute, but an editor has to keep `bh-color` and `color` distinct so both
// round-trip and both show separately in the property panel.
std::string attributeKey(std::string_view name);

// True when `name` is already what attributeKey would return, so a lookup can skip the copy,
// which effectiveValue pays per ancestor per marker. False when unsure.
bool isAttributeKey(std::string_view name);

// ── Value parsing ─────────────────────────────────────────────────────────────
// All are permissive in the same places the reference implementations are, and all
// leave *out untouched and return false when the text does not parse.

bool parseInt(std::string_view text, int* out);
bool parseFloat(std::string_view text, float* out);
bool parseBool(std::string_view text, bool* out);
bool parseColor(std::string_view text, uint32_t* out_argb);
bool parseVec3(std::string_view text, glm::vec3* out);
bool parseGuid(std::string_view text, std::array<uint8_t, 16>* out);

std::string guidToBase64(const std::array<uint8_t, 16>& guid);

// A fresh GUID for a marker the editor creates, already in the base64 form the format
// uses. 128 random bits, so a collision with anything else in the pack is not a case worth
// coding around.
std::string generateGuid();

// Value text for a number we are writing back. Only ever applied to a value the user
// actually changed — an untouched attribute keeps its original text verbatim, which is
// what makes a byte-exact round trip possible. See Attribute::value in model.h.
std::string formatFloat(float value);
std::string formatVec3(const glm::vec3& value);

// Typed reads with the table's documented default as the fallback.
int defaultInt(std::string_view key, int fallback = 0);
float defaultFloat(std::string_view key, float fallback = 0.0f);
bool defaultBool(std::string_view key, bool fallback = false);

} // namespace pack
