// Adapted from Pathing's Utility/ColorThief/, which adapts ColorThief
// (https://github.com/KSemenenko/ColorThief/tree/e3d52d1d84944437696d89945ec61bf1b102da2f):
//
//  The MIT License (MIT)
//
//  Copyright (c) 2015 Kos
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in all
//  copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//  SOFTWARE.

#include "viewer/render/trail_colour.h"

#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <optional>

namespace viewer::trail_colour {

namespace {

// Mmcq's: five bits of each channel, a histogram over the 32,768 boxes they make.
constexpr int SIGBITS = 5;
constexpr int RSHIFT = 8 - SIGBITS;
constexpr int MULT = 1 << RSHIFT;
constexpr int HISTO_SIZE = 1 << (3 * SIGBITS);
constexpr int VBOX_LENGTH = 1 << SIGBITS;
constexpr int MAX_ITERATIONS = 1000;

int colourIndex(int r, int g, int b) {
    return (r << (2 * SIGBITS)) + (g << SIGBITS) + b;
}

// .NET's Convert.ToInt32 of a double: to the nearest, a half to the even one.
int roundHalfEven(double value) {
    const double floor = std::floor(value);
    const double fraction = value - floor;
    if (fraction > 0.5) return static_cast<int>(floor) + 1;
    if (fraction < 0.5) return static_cast<int>(floor);
    return static_cast<int>(std::fmod(floor, 2.0) == 0.0 ? floor : floor + 1.0);
}

// A box of the histogram, its bounds inclusive. A cut can leave one with its low bound past its
// high one, which holds nothing.
struct Box {
    int r1 = 0, r2 = 0, g1 = 0, g2 = 0, b1 = 0, b2 = 0;
    // VBox caches both on first use; nothing changes a box's bounds after that, so the cache
    // only saves the walk.
    mutable std::optional<int> count_cache;

    int count(const std::vector<int>& histo) const {
        if (!count_cache) {
            int pixels = 0;
            for (int i = r1; i <= r2; ++i) {
                for (int j = g1; j <= g2; ++j) {
                    for (int k = b1; k <= b2; ++k) pixels += histo[colourIndex(i, j, k)];
                }
            }
            count_cache = pixels;
        }
        return *count_cache;
    }

    int volume() const { return (r2 - r1 + 1) * (g2 - g1 + 1) * (b2 - b1 + 1); }

    Box clone() const { return Box{r1, r2, g1, g2, b1, b2, std::nullopt}; }

