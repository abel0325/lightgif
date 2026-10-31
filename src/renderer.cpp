#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <gdiplus.h>

#include "renderer.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <utility>

#pragma comment(lib, "gdiplus.lib")

namespace gifstudio {
namespace {

using namespace Gdiplus;
constexpr int kSupersampling = 2;

Color Rgb(std::uint32_t rgb) {
    return Color(255, static_cast<BYTE>((rgb >> 16) & 255),
                 static_cast<BYTE>((rgb >> 8) & 255),
                 static_cast<BYTE>(rgb & 255));
}

struct Geometry {
    RectF outer;
    RectF inner;
    float availableWidth;
    float availableHeight;
};

Geometry GetGeometry(const Settings& s) {
    const float sx = static_cast<float>(s.width) / 480.0f;
    const float sy = static_cast<float>(s.height) / 206.0f;
    const RectF outer(7.0f * sx, 16.0f * sy, 458.0f * sx, 179.0f * sy);
    const float border = std::clamp(static_cast<float>(s.borderWidth) * std::min(sx, sy),
                                   0.0f, std::min(outer.Width, outer.Height) * 0.40f);
    const RectF inner(outer.X + border, outer.Y + border,
                      outer.Width - 2.0f * border, outer.Height - 2.0f * border);
    // Insets account for the curved ends, so automatic fitting doesn't merely
    // fit the rectangular bounds while clipping the outside glyphs.
    const float inset = std::min(inner.Height * 0.30f, inner.Width * 0.08f);
    return {outer, inner, std::max(1.0f, inner.Width - 2.0f * inset),
            std::max(1.0f, inner.Height * 0.72f)};
}

void RoundedPath(GraphicsPath& path, const RectF& r) {
    const float d = std::max(0.01f, std::min(r.Width, r.Height));
    path.AddArc(r.X, r.Y, d, d, 180.0f, 90.0f);
    path.AddArc(r.GetRight() - d, r.Y, d, d, 270.0f, 90.0f);
    path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0.0f, 90.0f);
    path.AddArc(r.X, r.GetBottom() - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

struct GlyphOutline {
    std::vector<PointF> points;
    std::vector<BYTE> types;
};

struct CachedLayout {
    Settings key;
    LayoutInfo info;
    bool ready = false;
    // Plain coordinates only: no native GDI+ objects survive this API call, so
    // the caller can shut GDI+ down normally without a renderer cleanup API.
    std::vector<GlyphOutline> paths;
    std::vector<RectF> bounds;
};

bool SameLayout(const Settings& a, const Settings& b) {
    return a.texts == b.texts && a.fontFamily == b.fontFamily &&
           a.fontSize == b.fontSize && a.bold == b.bold && a.autoFit == b.autoFit &&
           a.width == b.width && a.height == b.height && a.borderWidth == b.borderWidth;
}

void AddWarning(LayoutInfo& info, const std::wstring& text) {
    if (!info.warning.empty()) info.warning += L"；";
    info.warning += text;
}

CachedLayout& GetLayout(const Settings& s) {
    // Path construction is the expensive part of drawing CJK fonts. One cache
    // per thread avoids rebuilding outlines on every frame and has no shared
    // mutable state between the preview thread and the export thread.
    thread_local CachedLayout cache;
    if (cache.ready && SameLayout(cache.key, s)) return cache;
    cache.ready = false;
    cache.key = s;
    cache.paths.clear();
    cache.bounds.clear();
    cache.info = {};
    cache.info.requestedFontSize = s.fontSize;
    cache.info.effectiveFontSize = std::clamp(s.fontSize, 1, 4096);

    auto family = std::make_unique<FontFamily>(s.fontFamily.c_str());
    if (family->GetLastStatus() != Ok) {
        family = std::make_unique<FontFamily>(L"Microsoft YaHei");
        if (family->GetLastStatus() != Ok) family = std::make_unique<FontFamily>(L"SimHei");
        if (family->GetLastStatus() != Ok) family = std::make_unique<FontFamily>(L"Arial");
        AddWarning(cache.info, L"所选字体不可用，已使用系统替代字体");
    }

    INT style = s.bold ? FontStyleBold : FontStyleRegular;
    if (!family->IsStyleAvailable(style)) {
        style = family->IsStyleAvailable(FontStyleRegular) ? FontStyleRegular : FontStyleBold;
        if (s.bold) AddWarning(cache.info, L"所选字体没有粗体样式");
    }
    const float requestedSize = static_cast<float>(cache.info.effectiveFontSize);
    StringFormat format(StringFormat::GenericTypographic());
    format.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsNoClip |
                          StringFormatFlagsMeasureTrailingSpaces);
    float maxWidth = 0.0f;
    float maxHeight = 0.0f;
    std::vector<std::unique_ptr<GraphicsPath>> rawPaths;
    for (const auto& text : s.texts) {
        auto path = std::make_unique<GraphicsPath>();
        if (!text.empty()) {
            const Status status = path->AddString(text.c_str(), static_cast<INT>(text.size()),
                family.get(), style, requestedSize, PointF(0.0f, 0.0f), &format);
            if (status != Ok) throw std::runtime_error("Unable to render selected font.");
        }
        RectF bounds(0, 0, 0, 0);
        if (path->GetPointCount() > 0) path->GetBounds(&bounds);
        maxWidth = std::max(maxWidth, bounds.Width);
        maxHeight = std::max(maxHeight, bounds.Height);
        cache.bounds.push_back(bounds);
        rawPaths.push_back(std::move(path));
    }

    const Geometry geo = GetGeometry(s);
    const float fitRatio = std::min(1.0f, std::min(
        geo.availableWidth / std::max(1.0f, maxWidth),
        geo.availableHeight / std::max(1.0f, maxHeight)));
    if (s.autoFit && fitRatio < 1.0f) {
        cache.info.effectiveFontSize = std::max(1, static_cast<int>(std::floor(requestedSize * fitRatio)));
        const float scale = static_cast<float>(cache.info.effectiveFontSize) / requestedSize;
        Matrix transform;
        transform.Scale(scale, scale);
        for (std::size_t i = 0; i < rawPaths.size(); ++i) {
            rawPaths[i]->Transform(&transform);
            auto& bounds = cache.bounds[i];
            bounds.X *= scale;
            bounds.Y *= scale;
            bounds.Width *= scale;
            bounds.Height *= scale;
        }
        maxWidth *= scale;
        maxHeight *= scale;
    }
    cache.info.fits = maxWidth <= geo.availableWidth + 0.01f &&
                      maxHeight <= geo.availableHeight + 0.01f;
    if (!cache.info.fits) AddWarning(cache.info, L"文字超出安全范围，请减小字号或启用自动适配");
    if (s.texts.empty()) AddWarning(cache.info, L"请至少输入一条文案");
    for (const auto& path : rawPaths) {
        GlyphOutline outline;
        const INT count = path->GetPointCount();
        if (count > 0) {
            outline.points.resize(count);
            outline.types.resize(count);
            path->GetPathPoints(outline.points.data(), count);
            path->GetPathTypes(outline.types.data(), count);
        }
        cache.paths.push_back(std::move(outline));
    }
    cache.ready = true;
    return cache;
}

void ConfigureGraphics(Graphics& graphics) {
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(PixelOffsetModeHalf);
    graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    graphics.SetCompositingMode(CompositingModeSourceOver);
    graphics.ScaleTransform(static_cast<REAL>(kSupersampling), static_cast<REAL>(kSupersampling));
}

void DrawPhrase(Graphics& graphics, CachedLayout& layout, std::size_t index,
                const Geometry& geo, float verticalOffset, std::uint32_t color) {
    if (index >= layout.paths.size() || layout.paths[index].points.empty()) return;
    const auto& outline = layout.paths[index];
    GraphicsPath path(outline.points.data(), outline.types.data(),
                      static_cast<INT>(outline.points.size()), FillModeAlternate);
    const RectF& bounds = layout.bounds[index];
    const GraphicsState saved = graphics.Save();
    graphics.TranslateTransform(geo.inner.X + (geo.inner.Width - bounds.Width) / 2.0f - bounds.X,
        geo.inner.Y + (geo.inner.Height - bounds.Height) / 2.0f - bounds.Y + verticalOffset);
    SolidBrush brush(Rgb(color));
    graphics.FillPath(&brush, &path);
    graphics.Restore(saved);
}

void FadeTextEdges(Bitmap& textLayer, const Geometry& geo) {
    BitmapData pixels{};
    Rect lockRect(0, 0, textLayer.GetWidth(), textLayer.GetHeight());
    if (textLayer.LockBits(&lockRect, ImageLockModeRead | ImageLockModeWrite,
                          PixelFormat32bppPARGB, &pixels) != Ok) {
        throw std::runtime_error("Unable to access text pixels.");
    }
    const double top = geo.inner.Y;
    const double bottom = geo.inner.GetBottom();
    const double fadeHeight = std::max(1.0, static_cast<double>(geo.inner.Height) * 0.15);
    for (UINT y = 0; y < pixels.Height; ++y) {
        const double logicalY = (static_cast<double>(y) + 0.5) / kSupersampling;
        const double distance = std::min(logicalY - top, bottom - logicalY);
        const double fade = std::clamp(distance / fadeHeight, 0.0, 1.0);
        if (fade >= 1.0) continue;
        auto* row = static_cast<unsigned char*>(pixels.Scan0) + static_cast<std::ptrdiff_t>(y) * pixels.Stride;
        const unsigned multiplier = static_cast<unsigned>(std::lround(fade * 256.0));
        for (UINT x = 0; x < pixels.Width; ++x) {
            for (int channel = 0; channel < 4; ++channel) {
                row[x * 4 + channel] = static_cast<unsigned char>((row[x * 4 + channel] * multiplier + 128) >> 8);
            }
        }
    }
    textLayer.UnlockBits(&pixels);
}

RenderedFrame ToStraightRgba(Bitmap& source, int width, int height) {
    RenderedFrame frame{width, height, std::vector<unsigned char>(static_cast<std::size_t>(width) * height * 4)};
    BitmapData pixels{};
    Rect lockRect(0, 0, source.GetWidth(), source.GetHeight());
    if (source.LockBits(&lockRect, ImageLockModeRead, PixelFormat32bppPARGB, &pixels) != Ok) {
        throw std::runtime_error("Unable to read rendered pixels.");
    }
    constexpr unsigned sampleCount = kSupersampling * kSupersampling;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            unsigned channels[4]{};
            for (int dy = 0; dy < kSupersampling; ++dy) {
                const auto* row = static_cast<const unsigned char*>(pixels.Scan0) +
                    static_cast<std::ptrdiff_t>(y * kSupersampling + dy) * pixels.Stride;
                for (int dx = 0; dx < kSupersampling; ++dx) {
                    const auto* pixel = row + (x * kSupersampling + dx) * 4;
                    for (int c = 0; c < 4; ++c) channels[c] += pixel[c];
                }
            }
            auto* output = frame.rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4;
            output[3] = static_cast<unsigned char>((channels[3] + sampleCount / 2) / sampleCount);
            if (channels[3] == 0) {
                output[0] = output[1] = output[2] = 0;
            } else {
                for (int c = 0; c < 3; ++c) {
                    output[2 - c] = static_cast<unsigned char>(std::min(255u,
                        (channels[c] * 255u + channels[3] / 2) / channels[3]));
                }
            }
        }
    }
    source.UnlockBits(&pixels);
    return frame;
}

