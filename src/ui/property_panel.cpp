#include "ui/property_panel.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace ui::property_panel {

namespace {

// Formats a float back to text without the trailing-zero noise printf("%f") produces.
// Only ever applied to a value the user actually changed, so it cannot dirty untouched
// attributes — see the note on Attribute::value in pack/model.h.
std::string formatFloat(float value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.6g", static_cast<double>(value));
    return buffer;
}

std::string formatVec3(const glm::vec3& value) {
    return formatFloat(value.x) + "," + formatFloat(value.y) + "," + formatFloat(value.z);
}

// Which node kinds an attribute may be set on, so the "add attribute" list only offers
// things that make sense for what is selected.
uint8_t appliesMaskFor(const editor::State& state, editor::NodeRef node) {
    if (node.kind == editor::NodeKind::Category) return pack::kAppliesCategory;
    const pack::Pathable& pathable = state.pack.pathables[node.index];
    return pathable.type == pack::PathableType::Trail ? pack::kAppliesTrail : pack::kAppliesPoi;
}

pack::AttributeCollection* attributesOf(editor::State& state, editor::NodeRef node) {
    if (node.kind == editor::NodeKind::Category) {
        if (node.index >= state.pack.categories.size()) return nullptr;
        return &state.pack.categories[node.index].attributes;
    }
    if (node.index >= state.pack.pathables.size()) return nullptr;
    return &state.pack.pathables[node.index].attributes;
}

// Selection indices are into the pack's vectors, so anything that shrinks them can leave
// a stale ref. Reloading clears the selection, but this runs inside the game's render
// loop and a bad index there is a crash, not a glitch.
bool isValid(const editor::State& state, editor::NodeRef node) {
    return node.kind == editor::NodeKind::Category ? node.index < state.pack.categories.size() : node.index < state.pack.pathables.size();
}

// Draws the editor for one value. Returns the new text when the user changed it.
// The widget is picked purely from the table's ValueKind.
bool editValue(const char* id, pack::ValueKind kind, const std::string& current, std::string* out) {
    ImGui::PushID(id);
    ImGui::SetNextItemWidth(-1.0f);

    bool changed = false;

    switch (kind) {
    case pack::ValueKind::Float: {
        float value = 0.0f;
        pack::parseFloat(current, &value);
        if (ImGui::DragFloat("##v", &value, 0.05f)) {
            *out = formatFloat(value);
            changed = true;
        }
        break;
    }
    case pack::ValueKind::Int: {
        int value = 0;
        pack::parseInt(current, &value);
        if (ImGui::DragInt("##v", &value)) {
            *out = std::to_string(value);
            changed = true;
        }
        break;
    }
    case pack::ValueKind::Bool: {
        bool value = false;
        pack::parseBool(current, &value);
        if (ImGui::Checkbox("##v", &value)) {
            *out = value ? "1" : "0";
            changed = true;
        }
        break;
    }
    case pack::ValueKind::Color: {
        uint32_t argb = 0xFFFFFFFFu;
        pack::parseColor(current, &argb);
        float rgba[4] = {
            ((argb >> 16) & 0xFF) / 255.0f,
            ((argb >> 8) & 0xFF) / 255.0f,
            (argb & 0xFF) / 255.0f,
            ((argb >> 24) & 0xFF) / 255.0f};
        if (ImGui::ColorEdit4("##v", rgba, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar)) {
            auto byte = [](float f) { return static_cast<unsigned>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f); };
            char buffer[16];
            // Keep the 6-digit form when the colour is opaque; that is what packs use.
            if (byte(rgba[3]) == 255) {
                std::snprintf(buffer, sizeof(buffer), "%02X%02X%02X", byte(rgba[0]), byte(rgba[1]), byte(rgba[2]));
            } else {
                std::snprintf(buffer, sizeof(buffer), "%02X%02X%02X%02X", byte(rgba[3]), byte(rgba[0]), byte(rgba[1]), byte(rgba[2]));
            }
            *out = buffer;
            changed = true;
        }
        break;
    }
    case pack::ValueKind::Vec3: {
        glm::vec3 value(0.0f);
        pack::parseVec3(current, &value);
        if (ImGui::DragFloat3("##v", &value.x, 0.5f)) {
            *out = formatVec3(value);
            changed = true;
        }
        break;
    }
    default: {
        char buffer[512];
        std::snprintf(buffer, sizeof(buffer), "%s", current.c_str());
        if (ImGui::InputText("##v", buffer, sizeof(buffer))) {
            *out = buffer;
            changed = true;
        }
        break;
    }
    }

    ImGui::PopID();
    return changed;
}

