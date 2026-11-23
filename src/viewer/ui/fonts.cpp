#include "viewer/ui/fonts.h"

#include <IconsFontAwesome7.h>
#include <d3d11.h>
#include <imgui.h>

#include <cmath>
#include <cstring>

#include "viewer/ui/font_awesome_solid.h"

namespace viewer::ui::fonts {

namespace {

// Its vectors come from ImGui's allocator, which is Nexus's, so unload() empties them rather than
// leave them to a destructor that runs as the DLL detaches.
ImFontAtlas atlas;
ID3D11ShaderResourceView* atlas_view = nullptr;
ImFont* font = nullptr;
bool tried = false;
ID3D11Device* atlas_device = nullptr;
std::string status_text = "not built yet";

constexpr ImWchar kIconRanges[] = {ICON_MIN_FA, ICON_MAX_16_FA, 0};

// Latin-1 and Latin Extended-A and B, as Nexus builds every font but its default one, whose
// Chinese would cost a large atlas for nothing our windows write.
constexpr ImWchar kTextRanges[] = {0x0020, 0x00FF, 0x0100, 0x024F, 0};

// How much of Font Awesome's line box is above the baseline, from its hhea table: 448 units of
// 512. Every icon spans that box.
constexpr float kIconAscent = 448.0f / 512.0f;

bool upload(ID3D11Device* device) {
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    atlas.GetTexDataAsRGBA32(&pixels, &width, &height);
    if (!pixels) {
        status_text = "the atlas did not build";
        return false;
    }

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

    ID3D11Texture2D* texture = nullptr;
    if (FAILED(device->CreateTexture2D(&description, &data, &texture))) {
        status_text = "could not create the atlas texture";
        return false;
    }
    const HRESULT viewed = device->CreateShaderResourceView(texture, nullptr, &atlas_view);
    texture->Release();
    if (FAILED(viewed)) {
        atlas_view = nullptr;
        status_text = "could not create the atlas view";
        return false;
    }
    atlas.TexID = static_cast<ImTextureID>(atlas_view);
    return true;
}

void clear() {
    atlas.Clear();
    atlas.TexID = nullptr;
    if (atlas_view) {
        atlas_view->Release();
        atlas_view = nullptr;
    }
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
void build(const ImFont* source) {
    const ImFontConfig* config = source->ConfigData;
    if (!atlas_device) {
        status_text = "no device";
        return;
    }
    if (!config || !config->FontData) {
        status_text = "the host's window font holds no font file";
        return;
    }

    ImFontConfig text = *config;
    text.FontData = IM_ALLOC(static_cast<size_t>(config->FontDataSize));
    std::memcpy(text.FontData, config->FontData, static_cast<size_t>(config->FontDataSize));
    text.FontDataOwnedByAtlas = true;
    text.GlyphRanges = kTextRanges;
    text.MergeMode = false;
    text.DstFont = nullptr;
    ImFont* built = atlas.AddFont(&text);

    ImFontConfig icons;
    icons.MergeMode = true;
    icons.GlyphMinAdvanceX = config->SizePixels;
    atlas.AddFontFromMemoryCompressedTTF(
        font_awesome_solid_compressed_data,
        static_cast<int>(font_awesome_solid_compressed_size),
        config->SizePixels,
        &icons,
        kIconRanges);

    const bool uploaded = upload(atlas_device);
    // The glyphs are all that is drawn from; the font files and the pixels were only for building.
    atlas.ClearInputData();
    atlas.ClearTexData();
    if (!uploaded) {
        clear();
        return;
    }
    alignIcons(built);
    font = built;
    status_text = std::string("built from ") + config->Name;
}

} // namespace

void load(ID3D11Device* device) {
    atlas_device = device;
}

void unload() {
    clear();
    font = nullptr;
    tried = false;
    status_text = "not built yet";
    atlas_device = nullptr;
}

void push() {
    const ImFont* current = ImGui::GetFont();
    if (!tried) {
        tried = true;
        build(current);
    }
    if (!font) {
        ImGui::PushFont(ImGui::GetFont());
        return;
    }
    // The host's font may since have been resized, and a window's scale applies on top of a font's.
    font->Scale = current->FontSize * current->Scale / font->FontSize;
    ImGui::PushFont(font);
}

void pop() {
    ImGui::PopFont();
}

bool ready() {
    return font != nullptr;
}

std::string label(const char* icon, const char* text) {
    if (!font) return text;
    return std::string(icon) + "  " + text;
}

const char* status() {
    return status_text.c_str();
}

} // namespace viewer::ui::fonts
