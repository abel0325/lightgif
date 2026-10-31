#include "gif_encoder.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace gifstudio {
namespace {

using Byte = unsigned char;
using Bytes = std::vector<Byte>;

struct Bin {
    std::uint64_t count = 0;
    std::array<std::uint64_t, 3> sum{};
    std::array<double, 3> rgb{};
};

struct Color {
    Byte r = 0, g = 0, b = 0;
};

struct Box {
    std::vector<int> bins;
    double error = 0;
    int axis = 0;
};

constexpr std::array<double, 3> kChannelWeight{{3.0, 4.0, 2.0}};

unsigned BinIndex(const Byte* rgba) {
    return ((static_cast<unsigned>(rgba[0]) >> 3) << 10) |
           ((static_cast<unsigned>(rgba[1]) >> 3) << 5) |
           (static_cast<unsigned>(rgba[2]) >> 3);
}

unsigned RgbKey(const Byte* rgba) {
    return (static_cast<unsigned>(rgba[0]) << 16) |
           (static_cast<unsigned>(rgba[1]) << 8) | rgba[2];
}

struct PaletteLookup {
    std::vector<Byte> coarse;
    std::unordered_map<unsigned, Byte> exact;
    Byte Find(const Byte* rgba) const {
        return coarse.empty() ? exact.at(RgbKey(rgba)) : coarse[BinIndex(rgba)];
    }
};

void Measure(Box& box, const std::vector<Bin>& histogram) {
    box.error = 0;
    box.axis = 0;
    if (box.bins.size() < 2) return;
    double weight = 0;
    std::array<double, 3> mean{};
    for (int index : box.bins) {
        const Bin& bin = histogram[index];
        weight += static_cast<double>(bin.count);
        for (int c = 0; c < 3; ++c) mean[c] += static_cast<double>(bin.sum[c]);
    }
    for (double& value : mean) value /= weight;
    std::array<double, 3> variance{};
    for (int index : box.bins) {
        const Bin& bin = histogram[index];
        for (int c = 0; c < 3; ++c) {
            const double difference = bin.rgb[c] - mean[c];
            variance[c] += difference * difference * static_cast<double>(bin.count);
        }
    }
    for (int c = 0; c < 3; ++c) {
        variance[c] *= kChannelWeight[c];
        box.error += variance[c];
        if (variance[c] > variance[box.axis]) box.axis = c;
    }
}

std::vector<Color> BuildPalette(const std::vector<GifFrame>& frames, int maximum,
                                PaletteLookup& lookup) {
    // Exact histogram preserves smooth gradients. Limit unique colors so a
    // noisy photographic input cannot cause unbounded dictionary growth.
    constexpr std::size_t maximumExactColors = 65536;
    std::vector<Bin> histogram;
    std::vector<unsigned> colorKeys;
    std::unordered_map<unsigned, std::size_t> colorBins;
    colorBins.reserve(16384);
    bool coarse = false;
    for (const auto& frame : frames) {
        for (std::size_t i = 0; i < frame.rgba.size(); i += 4) {
            const Byte* pixel = frame.rgba.data() + i;
            if (pixel[3] < 128) continue;
            const unsigned key = RgbKey(pixel);
            auto found = colorBins.find(key);
            if (found == colorBins.end()) {
                if (histogram.size() == maximumExactColors) {
                    coarse = true;
                    break;
                }
                const std::size_t index = histogram.size();
                found = colorBins.emplace(key, index).first;
                histogram.push_back({});
                colorKeys.push_back(key);
            }
            Bin& bin = histogram[found->second];
            ++bin.count;
            for (int c = 0; c < 3; ++c) bin.sum[c] += pixel[c];
        }
        if (coarse) break;
    }
    if (coarse) {
        histogram.assign(32768, {});
        colorBins.clear();
        colorKeys.clear();
        for (const auto& frame : frames) {
            for (std::size_t i = 0; i < frame.rgba.size(); i += 4) {
                const Byte* pixel = frame.rgba.data() + i;
                if (pixel[3] < 128) continue;
                Bin& bin = histogram[BinIndex(pixel)];
                ++bin.count;
                for (int c = 0; c < 3; ++c) bin.sum[c] += pixel[c];
            }
        }
    }
    Box initial;
    for (int i = 0; i < static_cast<int>(histogram.size()); ++i) {
        Bin& bin = histogram[i];
        if (!bin.count) continue;
        for (int c = 0; c < 3; ++c)
            bin.rgb[c] = static_cast<double>(bin.sum[c]) / static_cast<double>(bin.count);
        initial.bins.push_back(i);
    }
    Measure(initial, histogram);
    std::vector<Box> boxes;
    if (!initial.bins.empty()) boxes.push_back(std::move(initial));
    while (boxes.size() < static_cast<std::size_t>(maximum - 1)) {
        std::size_t chosen = boxes.size();
        double greatestError = -1;
        for (std::size_t i = 0; i < boxes.size(); ++i) {
            if (boxes[i].bins.size() >= 2 && boxes[i].error > greatestError) {
                chosen = i;
                greatestError = boxes[i].error;
            }
        }
        if (chosen == boxes.size()) break;
        Box& box = boxes[chosen];
        const int axis = box.axis;
        std::sort(box.bins.begin(), box.bins.end(), [&](int lhs, int rhs) {
            const double a = histogram[lhs].rgb[axis];
            const double b = histogram[rhs].rgb[axis];
            return a == b ? lhs < rhs : a < b;
        });
        std::uint64_t weight = 0;
        for (int index : box.bins) weight += histogram[index].count;
        std::uint64_t cumulative = 0;
        std::size_t cut = 1;
        for (std::size_t i = 0; i + 1 < box.bins.size(); ++i) {
            cumulative += histogram[box.bins[i]].count;
            cut = i + 1;
            if (cumulative >= (weight + 1) / 2) break;
        }
        Box right;
        right.bins.assign(box.bins.begin() + cut, box.bins.end());
        box.bins.erase(box.bins.begin() + cut, box.bins.end());
        Measure(box, histogram);
        Measure(right, histogram);
        boxes.push_back(std::move(right));
    }

    // Entry zero is always transparent, even when all frames are opaque.
    std::vector<Color> palette(1);
    for (const Box& box : boxes) {
        std::uint64_t count = 0;
        std::array<std::uint64_t, 3> sums{};
        for (int index : box.bins) {
            count += histogram[index].count;
            for (int c = 0; c < 3; ++c) sums[c] += histogram[index].sum[c];
        }
        palette.push_back({static_cast<Byte>((sums[0] + count / 2) / count),
                           static_cast<Byte>((sums[1] + count / 2) / count),
                           static_cast<Byte>((sums[2] + count / 2) / count)});
    }
    if (palette.size() == 1) palette.push_back({0, 0, 0});
    if (coarse) lookup.coarse.assign(32768, 0);
    else lookup.exact.reserve(histogram.size());
    for (int i = 0; i < static_cast<int>(histogram.size()); ++i) {
        if (!histogram[i].count) continue;
        const auto& rgb = histogram[i].rgb;
        double bestError = std::numeric_limits<double>::max();
        Byte best = 1;
        for (std::size_t j = 1; j < palette.size(); ++j) {
            const Color& color = palette[j];
            const double dr = rgb[0] - color.r;
            const double dg = rgb[1] - color.g;
            const double db = rgb[2] - color.b;
            const double error = 3 * dr * dr + 4 * dg * dg + 2 * db * db;
            if (error < bestError) {
                bestError = error;
                best = static_cast<Byte>(j);
            }
        }
        if (coarse) lookup.coarse[i] = best;
        else lookup.exact.emplace(colorKeys[i], best);
    }
    return palette;
}

void U16(Bytes& output, unsigned value) {
    output.push_back(static_cast<Byte>(value & 255));
    output.push_back(static_cast<Byte>((value >> 8) & 255));
}

class BitWriter {
public:
    void Write(unsigned code, int bits) {
        pending_ |= code << bitCount_;
        bitCount_ += bits;
        while (bitCount_ >= 8) {
            bytes_.push_back(static_cast<Byte>(pending_ & 255));
            pending_ >>= 8;
            bitCount_ -= 8;
        }
    }
    Bytes Finish() {
        if (bitCount_) bytes_.push_back(static_cast<Byte>(pending_ & 255));
        return std::move(bytes_);
    }
private:
    Bytes bytes_;
    unsigned pending_ = 0;
    int bitCount_ = 0;
};

Bytes CompressLzw(const Bytes& indices, int minimumCodeSize) {
    const int clearCode = 1 << minimumCodeSize;
    const int endCode = clearCode + 1;
    int nextCode = endCode + 1;
    int codeBits = minimumCodeSize + 1;
    // A fixed open-addressed dictionary avoids per-entry allocations.
    std::array<int, 8192> keys;
    std::array<std::uint16_t, 8192> values{};
    keys.fill(-1);
    BitWriter writer;
    writer.Write(clearCode, codeBits);
    unsigned prefix = indices.front();
    for (std::size_t i = 1; i < indices.size(); ++i) {
        const unsigned suffix = indices[i];
        const int key = static_cast<int>((prefix << 8) | suffix);
        unsigned slot = (static_cast<std::uint32_t>(key) * 2654435761U) >> 19;
        while (keys[slot] != -1 && keys[slot] != key) slot = (slot + 1) & 8191;
        if (keys[slot] == key) {
            prefix = values[slot];
            continue;
        }
        writer.Write(prefix, codeBits);
        // The decoder creates its next dictionary entry after reading this
        // code. Grow only now, one emission after the encoder filled it.
        if (nextCode == (1 << codeBits) && codeBits < 12) ++codeBits;
        if (nextCode < 4096) {
            keys[slot] = key;
            values[slot] = static_cast<std::uint16_t>(nextCode++);
        } else {
            writer.Write(clearCode, codeBits);
            keys.fill(-1);
            nextCode = endCode + 1;
            codeBits = minimumCodeSize + 1;
        }
        prefix = suffix;
    }
    writer.Write(prefix, codeBits);
    if (nextCode == (1 << codeBits) && codeBits < 12) ++codeBits;
    writer.Write(endCode, codeBits);
    return writer.Finish();
}

void EmitFrame(Bytes& output, const Bytes& indices, int left, int top, int width,
               int height, unsigned delay, int minimumCodeSize, bool retain) {
    output.insert(output.end(), {0x21, 0xF9, 0x04,
                                static_cast<Byte>(retain ? 0x05 : 0x09)});
    U16(output, delay);
    output.insert(output.end(), {0x00, 0x00, 0x2C});
    U16(output, static_cast<unsigned>(left));
    U16(output, static_cast<unsigned>(top));
    U16(output, static_cast<unsigned>(width));
    U16(output, static_cast<unsigned>(height));
    output.push_back(0x00); // Shared global palette, no interlace.
    output.push_back(static_cast<Byte>(minimumCodeSize));
    const Bytes compressed = CompressLzw(indices, minimumCodeSize);
    for (std::size_t i = 0; i < compressed.size();) {
        const std::size_t count = (std::min)(std::size_t(255), compressed.size() - i);
        output.push_back(static_cast<Byte>(count));
        output.insert(output.end(), compressed.begin() + i, compressed.begin() + i + count);
        i += count;
    }
    output.push_back(0x00);
}

} // namespace