RenderedFrame DrawFrame(const Settings& s, std::size_t index, double progress, bool moving) {
    if (s.width < 1 || s.height < 1 || s.width > 8192 || s.height > 8192 ||
        static_cast<std::uint64_t>(s.width) * s.height > 16777216ULL) {
        throw std::invalid_argument("Image dimensions are out of range.");
    }
    auto& layout = GetLayout(s);
    const Geometry geo = GetGeometry(s);
    Bitmap output(s.width * kSupersampling, s.height * kSupersampling, PixelFormat32bppPARGB);
    if (output.GetLastStatus() != Ok) throw std::runtime_error("Unable to allocate image.");
    {
        Graphics graphics(&output);
        graphics.Clear(s.transparent ? Color(0, 0, 0, 0) : Rgb(s.backgroundColor));
        ConfigureGraphics(graphics);
        GraphicsPath outer;
        RoundedPath(outer, geo.outer);
        SolidBrush border(Rgb(s.borderColor));
        graphics.FillPath(&border, &outer);
        GraphicsPath inner;
        RoundedPath(inner, geo.inner);
        LinearGradientBrush gradient(PointF(geo.inner.X, geo.inner.Y),
                                     PointF(geo.inner.GetRight(), geo.inner.Y),
                                     Rgb(s.gradientStart), Rgb(s.gradientEnd));
        gradient.SetWrapMode(WrapModeTileFlipXY);
        graphics.FillPath(&gradient, &inner);
        if (!moving) {
            graphics.SetClip(&inner);
            DrawPhrase(graphics, layout, index, geo, 0.0f, s.textColor);
        }
    }

    if (moving && !layout.paths.empty()) {
        Bitmap textLayer(s.width * kSupersampling, s.height * kSupersampling, PixelFormat32bppPARGB);
        if (textLayer.GetLastStatus() != Ok) throw std::runtime_error("Unable to allocate text layer.");
        {
            Graphics textGraphics(&textLayer);
            textGraphics.Clear(Color(0, 0, 0, 0));
            ConfigureGraphics(textGraphics);
            GraphicsPath clip;
            RoundedPath(clip, geo.inner);
            textGraphics.SetClip(&clip);
            const std::size_t nextIndex = (index + 1) % layout.paths.size();
            const float maxHeight = std::max(layout.bounds[index].Height, layout.bounds[nextIndex].Height);
            const float travel = (geo.inner.Height + maxHeight) * 0.5f + 2.0f;
            const float p = static_cast<float>(progress * progress * (3.0 - 2.0 * progress));
            DrawPhrase(textGraphics, layout, index, geo, -p * travel, s.textColor);
            DrawPhrase(textGraphics, layout, nextIndex, geo, (1.0f - p) * travel, s.textColor);
        }
        FadeTextEdges(textLayer, geo);
        Graphics composite(&output);
        composite.SetCompositingMode(CompositingModeSourceOver);
        composite.DrawImage(&textLayer, 0, 0, textLayer.GetWidth(), textLayer.GetHeight());
    }
    return ToStraightRgba(output, s.width, s.height);
}

} // namespace

