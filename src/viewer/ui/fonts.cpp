#include "viewer/ui/fonts.h"

#include <IconsFontAwesome7.h>
#include <ShlObj.h>
#include <d3d11.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include "viewer/ui/font_awesome_solid.h"
#include "viewer/ui/glyphs.h"

namespace viewer::ui::fonts {

// Ranges are built as uint16_t and handed to ImGui as they are.
static_assert(std::is_same_v<ImWchar, uint16_t>);

struct Fonts::State {
    // Its vectors come from ImGui's allocator, which is Nexus's: it goes with the session, while
    // Nexus is still there, not as the DLL detaches.
    ImFontAtlas atlas;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    // The views of atlases built over, each with the frame it was retired in: what that frame
    // drew before the build still samples it.
    std::vector<std::pair<int, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>>> retired;
    ImFont* font = nullptr;
    ImFont* heading = nullptr;
    ImFont* overlay = nullptr;
    float overlay_pixels = 0.0f; // what pushOverlay() last asked for; none built while 0
    bool tried = false;
    ID3D11Device* device = nullptr; // the viewer's, which holds it

    // The host's font as the first push found it, which every build is made from: Nexus frees
    // its own when the player picks another font. FontData is null; the file is `host_data`.
    ImFontConfig host;
    std::vector<unsigned char> host_data;

    glyphs::Set wanted; // everything require() was handed
    bool stale = false; // `wanted` holds characters the atlas was not built with, or `overlay_pixels` a size
    double built_at = 0.0; // ImGui::GetTime() at the last build
};

namespace {

// The session's, while it lives. Read and written on the render thread only.
Fonts::State* current = nullptr;

// How much larger pushHeading()'s font is than the text: enough to stand out as a title in a
// list, small enough that a long name still fits beside its author.
constexpr float kHeadingScale = 1.2f;

constexpr ImWchar kIconRanges[] = {ICON_MIN_FA, ICON_MAX_16_FA, 0};

// How much of Font Awesome's line box is above the baseline, from its hhea table: 448 units of
// 512. Every icon spans that box.
constexpr float kIconAscent = 448.0f / 512.0f;

// The least time between two builds, in seconds. Packs install one after another as the game
// starts, each with characters of its own; a build per pack would read and rasterize the CJK
// font for each. Until the next build a new character draws as ImGui's '?'.
constexpr double kRebuildInterval = 1.0;

// Windows' own fonts, tried in order for what the host's font does not have, each for the blocks
// it is there for (every block when none). All ship with Windows 10 and 11 whatever its language.
constexpr glyphs::Block kCjk[] = {{0x2E80, 0x9FFF}, {0xF900, 0xFAFF}, {0xFE30, 0xFE4F}, {0xFF00, 0xFFEF}};
constexpr glyphs::Block kHangul[] = {{0x1100, 0x11FF}, {0x3130, 0x318F}, {0xA960, 0xA97F}, {0xAC00, 0xD7FF}};

struct Fallback {
    const wchar_t* file;
    std::span<const glyphs::Block> blocks;
};

constexpr Fallback kFallbacks[] = {
    {L"segoeui.ttf", {}}, // Latin beyond Extended-B, Greek, Cyrillic, Armenian, Georgian, Hebrew
    {L"msyh.ttc", kCjk}, // Microsoft YaHei: Chinese, simplified and traditional, and Japanese kana
    {L"malgun.ttf", kHangul}, // Malgun Gothic: Korean
    {L"seguisym.ttf", {}}, // Segoe UI Symbol: arrows, stars, dingbats, box drawing
};

// One fallback's file, read for a build, with the characters it is asked for.
struct FallbackFile {
    std::vector<unsigned char> data;
    std::vector<uint16_t> ranges;
};

std::filesystem::path fontsFolder() {
    PWSTR folder = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Fonts, 0, nullptr, &folder))) return {};
    std::filesystem::path path(folder);
    CoTaskMemFree(folder);
    return path;
}

std::vector<unsigned char> readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return {};
    std::vector<unsigned char> data(static_cast<size_t>(in.tellg()));
    in.seekg(0);
    if (!in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()))) return {};
    return data;
}

