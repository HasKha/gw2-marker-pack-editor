#include "editor/ui/property_panel.h"

#include <IconsFontAwesome7.h>
#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "editor/pathable_edit.h"
#include "editor/undo.h"
#include "viewer/ui/fonts.h"
#include "viewer/ui/style.h"

namespace ui::property_panel {

namespace {

// What kind of node this is, as a mask to test the attribute table against. Whether a given
// attribute belongs here is pack::settableOn's question, not this one's — a category is
// where an author sets iconSize for a whole subtree, so the two are far from the same.
uint8_t appliesMaskFor(const editor::State& state, editor::NodeRef node) {
    if (node.kind == editor::NodeKind::Category) return pack::kAppliesCategory;
    if (node.kind != editor::NodeKind::Pathable || node.index >= state.pack.pathables.size()) return 0;
    const pack::Pathable& pathable = state.pack.pathables[node.index];
    return pathable.type == pack::PathableType::Trail ? pack::kAppliesTrail : pack::kAppliesPoi;
}

// For the warning on an attribute that is real but does nothing where it sits.
const char* kindName(const editor::State& state, editor::NodeRef node) {
    if (node.kind == editor::NodeKind::Category) return "a category";
    if (node.kind != editor::NodeKind::Pathable || node.index >= state.pack.pathables.size()) return "this node";
    return state.pack.pathables[node.index].type == pack::PathableType::Trail ? "a trail" : "a marker";
}

// The category the chain continues from: a pathable's owning category, or a category's
// parent. kNoIndex when there is nothing above this node to inherit from.
size_t inheritsFrom(const editor::State& state, editor::NodeRef node) {
    if (node.kind == editor::NodeKind::Pathable && node.index < state.pack.pathables.size()) {
        return state.pack.pathables[node.index].category;
    }
    if (node.kind == editor::NodeKind::Category && node.index < state.pack.categories.size()) {
        return state.pack.categories[node.index].parent;
    }
    return pack::kNoIndex;
}

// Not constexpr: imgui 1.80's ImVec4 constructor is a plain one.
const ImVec4 kWarningColor(1.00f, 0.70f, 0.30f, 1.00f);

// A trail point is a coordinate in a .trl, not an XML node: attributesOf gives nullptr for
// it, and the trail editor is where it is edited.
using editor::attributesOf;

// Selection indices are into the pack's vectors, so anything that shrinks them can leave
// a stale ref. Reloading clears the selection, but this runs inside the game's render
// loop and a bad index there is a crash, not a glitch.
bool isValid(const editor::State& state, editor::NodeRef node) {
    if (node.kind == editor::NodeKind::Category) return node.index < state.pack.categories.size();
    return node.kind == editor::NodeKind::Pathable && node.index < state.pack.pathables.size();
}

// Deleted this frame, by the button in the header. The entry is still there — tombstones
// keep their slot — but there is nothing left worth showing for it.
using editor::isDeleted;

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
            *out = pack::formatFloat(value);
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
            *out = pack::formatVec3(value);
            changed = true;
        }
        break;
    }
    default: {
        // Sized to the value: packs ship tip-descriptions over a kilobyte, which a fixed buffer
        // would truncate. The headroom is what can be typed before the next frame resizes it.
        constexpr size_t kHeadroom = 256;
        static std::vector<char> buffer;
        buffer.assign(current.begin(), current.end());
        buffer.resize(current.size() + kHeadroom, '\0');

        if (ImGui::InputText("##v", buffer.data(), buffer.size())) {
            *out = buffer.data();
            changed = true;
        }
        break;
    }
    }

    ImGui::PopID();
    return changed;
}

bool open_delete_popup = false;

// What the active field held when it was clicked into. One field is active at a time.
std::string edit_started_value;

// A category takes its whole subtree with it, which is more than a single click should do
// without saying so. Ctrl+Z takes it back — the delete is one undo step, subtree included.
void drawDeletePopup(editor::State& state, editor::NodeRef node) {
    constexpr const char* kTitle = "Delete Category?";

    if (open_delete_popup) {
        ImGui::OpenPopup(kTitle);
        open_delete_popup = false;
    }

    if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    const pathable_edit::Removal removal = pathable_edit::wouldRemove(state, node);
    if (node.kind == editor::NodeKind::Category && node.index < state.pack.categories.size()) {
        ImGui::Text("%s", state.pack.categories[node.index].id.c_str());
    }
    ImGui::TextDisabled("Removes %zu categories and %zu markers.", removal.categories, removal.pathables);
    ImGui::Separator();

    const std::string remove = viewer::ui::fonts::label(ICON_FA_TRASH, "Delete");
    const float width = viewer::ui::style::buttonWidth({remove.c_str(), "Cancel"});
    if (ImGui::Button(remove.c_str(), ImVec2(width, 0))) {
        pathable_edit::remove(state, node);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(width, 0))) ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