std::vector<unsigned char> EncodeGif(const std::vector<GifFrame>& frames,
                                     const GifOptions& options) {
    if (frames.empty()) throw std::invalid_argument("GIF needs at least one frame");
    if (options.maxColors != 32 && options.maxColors != 64 &&
        options.maxColors != 128 && options.maxColors != 256)
        throw std::invalid_argument("GIF palette size must be 32, 64, 128, or 256");
    const int width = frames.front().width;
    const int height = frames.front().height;
    if (width <= 0 || height <= 0 || width > 65535 || height > 65535)
        throw std::invalid_argument("GIF dimensions must be between 1 and 65535");
    const std::uint64_t pixels64 = static_cast<std::uint64_t>(width) * height;
    if (pixels64 > (std::numeric_limits<std::size_t>::max)() / 4)
        throw std::invalid_argument("GIF dimensions are too large");
    const std::size_t pixels = static_cast<std::size_t>(pixels64);
    bool fixedAlphaMask = true;
    for (const auto& frame : frames) {
        if (frame.width != width || frame.height != height || frame.rgba.size() != pixels * 4)
            throw std::invalid_argument("GIF frames must have matching dimensions and exact RGBA buffers");
        if (frame.delayCs < 0) throw std::invalid_argument("GIF frame delay cannot be negative");
        if (fixedAlphaMask && &frame != &frames.front()) {
            for (std::size_t p = 3; p < frame.rgba.size(); p += 4) {
                if ((frame.rgba[p] < 128) != (frames.front().rgba[p] < 128)) {
                    fixedAlphaMask = false;
                    break;
                }
            }
        }
    }

    PaletteLookup lookup;
    std::vector<Color> palette = BuildPalette(frames, options.maxColors, lookup);
    int paletteBits = 1;
    while ((std::size_t(1) << paletteBits) < palette.size()) ++paletteBits;
    palette.resize(std::size_t(1) << paletteBits);
    const int minimumCodeSize = (std::max)(2, paletteBits);

    Bytes output{'G', 'I', 'F', '8', '9', 'a'};
    U16(output, static_cast<unsigned>(width));
    U16(output, static_cast<unsigned>(height));
    output.push_back(static_cast<Byte>(0xF0 | (paletteBits - 1)));
    output.insert(output.end(), {0x00, 0x00}); // Background and aspect ratio.
    for (const auto& color : palette) {
        output.push_back(color.r);
        output.push_back(color.g);
        output.push_back(color.b);
    }
    output.insert(output.end(), {0x21, 0xFF, 0x0B, 'N', 'E', 'T', 'S', 'C', 'A',
                                 'P', 'E', '2', '.', '0', 0x03, 0x01, 0x00, 0x00, 0x00});

    Bytes pending;
    Bytes previous;
    std::uint64_t pendingDelay = 0;
    auto flush = [&]() {
        if (pending.empty()) return;
        int left = 0, top = 0, right = width, bottom = height;
        Bytes rectangle;
        if (fixedAlphaMask && !previous.empty()) {
            left = width;
            top = height;
            right = bottom = 0;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const std::size_t p = static_cast<std::size_t>(y) * width + x;
                    if (pending[p] == previous[p]) continue;
                    left = (std::min)(left, x);
                    top = (std::min)(top, y);
                    right = (std::max)(right, x + 1);
                    bottom = (std::max)(bottom, y + 1);
                }
            }
            if (right <= left || bottom <= top) {
                left = top = 0;
                right = bottom = 1;
            }
            rectangle.reserve(static_cast<std::size_t>(right - left) * (bottom - top));
            for (int y = top; y < bottom; ++y) {
                for (int x = left; x < right; ++x) {
                    const std::size_t p = static_cast<std::size_t>(y) * width + x;
                    // Transparent unchanged pixels retain the previous image;
                    // changed pixels carry real colors, including the backdrop
                    // needed to erase the old glyphs without leaving trails.
                    rectangle.push_back(pending[p] == previous[p] ? 0 : pending[p]);
                }
            }
        }
        const Bytes& image = rectangle.empty() ? pending : rectangle;
        do {
            const unsigned portion = static_cast<unsigned>((std::min)(pendingDelay, std::uint64_t(65535)));
            // With a fixed alpha mask every changed pixel is opaque, so a
            // retained delta rectangle can erase old text using real backdrop
            // colors. A changing mask needs full-frame background disposal.
            EmitFrame(output, image, left, top, right - left, bottom - top,
                      portion, minimumCodeSize, fixedAlphaMask);
            pendingDelay -= portion;
        } while (pendingDelay);
        if (fixedAlphaMask) previous = pending;
    };
    for (const auto& frame : frames) {
        Bytes indexed(pixels);
        for (std::size_t p = 0; p < pixels; ++p) {
            const Byte* rgba = frame.rgba.data() + p * 4;
            indexed[p] = rgba[3] < 128 ? 0 : lookup.Find(rgba);
        }
        if (pending == indexed) {
            pendingDelay += static_cast<unsigned>(frame.delayCs);
        } else {
            flush();
            pending = std::move(indexed);
            pendingDelay = static_cast<unsigned>(frame.delayCs);
        }
    }
    flush();
    output.push_back(0x3B);
    return output;
}

} // namespace gifstudio