// The fallbacks with something in `wanted` to try, read. None for an empty set, so a player
// whose packs are all Latin reads no font file of Windows'.
std::vector<FallbackFile> readFallbacks(const glyphs::Set& wanted) {
    std::vector<FallbackFile> files;
    if (wanted.empty()) return files;
    const std::filesystem::path folder = fontsFolder();
    if (folder.empty()) return files;
    for (const Fallback& fallback : kFallbacks) {
        std::vector<uint16_t> ranges = wanted.ranges(fallback.blocks);
        if (ranges.size() == 1) continue;
        std::vector<unsigned char> data = readFile(folder / fallback.file);
        if (!data.empty()) files.push_back({std::move(data), std::move(ranges)});
    }
    return files;
}

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

// The host's font at `size`, for `ranges`, as a new font of the atlas.
ImFont* addHost(Fonts::State& fonts, float size, const std::vector<uint16_t>& ranges) {
    ImFontConfig config = fonts.host;
    config.FontData = IM_ALLOC(fonts.host_data.size());
    std::memcpy(config.FontData, fonts.host_data.data(), fonts.host_data.size());
    config.FontDataSize = static_cast<int>(fonts.host_data.size());
    config.FontDataOwnedByAtlas = true;
    config.SizePixels = size;
    config.GlyphRanges = ranges.data();
    config.MergeMode = false;
    config.DstFont = nullptr;
    return fonts.atlas.AddFont(&config);
}

// Each fallback at `size`, merged into the font added last: the host's characters stay its own.
void mergeFallbacks(Fonts::State& fonts, std::vector<FallbackFile>& files, float size) {
    for (FallbackFile& file : files) {
        ImFontConfig config;
        config.FontData = file.data.data();
        config.FontDataSize = static_cast<int>(file.data.size());
        config.FontDataOwnedByAtlas = false; // `files` outlives the build, and is read once for both sizes
        config.SizePixels = size;
        config.GlyphRanges = file.ranges.data();
        config.MergeMode = true;
        // Thousands of CJK glyphs: oversampling would triple the atlas for no gain at this size.
        config.OversampleH = 1;
        config.OversampleV = 1;
        config.PixelSnapH = true;
        fonts.atlas.AddFont(&config);
    }
}

// Builds the atlas from the start from the host's font, with the icons merged in and, when
// `with_fallbacks`, Windows' fonts for the wanted characters the host's lacks. Whether it worked;
// without, the windows keep the host's font.
bool build(Fonts::State& fonts, bool with_fallbacks) {
    // What this frame drew so far sampled the old texture, so its view stays to the next frame.
    if (fonts.view) fonts.retired.emplace_back(ImGui::GetFrameCount(), std::move(fonts.view));
    fonts.atlas.Clear();
    fonts.atlas.TexID = nullptr;
    fonts.font = nullptr;
    fonts.heading = nullptr;
    fonts.overlay = nullptr;

    std::vector<uint16_t> ranges;
    for (const glyphs::Block& block : glyphs::kBase) {
        ranges.push_back(static_cast<uint16_t>(block.first));
        ranges.push_back(static_cast<uint16_t>(block.last));
    }
    // The host's font is tried for the wanted characters first: one with Cyrillic draws its own.
    const std::vector<uint16_t> wanted = fonts.wanted.ranges();
    ranges.insert(ranges.end(), wanted.begin(), wanted.end());

    std::vector<FallbackFile> fallbacks = with_fallbacks ? readFallbacks(fonts.wanted) : std::vector<FallbackFile>{};
    const float size = fonts.host.SizePixels;

    ImFont* built = addHost(fonts, size, ranges);
    ImFontConfig icons;
    icons.MergeMode = true;
    icons.GlyphMinAdvanceX = size;
    fonts.atlas.AddFontFromMemoryCompressedTTF(
        font_awesome_solid_compressed_data,
        static_cast<int>(font_awesome_solid_compressed_size),
        size,
        &icons,
        kIconRanges);
    mergeFallbacks(fonts, fallbacks, size);

    // The heading, built at its size rather than scaled up from the text's, which blurs.
    ImFont* built_heading = addHost(fonts, size * kHeadingScale, ranges);
    mergeFallbacks(fonts, fallbacks, size * kHeadingScale);

    ImFont* built_overlay = nullptr;
    if (fonts.overlay_pixels > 0.0f) {
        built_overlay = addHost(fonts, fonts.overlay_pixels, ranges);
        mergeFallbacks(fonts, fallbacks, fonts.overlay_pixels);
    }

    const bool uploaded = upload(fonts);
    // The glyphs are all that is drawn from; the font files and the pixels were only for building.
    fonts.atlas.ClearInputData();
    fonts.atlas.ClearTexData();
    if (!uploaded) {
        fonts.atlas.Clear();
        fonts.atlas.TexID = nullptr;
        return false;
    }
    alignIcons(built);
    fonts.font = built;
    fonts.heading = built_heading;
    fonts.overlay = built_overlay;
    return true;
}

