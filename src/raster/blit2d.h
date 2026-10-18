#pragma once
#include <cmath>
#include <string>
#include <vector>

#include "core/blend.h"
#include "core/framebuffer.h"
#include "core/tga.h"
#include "math/mat3.h"
#include "math/rect.h"
#include "raster/shape2d.h"
#include "scene/texture.h"

// ============================================================
// 2D 图像合成（blit2d）——把“已有的像素块”贴到帧缓冲上。
//
// 与 shape2d 的区别：shape2d 画的是矢量图元（矩形/圆/多边形），
// 这里搬运的是位图（tga::Image / Texture / 另一个 Framebuffer /
// 字符模板精灵）。2D 游戏与 UI 的大量内容都是位图合成。
//
// 【核心机制】
//   - Alpha：源像素自带 a 通道，配合 BlendMode 做半透明叠加（blend.h）。
//   - tint：整体染色/调色（受击变红、变暗、闪烁），逐通道乘以 tint/255。
//   - 变换：缩放用最近邻/双线性重采样；旋转/任意仿射用“反向映射”——
//     遍历目标像素，用逆变换求它对应源图哪个位置再采样，避免空洞。
//
// 【坐标约定】tga::Image 与 Framebuffer 一致：行 0 在底部，y 向上。
// 目标位置 (dx,dy) 指贴图左下角落在帧缓冲的哪个像素。
// ============================================================