// Right-aligned on the header's first row. Deleting a marker goes through immediately;
// deleting a category takes its whole subtree with it, which is worth asking about first.
void drawDeleteButton(editor::State& state, editor::NodeRef node, bool* out_confirm) {
    const std::string label = viewer::ui::fonts::label(ICON_FA_TRASH, "Delete");
    ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - viewer::ui::style::buttonWidth({label.c_str()}));
    if (!ImGui::Button(label.c_str())) return;

    if (node.kind == editor::NodeKind::Category) {
        *out_confirm = true;
        return;
    }
    pathable_edit::remove(state, node);
}

void drawHeader(editor::State& state, editor::NodeRef node, bool* out_confirm) {
    if (node.kind == editor::NodeKind::Category) {
        const pack::Category& category = state.pack.categories[node.index];
        ImGui::Text("Category  %s", category.displayName().c_str());
        drawDeleteButton(state, node, out_confirm);
        ImGui::TextDisabled("%s", category.id.c_str());

        std::set<size_t> files;
        for (const pack::SourceNode& declaration : category.declarations) files.insert(declaration.file);
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
    drawDeleteButton(state, node, out_confirm);
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
    const uint8_t mask = appliesMaskFor(state, node);

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
            // Two things get flagged, and neither is ever dropped. An attribute the table
            // does not know at all — usually a typo. And one it knows that does nothing
            // where it sits, like an iconSize on a trail: real, spelled right, dead.
            const bool misplaced = def && (pack::settableOn(*def) & mask) == 0;
            if (def && !misplaced) {
                ImGui::TextUnformatted(attribute.name.c_str());
                if (ImGui::IsItemHovered() && !def->doc.empty()) {
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(std::string(def->doc).c_str());
                    ImGui::EndTooltip();
                }
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, kWarningColor);
                ImGui::TextUnformatted(attribute.name.c_str());
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) {
                    if (misplaced) {
                        ImGui::SetTooltip("Does nothing on %s. Kept as-is.", kindName(state, node));
                    } else {
                        ImGui::SetTooltip("Not a known attribute. Kept as-is, but check the spelling.");
                    }
                }
            }

            ImGui::TableNextColumn();
            std::string updated;
            const pack::ValueKind kind = def ? def->kind : pack::ValueKind::String;
            const std::string merge_key =
                "attr:" + std::to_string(static_cast<int>(node.kind)) + ":" + std::to_string(node.index) + ":" + attribute.key;

            // Keyed on the field: dragging one number is one step, but moving to the next
            // attribute starts another.
            if (editValue(attribute.key.c_str(), kind, attribute.value, &updated)) {
                undo::Step step(state, "Edit " + attribute.name, merge_key);
                undo::attributes(state, node);
                attributes->set(attribute.name, updated);
                state.touch();
            }

            // `rows` is this frame's copy, so on the frame the field is clicked into it
            // still holds the value being edited away from.
            if (ImGui::IsItemActivated()) edit_started_value = attribute.value;

            // On commit, not per keystroke: attributeRenamed moves a file, and typing
            // "route-one.trl" is eighteen intermediate paths. Escape reverts the field
            // first, so an abandoned edit arrives here as previous == current and no-ops.
            if (ImGui::IsItemDeactivatedAfterEdit() && !edit_started_value.empty()) {
                const pack::Attribute* settled = attributes->find(attribute.key);
                const std::string settled_value = settled ? settled->value : std::string();

                undo::Step step(state, "Edit " + attribute.name, merge_key);

                // A pause while typing ends the step, so the move could land in one without the
                // value. Restoring the old value for an instant gives it the right before-image;
                // undo::attributes ignores it when the steps did merge.
                attributes->set(attribute.name, edit_started_value);
                undo::attributes(state, node);
                attributes->set(attribute.name, settled_value);

                pathable_edit::attributeRenamed(state, node, attribute.key, edit_started_value);
                edit_started_value.clear();
            }

            ImGui::TableNextColumn();
            ImGui::PushID(attribute.key.c_str());
            // The icon alone, or an x before the font is built.
            if (ImGui::SmallButton(viewer::ui::fonts::ready() ? ICON_FA_XMARK : "x")) remove_key = attribute.key;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove this attribute");
            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    if (!remove_key.empty()) {
        undo::Step step(state, "Remove " + remove_key);
        undo::attributes(state, node);
        attributes->erase(remove_key);
        state.touch();
    }
}

