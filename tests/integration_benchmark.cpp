#define NOMINMAX
#include <windows.h>
#include <gdiplus.h>
#include "../src/gif_encoder.h"
#include "../src/renderer.h"
#include <chrono>
#include <fstream>
#include <iostream>

using namespace gifstudio;
int main() {
    ULONG_PTR token;
    Gdiplus::GdiplusStartupInput gdiplus;
    if (Gdiplus::GdiplusStartup(&token,&gdiplus,nullptr)!=Gdiplus::Ok) return 2;
    Settings settings;
    std::vector<GifFrame> frames;
    std::ofstream raw("integration.rgba",std::ios::binary);
    for (int text=0;text<3;++text) {
        auto f=RenderPhrase(settings,text);
        frames.push_back({f.width,f.height,200,std::move(f.rgba)});
        for(int j=0;j<15;++j) {
            f=RenderAt(settings,text*2.6+2.0+(j+0.5)*0.04);
            frames.push_back({f.width,f.height,4,std::move(f.rgba)});
        }
    }
    for (const auto& f:frames) raw.write(reinterpret_cast<const char*>(f.rgba.data()),f.rgba.size());
    raw.close();
    for (int colors:{256,128,64,32}) {
        auto start=std::chrono::steady_clock::now();
        auto bytes=EncodeGif(frames,GifOptions{colors});
        double secs=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        std::ofstream gif("integration_"+std::to_string(colors)+".gif",std::ios::binary);
        gif.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
        std::cout<<colors<<" colors: "<<bytes.size()<<" bytes, "<<secs<<" sec, source "<<frames.size()<<" frames\n";
    }
    Gdiplus::GdiplusShutdown(token);
}
