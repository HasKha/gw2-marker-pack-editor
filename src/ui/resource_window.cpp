#include "ui/resource_window.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <unordered_map>
#include <vector>

#include "pack/attributes.h"
#include "pack/source.h"
#include "resources.h"
#include "ui/tree_window.h"

namespace ui::resource_window {

namespace {

// Extend this to list more kinds of file. Everything below works off it, so nothing else
// has to know what counts as an image.
constexpr const char* kImageExtensions[] = {".png", ".jpg", ".jpeg", ".bmp"};

// Entry paths are normalized, so they are already lowercase.
bool isImage(const std::string& path) {
    for (const char* extension : kImageExtensions) {
        const size_t length = std::strlen(extension);
        if (path.size() > length && path.compare(path.size() - length, length, extension) == 0) return true;
    }
    return false;
}

std::string folderOf(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

std::string nameOf(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

struct Asset {
    std::string path; // normalized
    std::string name; // basename
    bool pending_add = false;
};

struct Folder {
    std::string name; // one segment; empty for the root
    std::string path; // "data/icons"; empty for the root
    std::vector<size_t> folders;
    std::vector<size_t> assets;
};

// Rebuilt when the pack changes, not per frame. Counting references means walking every
// attribute of every node, and the largest pack tested has 56k markers; the folder tree
// falls out of the same pass, so the two are cached together.
struct Index {
    uint64_t revision = 0;
    const pack::Pack* pack = nullptr;
    const pack::DataSource* source = nullptr;

    std::vector<Asset> assets; // sorted by path
    std::vector<Folder> folders; // [0] is the root
    std::unordered_map<std::string, std::vector<editor::NodeRef>> references;
    size_t unreferenced = 0;
};

Index index;

// The asset the preview and the buttons act on, and the folder an added file lands in.
std::string selected;
std::string target_folder;
bool target_chosen = false; // the user picked the folder; stop defaulting it
char filter[64] = "";
bool open_remove_popup = false;

const ImVec4 kAddedColour(0.55f, 0.90f, 0.55f, 1.00f);
const ImVec4 kRemovedColour(1.00f, 0.45f, 0.45f, 1.00f);

size_t folderIndex(std::unordered_map<std::string, size_t>& by_path, const std::string& path) {
    if (path.empty()) return 0;

    auto it = by_path.find(path);
    if (it != by_path.end()) return it->second;

    const size_t parent = folderIndex(by_path, folderOf(path));
    index.folders.push_back(Folder{nameOf(path), path, {}, {}});

    const size_t self = index.folders.size() - 1;
    index.folders[parent].folders.push_back(self);
    by_path.emplace(path, self);
    return self;
}

// Any attribute whose kind is Path points at a file in the pack, so the attribute table
// decides what counts as a reference — `iconfile`, `texture`, `traildata` and anything
// added to the table later are all picked up without being named here.
void collectReferences(const pack::AttributeCollection& attributes, editor::NodeRef node) {
    for (const pack::Attribute& attribute : attributes.all()) {
        const pack::AttributeDef* def = pack::findAttribute(attribute.key);
        if (!def || def->kind != pack::ValueKind::Path || attribute.value.empty()) continue;
        index.references[pack::normalizePath(attribute.value)].push_back(node);
    }
}

void buildIndex(const editor::State& state) {
    const pack::DataSource* source = resources::source();

    // A different pack entirely: whatever folder the user picked in the last one means
    // nothing here.
    if (index.pack != &state.pack || index.source != source) target_chosen = false;

    index = Index{};
    index.revision = state.revision;
    index.pack = &state.pack;
    index.source = source;
    index.folders.push_back(Folder{});

    for (size_t i = 0; i < state.pack.categories.size(); ++i) {
        collectReferences(state.pack.categories[i].attributes, editor::NodeRef{editor::NodeKind::Category, i});
    }
    for (size_t i = 0; i < state.pack.pathables.size(); ++i) {
        collectReferences(state.pack.pathables[i].attributes, editor::NodeRef{editor::NodeKind::Pathable, i});
    }

    std::vector<std::string> paths;
    if (source) {
        for (const std::string& entry : source->entries()) {
            if (isImage(entry)) paths.push_back(entry);
        }
    }
    // A pending add may or may not already be in the source: adding over an existing file
    // is a replacement, and it must not show up twice.
    for (const pack::Pack::AddedAsset& added : state.pack.added_assets) {
        if (isImage(added.path)) paths.push_back(added.path);
    }
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());

    std::unordered_map<std::string, size_t> folders_by_path;
    index.assets.reserve(paths.size());
    for (const std::string& path : paths) {
        index.assets.push_back(Asset{path, nameOf(path), state.pack.findAddedAsset(path) != nullptr});
        index.folders[folderIndex(folders_by_path, folderOf(path))].assets.push_back(index.assets.size() - 1);
        if (index.references.find(path) == index.references.end()) ++index.unreferenced;
    }

    // A different pack, or the selected file was just removed: do not keep pointing at
    // something that is no longer listed.
    const bool still_there =
        std::any_of(index.assets.begin(), index.assets.end(), [](const Asset& asset) { return asset.path == selected; });
    if (!still_there) selected.clear();

    // Default the add target to wherever the pack already keeps most of its images, so the
    // first added file does not land in the pack root by accident. Once the user has picked
    // a folder, that stands — including the root.
    const bool folder_known =
        std::any_of(index.folders.begin(), index.folders.end(), [](const Folder& folder) { return folder.path == target_folder; });
    if (!target_chosen || !folder_known) {
        target_folder.clear();
        size_t most = 0;
        for (const Folder& folder : index.folders) {
            if (folder.assets.size() <= most) continue;
            most = folder.assets.size();
            target_folder = folder.path;
        }
    }
}

void ensureIndex(const editor::State& state) {
    // The source pointer changes when a pack is saved and reopened, which does not bump the
    // revision but does mean the pending adds are now real entries.
    if (index.revision == state.revision && index.pack == &state.pack && index.source == resources::source()) return;
    buildIndex(state);
}

const std::vector<editor::NodeRef>* referencesTo(const std::string& path) {
    auto it = index.references.find(path);
    return it == index.references.end() ? nullptr : &it->second;
}

size_t referenceCount(const std::string& path) {
    const std::vector<editor::NodeRef>* nodes = referencesTo(path);
    return nodes ? nodes->size() : 0;
}

void select(const Asset& asset) {
    selected = asset.path;
    target_folder = folderOf(asset.path);
    target_chosen = true;
}

void drawAssetRow(editor::State& state, size_t asset_index) {
    const Asset& asset = index.assets[asset_index];
    const bool removed = state.pack.isAssetRemoved(asset.path);
    const size_t references = referenceCount(asset.path);

    // The count goes in the label rather than being drawn after the row: a Selectable
    // spans the full width, so anything placed beside it would sit on top of it.
    std::string label = asset.name;
    label += references == 0 ? "   unused" : "   (" + std::to_string(references) + ")";
    if (asset.pending_add) label += "  [added]";
    if (removed) label += "  [removed]";

    ImGui::PushID(static_cast<int>(asset_index));
    if (removed) {
        ImGui::PushStyleColor(ImGuiCol_Text, kRemovedColour);
    } else if (asset.pending_add) {
        ImGui::PushStyleColor(ImGuiCol_Text, kAddedColour);
    }

    if (ImGui::Selectable(label.c_str(), selected == asset.path)) select(asset);

    if (removed || asset.pending_add) ImGui::PopStyleColor();
    ImGui::PopID();
}

void drawFolder(editor::State& state, size_t folder_index);

void drawFolderContents(editor::State& state, size_t folder_index) {
    for (size_t child : index.folders[folder_index].folders) drawFolder(state, child);
    for (size_t asset : index.folders[folder_index].assets) drawAssetRow(state, asset);
}

void drawFolder(editor::State& state, size_t folder_index) {
    const Folder& folder = index.folders[folder_index];

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_OpenOnArrow;
    if (target_folder == folder.path) flags |= ImGuiTreeNodeFlags_Selected;

    ImGui::PushID(static_cast<int>(folder_index));
    const bool open = ImGui::TreeNodeEx("##folder", flags, "%s", folder.name.c_str());
    // Clicking a folder is how the user says where an added file should go.
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        target_folder = folder.path;
        target_chosen = true;
    }
    if (open) {
        drawFolderContents(state, folder_index);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void drawFiltered(editor::State& state) {
    std::string needle = filter;
    std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    size_t matches = 0;
    for (size_t i = 0; i < index.assets.size(); ++i) {
        if (index.assets[i].path.find(needle) == std::string::npos) continue;
        drawAssetRow(state, i);
        ++matches;
    }
    if (matches == 0) ImGui::TextDisabled("Nothing matches.");
}

// The folder's spelling as the pack already writes it. Without this an added file would
// land in "data/icons" beside an existing "Data/icons" — two directories in the archive
// that differ only in case.
std::string originalFolder(const std::string& normalized) {
    const pack::DataSource* source = resources::source();
    if (!source || normalized.empty()) return normalized;

    for (const Asset& asset : index.assets) {
        if (folderOf(asset.path) != normalized) continue;

        std::string original = source->originalName(asset.path);
        if (original.size() != asset.path.size()) continue; // a spelling we cannot line up
        std::replace(original.begin(), original.end(), '\\', '/');
        return folderOf(original);
    }
    return normalized;
}

void removeSelected(editor::State& state) {
    state.pack.removeAsset(selected);
    // The bytes behind the path just changed — a pending add withdrawn, or a file that will
    // not be in the next save. Drop the cached texture so the preview follows.
    resources::forget(selected);
    state.touch();
}

void drawRemovePopup(editor::State& state) {
    constexpr const char* kTitle = "Remove image?";

    if (open_remove_popup) {
        ImGui::OpenPopup(kTitle);
        open_remove_popup = false;
    }

    if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    ImGui::Text("%zu marker(s) still point at this image.", referenceCount(selected));
    ImGui::TextDisabled("They will fall back to a plain dot until they point somewhere else.");
    ImGui::Separator();

    if (ImGui::Button("Remove", ImVec2(110, 0))) {
        removeSelected(state);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(110, 0))) ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

void drawPreview() {
    const resources::Image preview = resources::image(selected);
    if (!preview.texture) {
        ImGui::TextDisabled("(no preview)");
        return;
    }

    // Fit the long edge into a fixed box so a 1024px banner and a 32px icon both stay put.
    constexpr float kBox = 96.0f;
    const float longest = static_cast<float>(std::max(std::max(preview.width, preview.height), 1));
    const float scale = std::min(1.0f, kBox / longest);
    ImGui::Image(preview.texture, ImVec2(preview.width * scale, preview.height * scale));

    ImGui::SameLine();
    ImGui::TextDisabled("%d x %d", preview.width, preview.height);
}

void drawReferences(editor::State& state) {
    const std::vector<editor::NodeRef>* nodes = referencesTo(selected);
    if (!nodes) {
        ImGui::TextDisabled("Nothing references this.");
        return;
    }

    ImGui::TextDisabled("Referenced by %zu", nodes->size());
    ImGui::BeginChild("##references", ImVec2(0, 110), true);

    // Long lists are not worth drawing in full; a pack can point thousands of markers at
    // one icon and the point here is to identify them, not to enumerate them.
    constexpr size_t kMaxRows = 200;
    for (size_t i = 0; i < nodes->size() && i < kMaxRows; ++i) {
        const editor::NodeRef node = (*nodes)[i];

        // The full dotted path, not the tree's short name: this list is read away from the
        // hierarchy, where "Shogg" on its own says nothing about where it lives.
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Selectable(ui::tree_window::nodePath(state.pack, node).c_str())) {
            state.selection.set(node);
            state.requestReveal(node);
        }
        ImGui::PopID();
    }
    if (nodes->size() > kMaxRows) ImGui::TextDisabled("... and %zu more", nodes->size() - kMaxRows);

    ImGui::EndChild();
}

// Everything about the selected file: what it is, what uses it, and what can be done to it.
void drawSelected(editor::State& state) {
    if (selected.empty()) {
        ImGui::TextDisabled("Select an image to preview it.");
        return;
    }

    ImGui::TextWrapped("%s", selected.c_str());

    const bool pending_add = state.pack.findAddedAsset(selected) != nullptr;
    const bool removed = state.pack.isAssetRemoved(selected);

    drawPreview();

    if (pending_add) {
        if (ImGui::Button("Discard")) removeSelected(state);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drops the file again. It was never written to the pack.");
    } else if (removed) {
        if (ImGui::Button("Restore")) {
            state.pack.restoreAsset(selected);
            resources::forget(selected);
            state.touch();
        }
    } else if (ImGui::Button("Remove")) {
        // Removing something nothing points at needs no ceremony.
        if (referenceCount(selected) == 0) {
            removeSelected(state);
        } else {
            open_remove_popup = true;
        }
    }

    drawRemovePopup(state);

    ImGui::Separator();
    drawReferences(state);
}

} // namespace

bool draw(editor::State& state) {
    if (!state.loaded) {
        ImGui::TextDisabled("No pack open.");
        return false;
    }

    ensureIndex(state);

    bool wants_add = ImGui::Button("Add image...");
    ImGui::SameLine();
    ImGui::TextDisabled("%zu images, %zu unused", index.assets.size(), index.unreferenced);

    ImGui::TextDisabled("Add to:");
    ImGui::SameLine();
    ImGui::TextUnformatted(target_folder.empty() ? "(pack root)" : target_folder.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click a folder to change this.");

    ImGui::TextDisabled("Filter");
    ImGui::SameLine();
    ImGui::PushItemWidth(-1.0f);
    ImGui::InputText("##filter", filter, sizeof(filter));
    ImGui::PopItemWidth();

    ImGui::Separator();

    // The selected file's panel is a fixed strip at the bottom, so the list above it does
    // not jump around as the selection changes.
    constexpr float kSelectedHeight = 260.0f;
    const float available = ImGui::GetContentRegionAvail().y;
    const float list_height = std::max(80.0f, available - kSelectedHeight);

    ImGui::BeginChild("##assets", ImVec2(0, list_height), true);
    if (index.assets.empty()) {
        ImGui::TextDisabled("This pack ships no images.");
    } else if (filter[0] != '\0') {
        drawFiltered(state);
    } else {
        drawFolderContents(state, 0);
    }
    ImGui::EndChild();

    ImGui::Separator();
    drawSelected(state);

    return wants_add;
}

void addFile(editor::State& state, const std::filesystem::path& file, std::string* status) {
    if (!state.loaded) return;

    std::ifstream input(file, std::ios::binary);
    if (!input) {
        if (status) *status = "Could not open " + file.filename().string();
        return;
    }

    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        if (status) *status = file.filename().string() + " is empty";
        return;
    }

    const std::string folder = originalFolder(target_folder);
    const std::string destination = folder.empty() ? file.filename().string() : folder + "/" + file.filename().string();
    const std::string key = pack::normalizePath(destination);

    const pack::DataSource* source = resources::source();
    const bool replacing = (source && source->contains(key)) || state.pack.findAddedAsset(key) != nullptr;

    state.pack.addAsset(destination, std::move(bytes));
    resources::forget(key);
    state.touch();

    selected = key;
    if (status) *status = (replacing ? "Replaced " : "Added ") + destination + " - pending save";
}

} // namespace ui::resource_window
