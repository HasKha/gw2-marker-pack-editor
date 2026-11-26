#include "editor/ui/resource_window.h"

#include <IconsFontAwesome7.h>
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#include "editor/resources.h"
#include "editor/ui/tree_window.h"
#include "editor/undo.h"
#include "pack/attributes.h"
#include "pack/source.h"
#include "util/path_text.h"
#include "viewer/ui/fonts.h"
#include "viewer/ui/style.h"

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

const ImVec4 kAddedColour(0.55f, 0.90f, 0.55f, 1.00f);
const ImVec4 kRemovedColour(1.00f, 0.45f, 0.45f, 1.00f);

} // namespace

size_t Window::folderIndex(std::unordered_map<std::string, size_t>& by_path, const std::string& path) {
    if (path.empty()) return 0;

    auto it = by_path.find(path);
    if (it != by_path.end()) return it->second;

    const size_t parent = folderIndex(by_path, folderOf(path));
    index_.folders.push_back(Folder{nameOf(path), path, {}, {}});

    const size_t self = index_.folders.size() - 1;
    index_.folders[parent].folders.push_back(self);
    by_path.emplace(path, self);
    return self;
}

// Any attribute whose kind is Path points at a file in the pack, so the attribute table
// decides what counts as a reference — `iconfile`, `texture`, `traildata` and anything
// added to the table later are all picked up without being named here.
void Window::collectReferences(const pack::AttributeCollection& attributes, editor::NodeRef node) {
    for (const pack::Attribute& attribute : attributes.all()) {
        const pack::AttributeDef* def = pack::findAttribute(attribute.key);
        if (!def || def->kind != pack::ValueKind::Path || attribute.value.empty()) continue;
        index_.references[pack::normalizePath(attribute.value)].push_back(node);
    }
}