// Values reaching this node from an ancestor category, categories included, with their source and
// a button to copy the value down to override it. Draws its own heading, which depends on the walk.
void drawInherited(editor::State& state, editor::NodeRef node) {
    const pack::AttributeCollection* own = attributesOf(state, node);
    if (!own) return;

    // kNoIndex for a root category, or a marker whose type resolves to nothing. The walk
    // below then finds nothing, which is the answer: no ancestors, nothing inherited.
    const size_t from = inheritsFrom(state, node);

    struct Row {
        std::string key;
        std::string name;
        std::string value;
        std::string from;
    };
    std::vector<Row> rows;

    // Only what this node could carry itself. A marker inheriting a category's `texture`
    // would be listing an attribute it has no use for and cannot usefully override.
    const uint8_t mask = appliesMaskFor(state, node);

    for (const pack::AttributeDef& def : pack::attributeTable()) {
        if (!def.inherits) continue;
        if ((pack::settableOn(def) & mask) == 0) continue;
        if (own->find(def.key)) continue; // set here, not inherited

        // Walk up to find which category actually supplies it.
        for (size_t at = from; at != pack::kNoIndex;) {
            const pack::Category& category = state.pack.categories[at];
            if (const pack::Attribute* found = category.attributes.find(def.key)) {
                rows.push_back(Row{std::string(def.key), found->name, found->value, category.displayName()});
                break;
            }
            at = category.parent;
        }
    }

    ImGui::Separator();

    if (rows.empty()) {
        ImGui::TextDisabled("Nothing inherited.");
        return;
    }

    ImGui::TextDisabled("Inherited");

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
            if (ImGui::SmallButton("Override")) {
                if (pack::AttributeCollection* target = attributesOf(state, node)) {
                    undo::Step step(state, "Override " + row.name);
                    undo::attributes(state, node);
                    target->set(row.name, row.value);
                    state.touch();
                }
            }
            ImGui::PopID();
        }

        ImGui::EndTable();
    }
}

// Everything from the table that can be set here and is not set yet.
void drawAddAttribute(editor::State& state, const std::vector<editor::NodeRef>& targets) {
    if (targets.empty()) return;

    // settableOn, not applies_to: a category consumes almost none of these itself but is the
    // place an author sets them for everything beneath it.
    //
    // The union across the selection decides what gets *listed*; per node it is asked again
    // before anything is written, so a mixed marker-and-trail selection offers each one's
    // attributes without stamping an iconSize onto the trail.
    auto settableOn = [&state](const pack::AttributeDef& def, editor::NodeRef node) {
        return (pack::settableOn(def) & appliesMaskFor(state, node)) != 0;
    };

    uint8_t mask = 0;
    for (editor::NodeRef node : targets) mask |= appliesMaskFor(state, node);

    if (!ImGui::BeginCombo("##add", "+ Add attribute")) return;

    static char search[64] = {};
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##search", "Search...", search, sizeof(search));

    std::string needle;
    for (const char* c = search; *c; ++c) needle.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*c))));

    for (const pack::AttributeDef& def : pack::attributeTable()) {
        if ((pack::settableOn(def) & mask) == 0) continue;
        if (!needle.empty() && std::string(def.key).find(needle) == std::string::npos) continue;

        // Hide it once every node that could take it already has it. Nodes it does not
        // belong on do not count, or an attribute only the trail in the selection can carry
        // would stay listed after the trail already had it.
        bool all_have = true;
        for (editor::NodeRef node : targets) {
            const pack::AttributeCollection* attributes = attributesOf(state, node);
            if (!attributes || !settableOn(def, node)) continue;
            if (!attributes->find(def.key)) {
                all_have = false;
                break;
            }
        }
        if (all_have) continue;

        if (ImGui::Selectable(std::string(def.display).c_str())) {
            undo::Step step(state, "Add " + std::string(def.display));
            for (editor::NodeRef node : targets) {
                pack::AttributeCollection* attributes = attributesOf(state, node);
                if (!attributes || attributes->find(def.key)) continue;
                if (!settableOn(def, node)) continue;
                undo::attributes(state, node);
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
                undo::Step step(state, "Edit " + key + " on " + std::to_string(nodes.size()) + " items", "bulk:" + key);
                for (editor::NodeRef node : nodes) {
                    pack::AttributeCollection* attributes = attributesOf(state, node);
                    if (!attributes) continue;
                    undo::attributes(state, node);
                    // No attributeRenamed: several nodes onto one path is a re-point, and
                    // there is no single file to move.
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

// Drawn under an id scope that follows the selection — see draw().
void drawSelection(editor::State& state) {
    if (state.selection.size() > 1) {
        drawMultiSelection(state);
        return;
    }

    const editor::NodeRef node = state.selection.primary();
    if (!isValid(state, node)) {
        ImGui::TextDisabled("Selection is stale.");
        return;
    }

    bool confirm_delete = false;
    drawHeader(state, node, &confirm_delete);
    if (confirm_delete) open_delete_popup = true;
    drawDeletePopup(state, node);

    // The header's Delete button just fired. Its attributes are still in memory, but
    // offering to edit them would be offering to edit something that no longer exists.
    if (isDeleted(state, node)) return;

    ImGui::Separator();

    drawExplicit(state, node);

    ImGui::Separator();
    drawAddAttribute(state, state.selection.items());

    drawInherited(state, node);
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

    // Fields below are identified by attribute name alone, which is the same name on every
    // node, so to imgui one node's `xpos` box and the next node's are one widget — and an
    // edit still active when the selection changes commits onto whatever is now selected.
    // Scoping by the selection changes every id with it, which drops the edit instead.
    const editor::NodeRef primary = state.selection.primary();
    ImGui::PushID(static_cast<int>(primary.kind));
    ImGui::PushID(static_cast<int>(primary.index));

    drawSelection(state);

    ImGui::PopID();
    ImGui::PopID();
}

} // namespace ui::property_panel
