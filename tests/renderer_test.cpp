#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <gdiplus.h>
#include "../src/renderer.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>

static void SavePng(const gifstudio::RenderedFrame& frame, const wchar_t* path) {
    Gdiplus::Bitmap bitmap(frame.width, frame.height, PixelFormat32bppARGB);
    Gdiplus::BitmapData data{};
    Gdiplus::Rect rect(0, 0, frame.width, frame.height);
    bitmap.LockBits(&rect, Gdiplus::ImageLockModeWrite, PixelFormat32bppARGB, &data);
    for (int y = 0; y < frame.height; ++y) {
        auto* row = static_cast<unsigned char*>(data.Scan0) + y * data.Stride;
        for (int x = 0; x < frame.width; ++x) {
            const auto* in = frame.rgba.data() + (y * frame.width + x) * 4;
            auto* out = row + x * 4;
            out[0] = in[2]; out[1] = in[1]; out[2] = in[0]; out[3] = in[3];
        }
    }
    bitmap.UnlockBits(&data);
    const CLSID png = {0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
    assert(bitmap.Save(path, &png) == Gdiplus::Ok);
}

int wmain() {
    Gdiplus::GdiplusStartupInput startup;
    ULONG_PTR token;
    assert(Gdiplus::GdiplusStartup(&token, &startup, nullptr) == Gdiplus::Ok);
    gifstudio::Settings s;
    auto layout = gifstudio::InspectLayout(s);
    assert(layout.effectiveFontSize == 78 && layout.fits);
    auto first = gifstudio::RenderAt(s, 0);
    assert(first.width == 480 && first.height == 206);
    assert(first.rgba.size() == 480 * 206 * 4);
    assert(first.rgba[3] == 0);
    assert(first.rgba[(103 * 480 + 240) * 4 + 3] == 255);
    assert(first.rgba == gifstudio::RenderAt(s, 1.5).rgba);
    assert(first.rgba == gifstudio::RenderAt(s, 7.8).rgba);
    assert(first.rgba != gifstudio::RenderAt(s, 2.3).rgba);
    assert(first.rgba != gifstudio::RenderAt(s, 3).rgba);
    SavePng(first, L"default.png");
    SavePng(gifstudio::RenderAt(s, 2.3), L"transition.png");
    s.texts = {L"专业门窗安装维修服务"};
    layout = gifstudio::InspectLayout(s);
    assert(layout.effectiveFontSize < 78 && layout.fits);
    assert(gifstudio::RenderAt(s, 0).rgba == gifstudio::RenderAt(s, 2.3).rgba);
    SavePng(gifstudio::RenderAt(s, 0), L"long-text.png");
    s.autoFit = false;
    assert(!gifstudio::InspectLayout(s).fits);
    s.fontFamily = L"A deliberately missing font 1.2.3";
    assert(!gifstudio::InspectLayout(s).warning.empty());
    s = gifstudio::Settings();
    s.transparent = false;
    assert(gifstudio::RenderAt(s, 0).rgba[3] == 255);
    s.transparent = true;
    constexpr int iterations = 60;
    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i) gifstudio::RenderAt(s, 2.0 + 0.59 * i / iterations);
    auto end = std::chrono::steady_clock::now();
    const double perFrame = std::chrono::duration<double, std::milli>(end - start).count() / iterations;
    std::printf("All renderer assertions passed; moving-frame average: %.2f ms\n", perFrame);
    Gdiplus::GdiplusShutdown(token);
    return 0;
}
