#pragma once

#include <vector>

namespace gifstudio {

struct GifFrame {
    int width = 0;
    int height = 0;
    int delayCs = 0;
    // Straight RGBA; pixels with alpha below 128 become fully transparent.
    std::vector<unsigned char> rgba;
};

struct GifOptions {
    // Total palette size, including reserved transparent index zero.
    int maxColors = 128;
};

// Produces an infinitely looping GIF89a with a shared palette. Adjacent
// identical indexed frames are coalesced while preserving total duration.
// A fixed transparency mask uses retained delta rectangles; changing masks
// use complete frames with restore-to-background disposal.
// Throws std::invalid_argument for malformed input; allocation failures
// propagate normally. No third-party libraries or platform APIs are required.
std::vector<unsigned char> EncodeGif(const std::vector<GifFrame>& frames,
                                     const GifOptions& options = {});

} // namespace gifstudio
