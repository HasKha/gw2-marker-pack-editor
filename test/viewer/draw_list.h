#pragma once

#include <imgui.h>

// What the tests that draw need of ImGui, with no device behind it.
namespace viewer::test {

// A draw list needs a context for its shared data, and nothing else of one.
class DrawListFixture
{
public:
    DrawListFixture()
        : context(ImGui::CreateContext())
        , draw_list(ImGui::GetDrawListSharedData()) {
        draw_list._ResetForNewFrame();
        draw_list.PushClipRectFullScreen();
    }
    ~DrawListFixture() { ImGui::DestroyContext(context); }

    DrawListFixture(const DrawListFixture&) = delete;
    DrawListFixture& operator=(const DrawListFixture&) = delete;

    ImDrawList& list() { return draw_list; }

private:
    ImGuiContext* context;
    // After the context, whose shared data it draws with.
    ImDrawList draw_list;
};

// Any non-null id will do: nothing samples it.
inline ImTextureID fakeTexture() {
    static int texture = 0;
    return &texture;
}

} // namespace viewer::test