LayoutInfo InspectLayout(const Settings& settings) {
    return GetLayout(settings).info;
}

RenderedFrame RenderAt(const Settings& settings, double elapsedSeconds) {
    const std::size_t count = settings.texts.size();
    if (count < 2) return DrawFrame(settings, 0, 0.0, false);
    const double hold = std::isfinite(settings.holdSeconds) ? std::max(0.0, settings.holdSeconds) : 2.0;
    const double transition = std::isfinite(settings.transitionSeconds) ? std::max(0.0, settings.transitionSeconds) : 0.6;
    const double phraseDuration = hold + transition;
    if (phraseDuration <= 0.0) return DrawFrame(settings, 0, 0.0, false);
    const double elapsed = std::isfinite(elapsedSeconds) ? std::max(0.0, elapsedSeconds) : 0.0;
    const double inCycle = std::fmod(elapsed, phraseDuration * static_cast<double>(count));
    const std::size_t index = std::min(count - 1, static_cast<std::size_t>(inCycle / phraseDuration));
    const double inPhrase = inCycle - static_cast<double>(index) * phraseDuration;
    if (transition <= 0.0 || inPhrase < hold) return DrawFrame(settings, index, 0.0, false);
    return DrawFrame(settings, index, std::clamp((inPhrase - hold) / transition, 0.0, 1.0), true);
}

RenderedFrame RenderPhrase(const Settings& settings, std::size_t phraseIndex) {
    const std::size_t index = settings.texts.empty() ? 0 : std::min(phraseIndex, settings.texts.size() - 1);
    return DrawFrame(settings, index, 0.0, false);
}

} // namespace gifstudio
