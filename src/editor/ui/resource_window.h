#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "editor/editor_state.h"

namespace ui::resource_window {

// The Pack Resources window: the pack's images as a folder tree, what references each one,
// and a preview of the selected file.
class Window
{
public:
    // Draws into the current window.
    //
    // Returns true when the user asked to add a file. The native file dialog is owned by
    // marker_editor — one runs at a time and it is polled from the render callback — so the
    // request comes back out rather than being started here.
    bool draw(editor::State& state);

    // Brings `file` into the pack, under the folder the window is pointed at. Like every other
    // change it is pending until the pack is saved.
    void addFile(editor::State& state, const std::filesystem::path& file, std::string* status);

private:
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
    // Keyed on the revision, plus the source: a save-and-reopen swaps the DataSource without
    // touching the revision, and that is exactly when the pending adds become real entries.
    struct Index {
        uint64_t revision = 0;
        const pack::DataSource* source = nullptr;

        std::vector<Asset> assets; // sorted by path
        std::vector<Folder> folders; // [0] is the root
        std::unordered_map<std::string, std::vector<editor::NodeRef>> references;
        size_t unreferenced = 0;
    };

    size_t folderIndex(std::unordered_map<std::string, size_t>& by_path, const std::string& path);
    void collectReferences(const pack::AttributeCollection& attributes, editor::NodeRef node);
    void buildIndex(const editor::State& state);
    void ensureIndex(const editor::State& state);
    const std::vector<editor::NodeRef>* referencesTo(const std::string& path) const;
    size_t referenceCount(const std::string& path) const;
    void select(const Asset& asset);
    void drawAssetRow(editor::State& state, size_t asset_index);
    void drawFolderContents(editor::State& state, size_t folder_index);
    void drawFolder(editor::State& state, size_t folder_index);
    void drawFiltered(editor::State& state);
    std::string originalFolder(const editor::State& state, const std::string& normalized) const;
    void removeSelected(editor::State& state);
    void drawRemovePopup(editor::State& state);
    void drawPreview(editor::State& state);
    void drawReferences(editor::State& state);
    void drawSelected(editor::State& state);

    Index index_;

    // The asset the preview and the buttons act on, and the folder an added file lands in.
    std::string selected_;
    std::string target_folder_;
    bool target_chosen_ = false; // the user picked the folder; stop defaulting it
    std::string filter_;
    bool open_remove_popup_ = false;
};

} // namespace ui::resource_window
