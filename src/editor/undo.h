#pragma once

#include <string>

#include "editor/editor_state.h"

// Undo and redo as before/after images of attributes, existence and files, which works because no
// index ever moves (docs/architecture.md), kept in State::history. Open a Step, capture what is
// about to change, change it:
//
//     {
//         undo::Step step(state, "Move marker", "gizmo");
//         undo::attributes(state, node);
//         ... write the attributes ...
//     }
namespace undo {

// Steps sharing a merge key coalesce while they keep being reopened on consecutive frames,
// so a drag that fires every frame is one entry rather than sixty.
void beginFrame(editor::State& state);

// Everything is per-pack. Opening or closing one starts over.
void clear(editor::State& state);

class Step
{
public:
    Step(editor::State& state, std::string label, std::string merge_key = {});
    ~Step();

    Step(const Step&) = delete;
    Step& operator=(const Step&) = delete;

private:
    editor::State& state_;
};

// All of these take the before-image now; the after-image is read when the step closes.
// Safe to call twice for the same thing — the first call is the one that counts.
void attributes(editor::State& state, editor::NodeRef node);
void existence(editor::State& state, editor::NodeRef node);
void asset(editor::State& state, const std::string& path);

// For something that did not exist when the step opened, so there is no before-image to
// take. Call it once the node is there; undoing removes it again.
void created(editor::State& state, editor::NodeRef node);

bool canUndo(const editor::State& state);
bool canRedo(const editor::State& state);
std::string undoLabel(const editor::State& state);
std::string redoLabel(const editor::State& state);

void undo(editor::State& state);
void redo(editor::State& state);

} // namespace undo