// Builds the atlas with what is wanted, falling back to the host's font alone, which is how it
// always worked, should Windows' fonts not build: an atlas past the device's largest texture.
void rebuild(Fonts::State& fonts) {
    fonts.stale = false;
    fonts.built_at = ImGui::GetTime();
    if (!build(fonts, true)) build(fonts, false);
}

// Keeps a copy of `source`'s font file and how it was added, to build from. False when it holds
// no file, or there is no device to build on: the windows keep the host's font.
bool takeHost(Fonts::State& fonts, const ImFont* source) {
    const ImFontConfig* config = source->ConfigData;
    if (!fonts.device || !config || !config->FontData || config->FontDataSize <= 0) return false;
    const auto* data = static_cast<const unsigned char*>(config->FontData);
    fonts.host_data.assign(data, data + config->FontDataSize);
    fonts.host = *config;
    fonts.host.FontData = nullptr;
    fonts.host.FontDataSize = 0;
    fonts.host.GlyphRanges = nullptr;
    fonts.host.DstFont = nullptr;
    return true;
}

// What is being typed into a field, ours or Nexus's, is wanted too: a pasted or typed character
// the font lacks draws from the next build on.
void wantTyped(Fonts::State& fonts) {
    const ImGuiContext& g = *ImGui::GetCurrentContext();
    if (!g.ActiveId || g.InputTextState.ID != g.ActiveId) return;
    const ImGuiInputTextState& state = g.InputTextState;
    for (int i = 0; i < state.CurLenW && i < state.TextW.Size; ++i) {
        if (fonts.wanted.add(static_cast<char32_t>(state.TextW[i]))) fonts.stale = true;
    }
}

// The first push builds; a later one builds again once something new is wanted, unless it is
// inside one of ours, whose font a build would free.
void update(Fonts::State& fonts, const ImFont* host) {
    const int frame = ImGui::GetFrameCount();
    std::erase_if(fonts.retired, [frame](const auto& retired) { return retired.first < frame; });
    if (!fonts.tried) {
        fonts.tried = true;
        if (takeHost(fonts, host)) rebuild(fonts);
        return;
    }
    if (fonts.host_data.empty()) return;
    wantTyped(fonts);
    if (fonts.stale && host->ContainerAtlas != &fonts.atlas && ImGui::GetTime() - fonts.built_at >= kRebuildInterval) {
        rebuild(fonts);
    }
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
    if (current) update(*current, host);
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

void pushOverlay(float pixels) {
    // Whole pixels, so a size that drifts by a fraction, as a scale worked out per frame may, does
    // not ask for a build every time.
    const float wanted = std::max(1.0f, std::round(pixels));
    if (current && current->overlay_pixels != wanted) {
        current->overlay_pixels = wanted;
        current->stale = true;
    }
    ImFont* font = current ? (current->overlay ? current->overlay : current->heading) : nullptr;
    if (!font) {
        ImGui::PushFont(ImGui::GetFont());
        return;
    }
    font->Scale = wanted / font->FontSize;
    ImGui::PushFont(font);
}

void require(const glyphs::Set& glyphs) {
    if (current && current->wanted.add(glyphs)) current->stale = true;
}

bool ready() {
    return current && current->font;
}

std::string label(const char* icon, const char* text) {
    if (!ready()) return text;
    return std::string(icon) + "  " + text;
}

} // namespace viewer::ui::fonts