void Window::buildIndex(const editor::State& state) {
    const pack::DataSource* source = state.resources.source();

    // A different pack entirely: whatever folder the user picked in the last one means
    // nothing here.
    if (index_.source != source) target_chosen_ = false;

    index_ = Index{};
    index_.revision = state.revision;
    index_.source = source;
    index_.folders.push_back(Folder{});

    // Deleted nodes reference nothing: an image only used by a marker that has been removed
    // should read as unused, or it could never be cleaned up.
    for (size_t i = 0; i < state.pack.categories.size(); ++i) {
        if (state.pack.categories[i].grave.deleted) continue;
        Window::collectReferences(state.pack.categories[i].attributes, editor::NodeRef{editor::NodeKind::Category, i});
    }
    for (size_t i = 0; i < state.pack.pathables.size(); ++i) {
        if (state.pack.pathables[i].grave.deleted) continue;
        Window::collectReferences(state.pack.pathables[i].attributes, editor::NodeRef{editor::NodeKind::Pathable, i});
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
    index_.assets.reserve(paths.size());
    for (const std::string& path : paths) {
        index_.assets.push_back(Asset{path, nameOf(path), state.pack.findAddedAsset(path) != nullptr});
        index_.folders[folderIndex(folders_by_path, folderOf(path))].assets.push_back(index_.assets.size() - 1);
        if (index_.references.find(path) == index_.references.end()) ++index_.unreferenced;
    }

    // A different pack, or the selected file was just removed: do not keep pointing at
    // something that is no longer listed.
    const bool still_there =
        std::any_of(index_.assets.begin(), index_.assets.end(), [this](const Asset& asset) { return asset.path == selected_; });
    if (!still_there) selected_.clear();

    // Default the add target to wherever the pack already keeps most of its images, so the
    // first added file does not land in the pack root by accident. Once the user has picked
    // a folder, that stands — including the root.
    const bool folder_known =
        std::any_of(index_.folders.begin(), index_.folders.end(), [this](const Folder& folder) { return folder.path == target_folder_; });
    if (!target_chosen_ || !folder_known) {
        target_folder_.clear();
        size_t most = 0;
        for (const Folder& folder : index_.folders) {
            if (folder.assets.size() <= most) continue;
            most = folder.assets.size();
            target_folder_ = folder.path;
        }
    }
}

void Window::ensureIndex(const editor::State& state) {
    if (index_.revision == state.revision && index_.source == state.resources.source()) return;
    Window::buildIndex(state);
}

const std::vector<editor::NodeRef>* Window::referencesTo(const std::string& path) const {
    auto it = index_.references.find(path);
    return it == index_.references.end() ? nullptr : &it->second;
}

size_t Window::referenceCount(const std::string& path) const {
    const std::vector<editor::NodeRef>* nodes = referencesTo(path);
    return nodes ? nodes->size() : 0;
}

void Window::select(const Asset& asset) {
    selected_ = asset.path;
    target_folder_ = folderOf(asset.path);
    target_chosen_ = true;
}

void Window::drawAssetRow(editor::State& state, size_t asset_index) {
    const Asset& asset = index_.assets[asset_index];
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

    if (ImGui::Selectable(label.c_str(), selected_ == asset.path)) select(asset);

    if (removed || asset.pending_add) ImGui::PopStyleColor();
    ImGui::PopID();
}

void Window::drawFolderContents(editor::State& state, size_t folder_index) {
    for (size_t child : index_.folders[folder_index].folders) drawFolder(state, child);
    for (size_t asset : index_.folders[folder_index].assets) drawAssetRow(state, asset);
}

void Window::drawFolder(editor::State& state, size_t folder_index) {
    const Folder& folder = index_.folders[folder_index];

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_OpenOnArrow;
    if (target_folder_ == folder.path) flags |= ImGuiTreeNodeFlags_Selected;

    ImGui::PushID(static_cast<int>(folder_index));
    const bool open = ImGui::TreeNodeEx("##folder", flags, "%s", folder.name.c_str());
    // Clicking a folder is how the user says where an added file should go.
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        target_folder_ = folder.path;
        target_chosen_ = true;
    }
    if (open) {
        Window::drawFolderContents(state, folder_index);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void Window::drawFiltered(editor::State& state) {
    std::string needle = filter_;
    std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    size_t matches = 0;
    for (size_t i = 0; i < index_.assets.size(); ++i) {
        if (index_.assets[i].path.find(needle) == std::string::npos) continue;
        Window::drawAssetRow(state, i);
        ++matches;
    }
    if (matches == 0) ImGui::TextDisabled("Nothing matches.");
}

// The folder's spelling as the pack already writes it. Without this an added file would
// land in "data/icons" beside an existing "Data/icons" — two directories in the archive
// that differ only in case.
std::string Window::originalFolder(const editor::State& state, const std::string& normalized) const {
    const pack::DataSource* source = state.resources.source();
    if (!source || normalized.empty()) return normalized;

    for (const Asset& asset : index_.assets) {
        if (folderOf(asset.path) != normalized) continue;

        std::string original = source->originalName(asset.path);
        if (original.size() != asset.path.size()) continue; // a spelling we cannot line up
        std::replace(original.begin(), original.end(), '\\', '/');
        return folderOf(original);
    }
    return normalized;
}

void Window::removeSelected(editor::State& state) {
    undo::Step step(state, "Remove image");
    undo::asset(state, selected_);

    state.pack.removeAsset(selected_);
    // The bytes behind the path just changed — a pending add withdrawn, or a file that will
    // not be in the next save. Drop the cached texture so the preview follows.
    state.resources.forget(selected_);
    state.touch();
}

void Window::drawRemovePopup(editor::State& state) {
    constexpr const char* kTitle = "Remove Image?";

    if (open_remove_popup_) {
        ImGui::OpenPopup(kTitle);
        open_remove_popup_ = false;
    }

    if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    const size_t references = referenceCount(selected_);
    ImGui::Text(references == 1 ? "%zu marker still points at this image." : "%zu markers still point at this image.", references);
    ImGui::TextDisabled("They will fall back to a plain dot until they point somewhere else.");
    ImGui::Separator();

    const std::string remove = viewer::ui::fonts::label(ICON_FA_TRASH, "Remove");
    const float width = viewer::ui::style::buttonWidth({remove.c_str(), "Cancel"});
    if (ImGui::Button(remove.c_str(), ImVec2(width, 0))) {
        Window::removeSelected(state);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(width, 0))) ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

void Window::drawPreview(editor::State& state) {
    const viewer::Image preview = state.resources.image(selected_);
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

void Window::drawReferences(editor::State& state) {
    const std::vector<editor::NodeRef>* nodes = referencesTo(selected_);
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
void Window::drawSelected(editor::State& state) {
    if (selected_.empty()) {
        ImGui::TextDisabled("Select an image to preview it.");
        return;
    }

    ImGui::TextWrapped("%s", selected_.c_str());

    const bool pending_add = state.pack.findAddedAsset(selected_) != nullptr;
    const bool removed = state.pack.isAssetRemoved(selected_);

    Window::drawPreview(state);

    if (pending_add) {
        if (ImGui::Button(viewer::ui::fonts::label(ICON_FA_XMARK, "Discard").c_str())) removeSelected(state);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drops the file again. It was never written to the pack.");
    } else if (removed) {
        if (ImGui::Button(viewer::ui::fonts::label(ICON_FA_ROTATE_LEFT, "Restore").c_str())) {
            undo::Step step(state, "Restore image");
            undo::asset(state, selected_);
            state.pack.restoreAsset(selected_);
            state.resources.forget(selected_);
            state.touch();
        }
    } else if (ImGui::Button(viewer::ui::fonts::label(ICON_FA_TRASH, "Remove").c_str())) {
        // Removing something nothing points at needs no ceremony.
        if (referenceCount(selected_) == 0) {
            Window::removeSelected(state);
        } else {
            open_remove_popup_ = true;
        }
    }

    Window::drawRemovePopup(state);

    ImGui::Separator();
    Window::drawReferences(state);
}

bool Window::draw(editor::State& state) {
    if (!state.loaded) {
        ImGui::TextDisabled("No pack open.");
        return false;
    }

    Window::ensureIndex(state);

    bool wants_add = ImGui::Button(viewer::ui::fonts::label(ICON_FA_IMAGE, "Add Image...").c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%zu images, %zu unused", index_.assets.size(), index_.unreferenced);

    ImGui::TextDisabled("Add to:");
    ImGui::SameLine();
    ImGui::TextUnformatted(target_folder_.empty() ? "(pack root)" : target_folder_.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click a folder to change this.");

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##filter", "Filter by file name", &filter_);

    ImGui::Separator();

    // The selected file's panel is a fixed strip at the bottom, so the list above it does
    // not jump around as the selection changes.
    constexpr float kSelectedHeight = 260.0f;
    const float available = ImGui::GetContentRegionAvail().y;
    const float list_height = std::max(80.0f, available - kSelectedHeight);

    ImGui::BeginChild("##assets", ImVec2(0, list_height), true);
    if (index_.assets.empty()) {
        ImGui::TextDisabled("This pack ships no images.");
    } else if (!filter_.empty()) {
        Window::drawFiltered(state);
    } else {
        Window::drawFolderContents(state, 0);
    }
    ImGui::EndChild();

    ImGui::Separator();
    Window::drawSelected(state);

    return wants_add;
}

void Window::addFile(editor::State& state, const std::filesystem::path& file, std::string* status) {
    if (!state.loaded) return;

    std::ifstream input(file, std::ios::binary);
    if (!input) {
        if (status) *status = "Could not open " + util::pathText(file.filename());
        return;
    }

    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        if (status) *status = util::pathText(file.filename()) + " is empty";
        return;
    }

    const std::string folder = originalFolder(state, target_folder_);
    const std::string destination = folder.empty() ? util::pathText(file.filename()) : folder + "/" + util::pathText(file.filename());
    const std::string key = pack::normalizePath(destination);

    const pack::DataSource* source = state.resources.source();
    const bool replacing = (source && source->contains(key)) || state.pack.findAddedAsset(key) != nullptr;

    undo::Step step(state, "Add image");
    undo::asset(state, key);

    state.pack.addAsset(destination, std::move(bytes));
    state.resources.forget(key);
    state.touch();

    selected_ = key;
    if (status) *status = (replacing ? "Replaced " : "Added ") + destination + " - pending save";
}

} // namespace ui::resource_window