    // VBox.Avg: the middle of each histogram cell weighted by its pixels, or the box's own middle
    // when it holds none.
    std::array<int, 3> average(const std::vector<int>& histo) const {
        long long total = 0;
        long long r_sum = 0, g_sum = 0, b_sum = 0;
        for (int i = r1; i <= r2; ++i) {
            for (int j = g1; j <= g2; ++j) {
                for (int k = b1; k <= b2; ++k) {
                    const long long value = histo[colourIndex(i, j, k)];
                    total += value;
                    // hval * (i + 0.5) * Mult, a whole number for any Mult that is even.
                    r_sum += value * (2 * i + 1) * MULT / 2;
                    g_sum += value * (2 * j + 1) * MULT / 2;
                    b_sum += value * (2 * k + 1) * MULT / 2;
                }
            }
        }
        if (total > 0) {
            return {
                static_cast<int>(std::llabs(r_sum / total)),
                static_cast<int>(std::llabs(g_sum / total)),
                static_cast<int>(std::llabs(b_sum / total))};
        }
        return {std::abs(MULT * (r1 + r2 + 1) / 2), std::abs(MULT * (g1 + g2 + 1) / 2), std::abs(MULT * (b1 + b2 + 1) / 2)};
    }
};

// List<T>.Sort as .NET Framework sorts a list this short (ArraySortHelper.IntroSort): two or
// three by fixed swaps, up to sixteen by insertion. The swaps are not stable, so which of two
// equal boxes comes last, and is cut next, is theirs. The lists sorted here are never longer.
template <typename Compare>
void sortAsDotNet(std::vector<Box>* boxes, Compare compare) {
    std::vector<Box>& keys = *boxes;
    const int lo = 0;
    const int hi = static_cast<int>(keys.size()) - 1;
    const auto swapIfGreater = [&](int a, int b) {
        if (a != b && compare(keys[a], keys[b]) > 0) std::swap(keys[a], keys[b]);
    };
    const int size = hi - lo + 1;
    if (size < 2) return;
    if (size == 2) {
        swapIfGreater(lo, hi);
    } else if (size == 3) {
        swapIfGreater(lo, hi - 1);
        swapIfGreater(lo, hi);
        swapIfGreater(hi - 1, hi);
    } else {
        for (int i = lo; i < hi; ++i) {
            int j = i;
            Box moving = keys[i + 1];
            while (j >= lo && compare(moving, keys[j]) < 0) {
                keys[j + 1] = keys[j];
                --j;
            }
            keys[j + 1] = moving;
        }
    }
}

// Mmcq.DoCut and MedianCutApply: the box split across its widest channel where its pixels pass
// half. Nothing when it holds none; itself alone when it holds one pixel. Empty, with `failed`
// set, where Pathing's would throw — an index past the histogram — which Pathing catches as a
// texture that failed to load.
struct Cut {
    std::optional<Box> first;
    std::optional<Box> second;
};

Cut medianCut(const std::vector<int>& histo, const Box& box, bool* failed) {
    const int count = box.count(histo);
    if (count == 0) return {};
    if (count == 1) return {box.clone(), std::nullopt};

    const int rw = box.r2 - box.r1 + 1;
    const int gw = box.g2 - box.g1 + 1;
    const int bw = box.b2 - box.b1 + 1;
    const int widest = std::max(std::max(rw, gw), bw);
    // Which channel, and the box's bounds on it: red first, then green, on a tie.
    const char channel = widest == rw ? 'r' : widest == gw ? 'g' : 'b';
    int Box::* low = channel == 'r' ? &Box::r1 : channel == 'g' ? &Box::g1 : &Box::b1;
    int Box::* high = channel == 'r' ? &Box::r2 : channel == 'g' ? &Box::g2 : &Box::b2;

    // The pixels up to each slice along that channel, and those after it; -1 outside the box.
    std::array<int, VBOX_LENGTH> partial;
    std::array<int, VBOX_LENGTH> lookahead;
    partial.fill(-1);
    lookahead.fill(-1);
    int total = 0;
    for (int i = box.*low; i <= box.*high; ++i) {
        int sum = 0;
        if (channel == 'r') {
            for (int j = box.g1; j <= box.g2; ++j) {
                for (int k = box.b1; k <= box.b2; ++k) sum += histo[colourIndex(i, j, k)];
            }
        } else if (channel == 'g') {
            for (int j = box.r1; j <= box.r2; ++j) {
                for (int k = box.b1; k <= box.b2; ++k) sum += histo[colourIndex(j, i, k)];
            }
        } else {
            for (int j = box.r1; j <= box.r2; ++j) {
                for (int k = box.g1; k <= box.g2; ++k) sum += histo[colourIndex(j, k, i)];
            }
        }
        total += sum;
        partial[static_cast<size_t>(i)] = total;
    }
    for (int i = 0; i < VBOX_LENGTH; ++i) {
        if (partial[static_cast<size_t>(i)] != -1) lookahead[static_cast<size_t>(i)] = total - partial[static_cast<size_t>(i)];
    }

    const int dim1 = box.*low;
    const int dim2 = box.*high;
    const auto at = [&](const std::array<int, VBOX_LENGTH>& sums, int index, bool* ok) {
        if (index < 0 || index >= VBOX_LENGTH) {
            *ok = false;
            return 0;
        }
        return sums[static_cast<size_t>(index)];
    };
    for (int i = dim1; i <= dim2; ++i) {
        if (partial[static_cast<size_t>(i)] <= total / 2) continue;
        const int left = i - dim1;
        const int right = dim2 - i;
        int d2 = left <= right ? std::min(dim2 - 1, std::abs(i + right / 2)) : std::max(dim1, std::abs(roundHalfEven(i - 1 - left / 2.0)));

        // Away from slices with no pixels, which can walk off the histogram.
        bool ok = true;
        while (ok && (d2 < 0 || at(partial, d2, &ok) <= 0)) ++d2;
        int count2 = at(lookahead, d2, &ok);
        while (ok && count2 == 0 && d2 > 0 && at(partial, d2 - 1, &ok) > 0) count2 = at(lookahead, --d2, &ok);
        if (!ok) {
            *failed = true;
            return {};
        }

        Box first = box.clone();
        Box second = box.clone();
        first.*high = d2;
        second.*low = d2 + 1;
        return {first, second};
    }
    // "VBox can't be cut": unreachable while the box holds a pixel, as the last slice holds them all.
    *failed = true;
    return {};
}

// Mmcq.Iter: cuts the last box of the sorted list until there are `target` colours, or a
// thousand passes.
template <typename Compare>
void cutUntil(std::vector<Box>* boxes, Compare compare, int target, const std::vector<int>& histo, bool* failed) {
    int colours = 1;
    int iterations = 0;
    while (iterations < MAX_ITERATIONS) {
        const Box last = boxes->back();
        if (last.count(histo) == 0) {
            sortAsDotNet(boxes, compare);
            ++iterations;
            continue;
        }
        boxes->pop_back();

        const Cut cut = medianCut(histo, last, failed);
        if (*failed || !cut.first) {
            *failed = true; // "vbox1 not defined"
            return;
        }
        boxes->push_back(*cut.first);
        if (cut.second) {
            boxes->push_back(*cut.second);
            ++colours;
        }
        sortAsDotNet(boxes, compare);

        if (colours >= target) return;
        if (iterations++ > MAX_ITERATIONS) return;
    }
}

int compareInts(int a, int b) {
    return a < b ? -1 : (a > b ? 1 : 0);
}

} // namespace

ImU32 dominant(const uint8_t* rgba, int width, int height) {
    // ColorThief.GetPalette's defaults: five colours asked for, every tenth pixel, white left out.
    constexpr int COLOUR_COUNT = 5;
    constexpr int QUALITY = 10;
    // Mostly opaque and not white, as ConvertPixels keeps them.
    constexpr int LEAST_ALPHA = 125;
    constexpr int WHITE_ABOVE = 250;

    // Blish HUD's TextureUtil.FromStreamPremultiplied: each channel times alpha in floats,
    // truncated, which is what ColorThief reads back from the texture.
    const auto premultiply = [](uint8_t colour, uint8_t alpha) {
        const float fc = colour / 255.0f;
        const float fa = alpha / 255.0f;
        return static_cast<uint8_t>(std::clamp(static_cast<int>(255.0f * fc * fa), 0, 255));
    };

    std::vector<std::array<uint8_t, 3>> pixels;
    const long long pixel_count = static_cast<long long>(width) * height;
    for (long long i = 0; i < pixel_count; i += QUALITY) {
        const uint8_t* p = rgba + i * 4;
        const uint8_t a = p[3];
        const uint8_t r = premultiply(p[0], a);
        const uint8_t g = premultiply(p[1], a);
        const uint8_t b = premultiply(p[2], a);
        if (a >= LEAST_ALPHA && !(r > WHITE_ABOVE && g > WHITE_ABOVE && b > WHITE_ABOVE)) pixels.push_back({r, g, b});
    }
    if (pixels.empty()) return NO_COLOUR;

    // Mmcq.Quantize, for one colour fewer than asked: GetColorMap's --colorCount.
    const int max_colours = COLOUR_COUNT - 1;
    std::vector<int> histo(HISTO_SIZE, 0);
    for (const auto& pixel : pixels) histo[static_cast<size_t>(colourIndex(pixel[0] >> RSHIFT, pixel[1] >> RSHIFT, pixel[2] >> RSHIFT))]++;

    // VboxFromPixels, with its `else if`: the first pixel sets each minimum and never a maximum,
    // so it is inside the first box only if a later pixel reaches it on every channel.
    int r_min = 1000000, r_max = 0, g_min = 1000000, g_max = 0, b_min = 1000000, b_max = 0;
    for (const auto& pixel : pixels) {
        const int r = pixel[0] >> RSHIFT;
        const int g = pixel[1] >> RSHIFT;
        const int b = pixel[2] >> RSHIFT;
        if (r < r_min) {
            r_min = r;
        } else if (r > r_max) {
            r_max = r;
        }
        if (g < g_min) {
            g_min = g;
        } else if (g > g_max) {
            g_max = g;
        }
        if (b < b_min) {
            b_min = b;
        } else if (b > b_max) {
            b_max = b;
        }
    }
    std::vector<Box> boxes{Box{r_min, r_max, g_min, g_max, b_min, b_max, std::nullopt}};

    // By population to three quarters of the colours, then by population times volume.
    const auto byCount = [&histo](const Box& a, const Box& b) { return compareInts(a.count(histo), b.count(histo)); };
    // VBoxComparer multiplies in a C# int, unchecked: a large texture's product wraps, and is
    // compared wrapped.
    const auto wrapped = [](int a, int b) { return static_cast<int32_t>(static_cast<uint32_t>(static_cast<int64_t>(a) * b)); };
    const auto byProduct = [&histo, &wrapped](const Box& a, const Box& b) {
        return compareInts(wrapped(a.count(histo), a.volume()), wrapped(b.count(histo), b.volume()));
    };
    bool failed = false;
    cutUntil(&boxes, byCount, static_cast<int>(std::ceil(0.75 * max_colours)), histo, &failed);
    if (failed) return NO_COLOUR;
    sortAsDotNet(&boxes, byProduct);
    cutUntil(&boxes, byProduct, max_colours - static_cast<int>(boxes.size()), histo, &failed);
    if (failed) return NO_COLOUR;
    std::reverse(boxes.begin(), boxes.end());

    // CMap.GeneratePalette, sorted by population with the most first, as SampleColor sorts it.
    std::vector<Box> palette = boxes;
    sortAsDotNet(&palette, [&histo](const Box& a, const Box& b) { return compareInts(b.count(histo), a.count(histo)); });
    const std::array<int, 3> rgb = palette.front().average(histo);
    // CMap.FromRgb's (byte) casts.
    return IM_COL32(static_cast<uint8_t>(rgb[0]), static_cast<uint8_t>(rgb[1]), static_cast<uint8_t>(rgb[2]), 255);
}

ImU32 sample(const std::vector<uint8_t>& file) {
    if (file.empty()) return NO_COLOUR;
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(file.data(), static_cast<int>(file.size()), &width, &height, &channels, 4);
    if (!pixels) return NO_COLOUR;
    const ImU32 colour = dominant(pixels, width, height);
    stbi_image_free(pixels);
    return colour;
}

} // namespace viewer::trail_colour
