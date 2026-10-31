#pragma once

#include "studio_model.h"
#include <cstddef>

namespace gifstudio {

// Straight (not premultiplied) RGBA, left-to-right, top-to-bottom.
struct RenderedFrame {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> rgba;
};

struct LayoutInfo {
    int requestedFontSize = 0;
    int effectiveFontSize = 0;
    bool fits = true;
    std::wstring warning;
};

// GDI+ must have been initialized by the caller. These functions are reentrant;
// preview and background GIF export may run simultaneously on separate threads.
LayoutInfo InspectLayout(const Settings& settings);
RenderedFrame RenderAt(const Settings& settings, double elapsedSeconds);
RenderedFrame RenderPhrase(const Settings& settings, std::size_t phraseIndex);

} // namespace gifstudio
