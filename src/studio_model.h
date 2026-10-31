#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gifstudio {

struct Settings {
    std::vector<std::wstring> texts{L"门窗维修", L"防水补漏", L"翻新改造"};
    std::wstring fontFamily = L"Microsoft YaHei";
    int fontSize = 78;
    bool bold = false;
    bool autoFit = true;
    int width = 480;
    int height = 206;
    double holdSeconds = 2.0;
    double transitionSeconds = 0.6;
    std::uint32_t gradientStart = 0xFF4F20;
    std::uint32_t gradientEnd = 0xFF941F;
    std::uint32_t textColor = 0xFFFFFF;
    std::uint32_t borderColor = 0xFFF4E9;
    int borderWidth = 8;
    bool transparent = true;
    std::uint32_t backgroundColor = 0xFFFFFF;
};

} // namespace gifstudio