void drawHeader(const editor::State& state, editor::NodeRef node) {
    if (node.kind == editor::NodeKind::Category) {
        const pack::Category& category = state.pack.categories[node.index];
        ImGui::Text("Category  %s", category.displayName().c_str());
        ImGui::TextDisabled("%s", category.id.c_str());

        std::set<size_t> files;
        for (const pack::SourceSpan& span : category.declarations) files.insert(span.file);
        std::string where;
        for (size_t file : files) {
            if (file >= state.pack.files.size()) continue;
            if (!where.empty()) where += ", ";
            where += state.pack.files[file].path;
        }
        ImGui::TextDisabled("declared in %s", where.c_str());
        return;
    }

    const pack::Pathable& pathable = state.pack.pathables[node.index];
    ImGui::Text("%s", pack::toString(pathable.type));
    ImGui::TextDisabled("%s", pathable.category_id.empty() ? "(no type attribute)" : pathable.category_id.c_str());
    if (pathable.source.file < state.pack.files.size()) {
        ImGui::TextDisabled("in %s", state.pack.files[pathable.source.file].path.c_str());
    }
}

// Attributes set on the node itself. Editable, removable.
void drawExplicit(editor::State& state, editor::NodeRef node) {
    pack::AttributeCollection* attributes = attributesOf(state, node);
    if (!attributes) return;

    if (attributes->empty()) {
        ImGui::TextDisabled("No attributes set here.");
        return;
    }

    std::string remove_key;

    if (ImGui::BeginTable("##explicit", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("x", ImGuiTableColumnFlags_WidthFixed, 22.0f);

        // Copy the names first: the loop mutates the collection.
        std::vector<pack::Attribute> rows = attributes->all();

        for (const pack::Attribute& attribute : rows) {
            const pack::AttributeDef* def = pack::findAttribute(attribute.key);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            if (def) {
                ImGui::TextUnformatted(attribute.name.c_str());
                if (ImGui::IsItemHovered() && !def->doc.empty()) {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(std::string(def->doc).c_str());
                    ImGui::EndTooltip();
                }
            } else {
                // Unknown to the table: still editable, still round-trips, but flagged
                // because it is usually a typo.
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.70f, 0.30f, 1.00f));
                ImGui::TextUnformatted(attribute.name.c_str());
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Not a known attribute. Kept as-is, but check the spelling.");
            }

            ImGui::TableNextColumn();
            std::string updated;
            const pack::ValueKind kind = def ? def->kind : pack::ValueKind::String;
            if (editValue(attribute.key.c_str(), kind, attribute.value, &updated)) {
                attributes->set(attribute.name, updated);
                state.touch();
            }

            ImGui::TableNextColumn();
            ImGui::PushID(attribute.key.c_str());
            if (ImGui::SmallButton("x")) remove_key = attribute.key;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove this attribute");
            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    if (!remove_key.empty()) {
        attributes->erase(remove_key);
        state.touch();
    }
}

// Values reaching this node from an ancestor category. Shown greyed with the source, and
// a button to copy the value down so it can be overridden here.
void drawInherited(editor::State& state, editor::NodeRef node) {
    if (node.kind != editor::NodeKind::Pathable) return;

    const pack::Pathable& pathable = state.pack.pathables[node.index];
    if (pathable.category == pack::kNoIndex) return;

    struct Row {
        std::string key;
        std::string name;
        std::string value;
        std::string from;
    };
    std::vector<Row> rows;

    for (const pack::AttributeDef& def : pack::attributeTable()) {
        if (!def.inherits) continue;
        if (pathable.attributes.find(def.key)) continue; // set here, not inherited

        // Walk up to find which category actually supplies it.
        for (size_t at = pathable.category; at != pack::kNoIndex;) {
            const pack::Category& category = state.pack.categories[at];
            if (const pack::Attribute* found = category.attributes.find(def.key)) {
                rows.push_back(Row{std::string(def.key), found->name, found->value, category.displayName()});
                break;
            }
            at = category.parent;
        }
    }

    if (rows.empty()) {
        ImGui::TextDisabled("Nothing inherited.");
        return;
    }

    if (ImGui::BeginTable("##inherited", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("override", ImGuiTableColumnFlags_WidthFixed, 70.0f);

        for (const Row& row : rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", row.name.c_str());

            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%s", row.value.c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Inherited from %s", row.from.c_str());

            ImGui::TableNextColumn();
            ImGui::PushID(row.key.c_str());
            if (ImGui::SmallButton("override")) {
                state.pack.pathables[node.index].attributes.set(row.name, row.value);
                state.touch();
            }
            ImGui::PopID();
        }

        ImGui::EndTable();
    }
}

// Everything from the table that applies here and is not set yet.
void drawAddAttribute(editor::State& state, const std::vector<editor::NodeRef>& targets) {
    if (targets.empty()) return;

    uint8_t mask = 0;
    for (editor::NodeRef node : targets) mask |= appliesMaskFor(state, node);

    if (!ImGui::BeginCombo("##add", "+ Add attribute")) return;

    static char search[64] = {};
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##search", "Search...", search, sizeof(search));

    std::string needle;
    for (const char* c = search; *c; ++c) needle.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*c))));

    for (const pack::AttributeDef& def : pack::attributeTable()) {
        if ((def.applies_to & mask) == 0) continue;
        if (!needle.empty() && std::string(def.key).find(needle) == std::string::npos) continue;

        // Hide it once every selected node already has it.
        bool all_have = true;
        for (editor::NodeRef node : targets) {
            const pack::AttributeCollection* attributes = attributesOf(state, node);
            if (attributes && !attributes->find(def.key)) {
                all_have = false;
                break;
            }
        }
        if (all_have) continue;

        if (ImGui::Selectable(std::string(def.display).c_str())) {
            for (editor::NodeRef node : targets) {
                pack::AttributeCollection* attributes = attributesOf(state, node);
                if (!attributes || attributes->find(def.key)) continue;
                attributes->set(def.display, std::string(def.default_value));
            }
            state.touch();
        }
        if (ImGui::IsItemHovered() && !def.doc.empty()) ImGui::SetTooltip("%s", std::string(def.doc).c_str());
    }

    ImGui::EndCombo();
}

