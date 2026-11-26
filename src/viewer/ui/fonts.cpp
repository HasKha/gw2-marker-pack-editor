#include "viewer/ui/fonts.h"

#include <IconsFontAwesome7.h>
#include <d3d11.h>
#include <imgui.h>
#include <wrl/client.h>

#include <cmath>
#include <cstring>

#include "viewer/ui/font_awesome_solid.h"

namespace viewer::ui::fonts {

struct Fonts::State {
    // Its vectors come from ImGui's allocator, which is Nexus's: it goes with the session, while
    // Nexus is still there, not as the DLL detaches.
    ImFontAtlas atlas;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    ImFont* font = nullptr;
    ImFont* heading = nullptr;
    bool tried = false;
    ID3D11Device* device = nullptr; // the viewer's, which holds it
};

namespace {

// The session's, while it lives. Read and written on the render thread only.
Fonts::State* current = nullptr;

// How much larger pushHeading()'s font is than the text: enough to stand out as a title in a
// list, small enough that a long name still fits beside its author.
constexpr float kHeadingScale = 1.2f;

constexpr ImWchar kIconRanges[] = {ICON_MIN_FA, ICON_MAX_16_FA, 0};

// Latin-1 and Latin Extended-A and B, as Nexus builds every font but its default one, whose
// Chinese would cost a large atlas for nothing our windows write.
constexpr ImWchar kTextRanges[] = {0x0020, 0x00FF, 0x0100, 0x024F, 0};

// How much of Font Awesome's line box is above the baseline, from its hhea table: 448 units of
// 512. Every icon spans that box.
constexpr float kIconAscent = 448.0f / 512.0f;

bool upload(Fonts::State& fonts) {
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    fonts.atlas.GetTexDataAsRGBA32(&pixels, &width, &height);
    if (!pixels) return false;

    D3D11_TEXTURE2D_DESC description{};
    description.Width = static_cast<UINT>(width);
    description.Height = static_cast<UINT>(height);
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_IMMUTABLE;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA data{pixels, static_cast<UINT>(width) * 4, 0};

    // The view holds the texture, so ours goes when this returns.
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    if (FAILED(fonts.device->CreateTexture2D(&description, &data, &texture))) return false;
    if (FAILED(fonts.device->CreateShaderResourceView(texture.Get(), nullptr, &fonts.view))) {
        fonts.view.Reset();
        return false;
    }
    fonts.atlas.TexID = static_cast<ImTextureID>(fonts.view.Get());
    return true;
}

// Moves the icons so that Font Awesome's line box lies on the text's, from the top of the line to
// its bottom. After the build, because where ImGui puts a merged glyph depends on the text font's
// ascent, which only the build reads.
void alignIcons(ImFont* built) {
    const float icon_top = std::round(built->Ascent) - kIconAscent * built->FontSize;
    for (ImFontGlyph& glyph : built->Glyphs) {
        if (glyph.Codepoint >= ICON_MIN_FA && glyph.Codepoint <= ICON_MAX_16_FA) {
            glyph.Y0 -= icon_top;
            glyph.Y1 -= icon_top;
        }
    }
}

// A copy of `source`'s font file at its size, with the icons merged into it. The copy is ours
// because Nexus frees its own when the player picks another font.
void build(Fonts::State& fonts, const ImFont* source) {
    // Without a device, or with a host font that holds no font file, the windows keep the host's.
    const ImFontConfig* config = source->ConfigData;
    if (!fonts.device || !config || !config->FontData) return;

    ImFontConfig text = *config;
    text.FontData = IM_ALLOC(static_cast<size_t>(config->FontDataSize));
    std::memcpy(text.FontData, config->FontData, static_cast<size_t>(config->FontDataSize));
    text.FontDataOwnedByAtlas = true;
    text.GlyphRanges = kTextRanges;
    text.MergeMode = false;
    text.DstFont = nullptr;
    ImFont* built = fonts.atlas.AddFont(&text);

    ImFontConfig icons;
    icons.MergeMode = true;
    icons.GlyphMinAdvanceX = config->SizePixels;
    fonts.atlas.AddFontFromMemoryCompressedTTF(
        font_awesome_solid_compressed_data,
        static_cast<int>(font_awesome_solid_compressed_size),
        config->SizePixels,
        &icons,
        kIconRanges);

    // The heading, built at its size rather than scaled up from the text's, which blurs.
    ImFontConfig large = *config;
    large.FontData = IM_ALLOC(static_cast<size_t>(config->FontDataSize));
    std::memcpy(large.FontData, config->FontData, static_cast<size_t>(config->FontDataSize));
    large.FontDataOwnedByAtlas = true;
    large.SizePixels = config->SizePixels * kHeadingScale;
    large.GlyphRanges = kTextRanges;
    large.MergeMode = false;
    large.DstFont = nullptr;
    ImFont* built_heading = fonts.atlas.AddFont(&large);

    const bool uploaded = upload(fonts);
    // The glyphs are all that is drawn from; the font files and the pixels were only for building.
    fonts.atlas.ClearInputData();
    fonts.atlas.ClearTexData();
    if (!uploaded) {
        fonts.atlas.Clear();
        fonts.atlas.TexID = nullptr;
        return;
    }
    alignIcons(built);
    fonts.font = built;
    fonts.heading = built_heading;
}

} // namespace

Fonts::Fonts()
    : state_(std::make_unique<State>()) {
    current = state_.get();
}

Fonts::~Fonts() {
    if (current == state_.get()) current = nullptr;
}

void Fonts::setDevice(ID3D11Device* device) {
    state_->device = device;
}

void push() {
    const ImFont* host = ImGui::GetFont();
    if (current && !current->tried) {
        current->tried = true;
        build(*current, host);
    }
    ImFont* font = current ? current->font : nullptr;
    if (!font) {
        ImGui::PushFont(ImGui::GetFont());
        return;
    }
    // The host's font may since have been resized, and a window's scale applies on top of a font's.
    font->Scale = host->FontSize * host->Scale / font->FontSize;
    ImGui::PushFont(font);
}

void pop() {
    ImGui::PopFont();
}

void pushHeading() {
    const ImFont* host = ImGui::GetFont();
    ImFont* heading = current ? current->heading : nullptr;
    if (!heading) {
        ImGui::PushFont(ImGui::GetFont());
        return;
    }
    heading->Scale = host->FontSize * host->Scale * kHeadingScale / heading->FontSize;
    ImGui::PushFont(heading);
}

bool ready() {
    return current && current->font;
}

std::string label(const char* icon, const char* text) {
    if (!ready()) return text;
    return std::string(icon) + "  " + text;
}

} // namespace viewer::ui::fonts
