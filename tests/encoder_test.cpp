#include "../src/gif_encoder.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace gifstudio;
static unsigned rng = 123456789;
static unsigned nextRandom() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static GifFrame frame(int w, int h, int colors, bool transparent) {
    GifFrame result{w,h,7,{}};
    for (int i=0;i<w*h;++i) {
        unsigned n=nextRandom()%colors;
        result.rgba.push_back(static_cast<unsigned char>((n%6)*51));
        result.rgba.push_back(static_cast<unsigned char>(((n/6)%6)*51));
        result.rgba.push_back(static_cast<unsigned char>(((n/36)%6)*51));
        result.rgba.push_back(transparent && n==0 ? 0 : 255);
    }
    return result;
}
static void emit(std::ofstream& manifest, const std::string& name,
                 const std::vector<GifFrame>& frames, int palette) {
    auto bytes=EncodeGif(frames, GifOptions{palette});
    if (bytes!=EncodeGif(frames, GifOptions{palette})) throw std::runtime_error("non-deterministic output");
    std::ofstream output(name+".gif",std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    std::ofstream raw(name+".rgba",std::ios::binary);
    manifest<<name<<'\t'<<frames.front().width<<'\t'<<frames.front().height<<'\t'<<frames.size();
    for (const auto& f:frames) {
        raw.write(reinterpret_cast<const char*>(f.rgba.data()),f.rgba.size());
        manifest<<'\t'<<f.delayCs;
    }
    manifest<<'\n';
}
int main() {
    std::ofstream manifest("manifest.tsv");
    emit(manifest,"all_transparent",{GifFrame{2,2,20,std::vector<unsigned char>(16,0)}},32);
    emit(manifest,"single_pixel",{frame(1,1,1,false)},32);
    for (int colors:{2,3,7,15,31,63,127,216}) {
        int pal=colors<32?32:colors<64?64:colors<128?128:256;
        auto a=frame(257,149,colors,true), b=frame(257,149,colors,true);
        a.delayCs=200;b.delayCs=60;
        emit(manifest,"noise_"+std::to_string(colors),{a,a,b,a},pal);
    }
    for (int length:{2,3,4,5,6,7,8,9,10,11,12,15,16,17,20,31,32,33,40,63,64,65,100,255,256,257,511,512,513,1024,4096,8192,16000}) {
        emit(manifest,"boundary_"+std::to_string(length),{frame(length,1,2,false)},32);
    }
    auto a=frame(4,4,3,true); a.delayCs=70000;
    emit(manifest,"long_duration",{a,a},32);
    auto b=frame(10,5,7,false); b.delayCs=0;
    emit(manifest,"zero_delay",{b},32);
    auto c=frame(100,100,3,false);
    for(int y=0;y<100;++y)for(int x=0;x<100;++x)
        if(x<5||x>94||y<5||y>94)c.rgba[(y*100+x)*4+3]=0;
    auto d=c;
    for(int y=40;y<60;++y)for(int x=25;x<70;++x) {
        d.rgba[(y*100+x)*4]=255;d.rgba[(y*100+x)*4+1]=255;d.rgba[(y*100+x)*4+2]=255;
    }
    emit(manifest,"delta_transparent_fixed_mask",{c,d,c},32);
    auto e=frame(300,300,3,false);
    for (std::size_t i=0;i<e.rgba.size();i+=4) {
        auto n=nextRandom(); e.rgba[i]=n&255;e.rgba[i+1]=(n>>8)&255;e.rgba[i+2]=(n>>16)&255;
    }
    emit(manifest,"coarse_fallback",{e},128);
    int rejected=0;
    try {EncodeGif({});}catch(const std::invalid_argument&){++rejected;}
    try {EncodeGif({GifFrame{0,1,7,{}}});}catch(const std::invalid_argument&){++rejected;}
    try {EncodeGif({GifFrame{1,1,7,{}}});}catch(const std::invalid_argument&){++rejected;}
    try {auto x=frame(1,1,1,false);x.delayCs=-1;EncodeGif({x});}catch(const std::invalid_argument&){++rejected;}
    try {EncodeGif({a,b});}catch(const std::invalid_argument&){++rejected;}
    try {EncodeGif({b},GifOptions{10});}catch(const std::invalid_argument&){++rejected;}
    if(rejected!=6) return 2;
    std::cout<<"Encoder generated 47 cases and rejected all 6 malformed inputs.\n";
}