// Bulk edit: attributes present on every selected node. A differing value is shown as
// such and only overwritten if the user actually edits the field.
void drawMultiSelection(editor::State& state) {
    const std::vector<editor::NodeRef>& nodes = state.selection.items();
    ImGui::Text("%zu items selected", nodes.size());
    ImGui::Separator();

    const pack::AttributeCollection* anchor = attributesOf(state, nodes.front());
    if (!anchor) {
        ImGui::TextDisabled("Selection is stale.");
        return;
    }

    std::vector<std::string> shared;
    for (const pack::Attribute& attribute : anchor->all()) shared.push_back(attribute.key);

    for (size_t i = 1; i < nodes.size(); ++i) {
        const pack::AttributeCollection* attributes = attributesOf(state, nodes[i]);
        shared.erase(
            std::remove_if(shared.begin(), shared.end(), [&](const std::string& key) { return !attributes || !attributes->find(key); }),
            shared.end());
    }

    if (shared.empty()) {
        ImGui::TextDisabled("No attributes common to the whole selection.");
    } else if (ImGui::BeginTable("##bulk", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

        for (const std::string& key : shared) {
            const pack::AttributeDef* def = pack::findAttribute(key);

            const pack::Attribute* first = anchor->find(key);
            if (!first) continue;

            bool same = true;
            for (size_t i = 1; i < nodes.size() && same; ++i) {
                const pack::AttributeCollection* attributes = attributesOf(state, nodes[i]);
                const pack::Attribute* other = attributes ? attributes->find(key) : nullptr;
                same = other && other->value == first->value;
            }

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(first->name.c_str());

            ImGui::TableNextColumn();
            if (!same) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("<multiple values>");
                ImGui::SameLine();
            }
            std::string updated;
            const pack::ValueKind kind = def ? def->kind : pack::ValueKind::String;
            if (editValue(key.c_str(), kind, same ? first->value : std::string(), &updated)) {
                for (editor::NodeRef node : nodes) {
                    pack::AttributeCollection* attributes = attributesOf(state, node);
                    if (!attributes) continue;
                    const pack::Attribute* existing = attributes->find(key);
                    attributes->set(existing ? existing->name : key, updated);
                }
                state.touch();
            }
        }

        ImGui::EndTable();
    }

    ImGui::Separator();
    drawAddAttribute(state, nodes);
}

} // namespace

void draw(editor::State& state) {
    if (!state.loaded) {
        ImGui::TextDisabled("No pack open.");
        return;
    }

    if (state.selection.empty()) {
        ImGui::TextDisabled("Select something in the tree.");
        return;
    }

    if (state.selection.size() > 1) {
        drawMultiSelection(state);
        return;
    }

    const editor::NodeRef node = state.selection.primary();
    if (!isValid(state, node)) {
        ImGui::TextDisabled("Selection is stale.");
        return;
    }

    drawHeader(state, node);
    ImGui::Separator();

    drawExplicit(state, node);

    ImGui::Separator();
    drawAddAttribute(state, state.selection.items());

    if (node.kind == editor::NodeKind::Pathable) {
        ImGui::Separator();
        ImGui::TextDisabled("Inherited");
        drawInherited(state, node);
    }
}

} // namespace ui::property_panel