namespace draw2d
{

// 字符模板精灵的调色板：一个字符对应一种颜色（'.' 等透明字符跳过）
struct SpritePalette
{
    char ch;
    Color color;
};

// 逐通道染色：src * tint / 255（tint=白则不变）。alpha 也一并调制。
inline Color tintPixel(Color src, Color tint)
{
    if (tint.r == 255 && tint.g == 255 && tint.b == 255 && tint.a == 255)
        return src;
    return Color(uint8_t(src.r * tint.r / 255),
                 uint8_t(src.g * tint.g / 255),
                 uint8_t(src.b * tint.b / 255),
                 uint8_t(src.a * tint.a / 255));
}

// ------------------------------------------------------------
// 字符模板精灵：把 vector<string> 的“像素画”按整数倍放大绘制。
// sprite 第 0 行是图案顶部；画布 y 向上，故顶部行画在最高处（自动 y 翻转）。
// (ox,oy) 是精灵左下角。scale 为放大倍数（1=原始像素）。
// ------------------------------------------------------------
inline void drawCharSprite(Framebuffer& fb, const std::vector<std::string>& sprite,
                           const std::vector<SpritePalette>& palette,
                           int ox, int oy, int scale = 1,
                           Color tint = colors::white, char transparent = '.',
                           BlendMode mode = BlendMode::Opaque,
                           const ClipRect* clip = nullptr)
{
    if (sprite.empty() || scale <= 0)
        return;
    const int h = (int)sprite.size();
    for (int row = 0; row < h; ++row)
    {
        const std::string& line = sprite[row];
        const int w = (int)line.size();
        for (int col = 0; col < w; ++col)
        {
            char ch = line[col];
            if (ch == transparent)
                continue;
            // 查调色板；查不到用 fallback（品红）以便发现拼写错误
            Color c = colors::magenta;
            for (const auto& p : palette)
                if (p.ch == ch)
                {
                    c = p.color;
                    break;
                }
            c = tintPixel(c, tint);
            int px = ox + col * scale;
            int py = oy + (h - 1 - row) * scale; // y 翻转
            for (int dy = 0; dy < scale; ++dy)
                for (int dx = 0; dx < scale; ++dx)
                    plotBlend(fb, px + dx, py + dy, c, mode, clip);
        }
    }
}

// ------------------------------------------------------------
// 1:1 位图拷贝：把整张 image 贴到 (dx,dy)。
// ------------------------------------------------------------
inline void blitImage(Framebuffer& fb, const tga::Image& img, int dx, int dy,
                      Color tint = colors::white,
                      BlendMode mode = BlendMode::Opaque,
                      const ClipRect* clip = nullptr)
{
    if (!img.valid())
        return;
    for (int y = 0; y < img.height; ++y)
        for (int x = 0; x < img.width; ++x)
            plotBlend(fb, dx + x, dy + y, tintPixel(img.at(x, y), tint), mode, clip);
}

// ------------------------------------------------------------
// 子矩形拷贝（精灵图集/动画帧）：只贴 image 中 src 指定的那块。
// src 用图像空间坐标（y 向上，行 0 在底部）。
// ------------------------------------------------------------
inline void blitImageRect(Framebuffer& fb, const tga::Image& img, const Recti& src,
                          int dx, int dy, Color tint = colors::white,
                          BlendMode mode = BlendMode::Opaque,
                          const ClipRect* clip = nullptr)
{
    if (!img.valid())
        return;
    Recti region = src.intersection(Recti(0, 0, img.width, img.height)); // 夹到图像内
    for (int y = region.y; y < region.top(); ++y)
        for (int x = region.x; x < region.right(); ++x)
            plotBlend(fb, dx + (x - region.x), dy + (y - region.y),
                      tintPixel(img.at(x, y), tint), mode, clip);
}

// ------------------------------------------------------------
// 缩放拷贝：把 image 拉伸到 dw×dh（最近邻重采样，像素风锐利）。
// 需要平滑放大时可先对 Texture 用双线性（见 blitTexture）。
// ------------------------------------------------------------
inline void blitImageScaled(Framebuffer& fb, const tga::Image& img,
                            int dx, int dy, int dw, int dh,
                            Color tint = colors::white,
                            BlendMode mode = BlendMode::Opaque,
                            const ClipRect* clip = nullptr)
{
    if (!img.valid() || dw <= 0 || dh <= 0)
        return;
    for (int j = 0; j < dh; ++j)
    {
        // 目标像素中心映射回源图坐标（最近邻）
        int sy = std::min(img.height - 1, (int)((j + 0.5f) * img.height / dh));
        for (int i = 0; i < dw; ++i)
        {
            int sx = std::min(img.width - 1, (int)((i + 0.5f) * img.width / dw));
            plotBlend(fb, dx + i, dy + j, tintPixel(img.at(sx, sy), tint), mode, clip);
        }
    }
}

// ------------------------------------------------------------
// 仿射变换拷贝：xform 把“图像空间 [0,w]×[0,h]”映射到设备像素。
// 用反向映射（遍历目标包围盒 + 逆变换求源坐标）保证旋转/缩放不留空洞。
// bilinear=true 时对源图做双线性采样（缩小时更平滑），否则最近邻。
// ------------------------------------------------------------
inline void blitImageTransformed(Framebuffer& fb, const tga::Image& img, const Mat3& xform,
                                 Color tint = colors::white, bool bilinear = false,
                                 BlendMode mode = BlendMode::Opaque,
                                 const ClipRect* clip = nullptr)
{
    if (!img.valid() || std::abs(xform.determinant()) < 1e-12f)
        return;
    const float W = float(img.width), H = float(img.height);
    // 1. 变换四角求设备包围盒
    Vec2f c0 = xform.transformPoint({0.0f, 0.0f});
    Vec2f c1 = xform.transformPoint({W, 0.0f});
    Vec2f c2 = xform.transformPoint({W, H});
    Vec2f c3 = xform.transformPoint({0.0f, H});
    int minX = (int)std::floor(std::min({c0.x, c1.x, c2.x, c3.x}));
    int maxX = (int)std::ceil(std::max({c0.x, c1.x, c2.x, c3.x}));
    int minY = (int)std::floor(std::min({c0.y, c1.y, c2.y, c3.y}));
    int maxY = (int)std::ceil(std::max({c0.y, c1.y, c2.y, c3.y}));
    // 夹到帧缓冲与裁剪区，避免对屏外像素做无谓的逆变换采样
    minX = std::max(minX, 0);
    maxX = std::min(maxX, fb.width() - 1);
    minY = std::max(minY, 0);
    maxY = std::min(maxY, fb.height() - 1);
    if (clip)
    {
        minX = std::max(minX, clip->x0);
        maxX = std::min(maxX, clip->x1 - 1);
        minY = std::max(minY, clip->y0);
        maxY = std::min(maxY, clip->y1 - 1);
    }
    if (minX > maxX || minY > maxY)
        return;

    // 2. 逆变换：设备点 → 图像空间点
    Mat3 inv = xform.inversed();
    for (int py = minY; py <= maxY; ++py)
    {
        for (int px = minX; px <= maxX; ++px)
        {
            Vec2f ip = inv.transformPoint({px + 0.5f, py + 0.5f});
            if (ip.x < 0.0f || ip.x >= W || ip.y < 0.0f || ip.y >= H)
                continue; // 落在源图外
            Color src;
            if (bilinear)
            {
                float fx = ip.x - 0.5f, fy = ip.y - 0.5f;
                int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
                float tx = fx - x0, ty = fy - y0;
                auto clampX = [&](int v) { return std::clamp(v, 0, img.width - 1); };
                auto clampY = [&](int v) { return std::clamp(v, 0, img.height - 1); };
                Color a00 = img.at(clampX(x0), clampY(y0));
                Color a10 = img.at(clampX(x0 + 1), clampY(y0));
                Color a01 = img.at(clampX(x0), clampY(y0 + 1));
                Color a11 = img.at(clampX(x0 + 1), clampY(y0 + 1));
                src = Color::lerp(Color::lerp(a00, a10, tx), Color::lerp(a01, a11, tx), ty);
            }
            else
            {
                src = img.at((int)ip.x, (int)ip.y);
            }
            plotBlend(fb, px, py, tintPixel(src, tint), mode, clip);
        }
    }
}

// 便捷：绕精灵中心旋转 + 缩放后贴图（cx,cy 为设备空间中心）
inline void blitImageRotated(Framebuffer& fb, const tga::Image& img,
                             float cx, float cy, float scale, float angleRad,
                             Color tint = colors::white, bool bilinear = false,
                             BlendMode mode = BlendMode::Opaque,
                             const ClipRect* clip = nullptr)
{
    // 图像空间以中心为原点：先平移到中心，再缩放、旋转，最后落到 (cx,cy)
    Mat3 xform = Mat3::translate(cx, cy) * Mat3::rotate(angleRad) * Mat3::scale(scale) *
                 Mat3::translate(-img.width * 0.5f, -img.height * 0.5f);
    blitImageTransformed(fb, img, xform, tint, bilinear, mode, clip);
}

// ------------------------------------------------------------
// 图层合成：把另一个 Framebuffer 当作图层贴上来（半透明 UI/特效常用）
// ------------------------------------------------------------
inline void blitFramebuffer(Framebuffer& dst, const Framebuffer& src, int dx, int dy,
                            BlendMode mode = BlendMode::AlphaBlend,
                            const ClipRect* clip = nullptr)
{
    for (int y = 0; y < src.height(); ++y)
        for (int x = 0; x < src.width(); ++x)
            plotBlend(dst, dx + x, dy + y, src.getPixel(x, y), mode, clip);
}

// ------------------------------------------------------------
// 纹理四边形：用 Texture（带 mipmap / 双线性 / 三线性过滤）填满设备矩形。
// 相比 tga::Image 的最近邻缩放，放大更平滑、缩小无闪烁。
// (x,y) 为矩形左下角，w/h 为设备像素尺寸。
// ------------------------------------------------------------
inline void blitTexture(Framebuffer& fb, const Texture& tex, int x, int y, int w, int h,
                        Color tint = colors::white,
                        BlendMode mode = BlendMode::Opaque,
                        const ClipRect* clip = nullptr)
{
    if (!tex.valid() || w <= 0 || h <= 0)
        return;
    for (int j = 0; j < h; ++j)
    {
        float v = h > 1 ? float(j) / float(h - 1) : 0.0f;
        for (int i = 0; i < w; ++i)
        {
            float u = w > 1 ? float(i) / float(w - 1) : 0.0f;
            Vec3f c = tex.sample(u, v);
            Color col = Color::fromFloat(c.x, c.y, c.z);
            plotBlend(fb, x + i, y + j, tintPixel(col, tint), mode, clip);
        }
    }
}

} // namespace draw2d
