#pragma once

#include <imgui.h>

// What the tests that draw need of ImGui, with no device behind it.
namespace viewer::test {

// A draw list needs a context for its shared data, and nothing else of one.
class DrawListFixture
{
public:
    DrawListFixture()
        : context_(ImGui::CreateContext())
        , list_(ImGui::GetDrawListSharedData()) {
        list_._ResetForNewFrame();
        list_.PushClipRectFullScreen();
    }
    ~DrawListFixture() { ImGui::DestroyContext(context_); }

    DrawListFixture(const DrawListFixture&) = delete;
    DrawListFixture& operator=(const DrawListFixture&) = delete;

    ImDrawList& list() { return list_; }

private:
    ImGuiContext* context_;
    ImDrawList list_;
};

// Any non-null id will do: nothing samples it.
inline ImTextureID fakeTexture() {
    static int texture = 0;
    return &texture;
}

} // namespace viewer::test
