#pragma once
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>

#include "softrenderer/core/blend.h"
#include "softrenderer/core/framebuffer.h"
#include "softrenderer/core/image.h"
#include "softrenderer/math/mat3.h"
#include "softrenderer/math/rect.h"
#include "softrenderer/render/draw2d/shape2d.h"
#include "softrenderer/resource/texture.h"

// ============================================================
// 2D 图像合成（blit2d）——把“已有的像素块”贴到帧缓冲上。
//
// 与 shape2d 的区别：shape2d 画的是矢量图元（矩形/圆/多边形），
// 这里搬运的是位图（sr::Image / Texture / 另一个 Framebuffer /
// 字符模板精灵）。2D 游戏与 UI 的大量内容都是位图合成。
//
// 【核心机制】
//   - Alpha：源像素自带 a 通道，配合 BlendMode 做半透明叠加（blend.h）。
//   - tint：整体染色/调色（受击变红、变暗、闪烁），逐通道乘以 tint/255。
//   - 变换：缩放用最近邻/双线性重采样；旋转/任意仿射用“反向映射”——
//     遍历目标像素，用逆变换求它对应源图哪个位置再采样，避免空洞。
//
// 【坐标约定】sr::Image 与 Framebuffer 一致：行 0 在底部，y 向上。
// 目标位置 (dx,dy) 指贴图左下角落在帧缓冲的哪个像素。
// ============================================================

namespace sr::draw2d
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
template <class FB>
void drawCharSprite(FB& fb, const std::vector<std::string>& sprite,
                           const std::vector<SpritePalette>& palette,
                           int ox, int oy, int scale = 1,
                           Color tint = sr::colors::white, char transparent = '.',
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
            Color c = sr::colors::magenta;
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
// 目标矩形与帧缓冲、裁剪区的交集（半开区间 [x0,x1) × [y0,y1)）。
//
// 返回 false 表示完全不可见。贴图前必须先算它：超屏矩形如果只靠
// plotBlend 的边界检查丢弃，每个屏外像素仍要付出采样 + 函数调用的代价
// （实测 4000×4000 的目标矩形 94% 的调用落在屏外，单次 blit 耗时 3.5 s）。
// ------------------------------------------------------------
template <class FB>
bool clipTargetRect(const FB& fb, const ClipRect* clip,
                           long long x, long long y, long long w, long long h,
                           int& x0, int& y0, int& x1, int& y1)
{
    if (fb.width() <= 0 || fb.height() <= 0 || w <= 0 || h <= 0)
        return false;
    long long left = std::max<long long>(x, 0);
    long long bottom = std::max<long long>(y, 0);
    long long right = std::min<long long>(x + w, fb.width());
    long long top = std::min<long long>(y + h, fb.height());
    if (clip)
    {
        left = std::max<long long>(left, clip->x0);
        bottom = std::max<long long>(bottom, clip->y0);
        right = std::min<long long>(right, clip->x1);
        top = std::min<long long>(top, clip->y1);
    }
    if (left >= right || bottom >= top)
        return false;
    x0 = int(left);
    y0 = int(bottom);
    x1 = int(right);
    y1 = int(top);
    return true;
}

// ------------------------------------------------------------
// 1:1 位图拷贝：把整张 image 贴到 (dx,dy)。
// ------------------------------------------------------------
template <class FB>
void blitImage(FB& fb, const sr::Image& img, int dx, int dy,
                      Color tint = sr::colors::white,
                      BlendMode mode = BlendMode::Opaque,
                      const ClipRect* clip = nullptr)
{
    if (!img.valid())
        return;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    if (!clipTargetRect(fb, clip, dx, dy, img.width, img.height, x0, y0, x1, y1))
        return;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            plotBlend(fb, x, y, tintPixel(img.at(x - dx, y - dy), tint), mode, clip);
}

// ------------------------------------------------------------
// 子矩形拷贝（精灵图集/动画帧）：只贴 image 中 src 指定的那块。
// src 用图像空间坐标（y 向上，行 0 在底部）。
// ------------------------------------------------------------
template <class FB>
void blitImageRect(FB& fb, const sr::Image& img, const Recti& src,
                          int dx, int dy, Color tint = sr::colors::white,
                          BlendMode mode = BlendMode::Opaque,
                          const ClipRect* clip = nullptr)
{
    if (!img.valid())
        return;
    Recti region = src.intersection(Recti(0, 0, img.width, img.height)); // 夹到图像内
    if (region.isEmpty())
        return;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    if (!clipTargetRect(fb, clip, dx, dy, region.w, region.h, x0, y0, x1, y1))
        return;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            plotBlend(fb, x, y, tintPixel(img.at(region.x + (x - dx), region.y + (y - dy)), tint), mode, clip);
}

// ------------------------------------------------------------
// 缩放拷贝：把 image 拉伸到 dw×dh（最近邻重采样，像素风锐利）。
// 需要平滑放大时可先对 Texture 用双线性（见 blitTexture）。
// ------------------------------------------------------------
template <class FB>
void blitImageScaled(FB& fb, const sr::Image& img,
                            int dx, int dy, int dw, int dh,
                            Color tint = sr::colors::white,
                            BlendMode mode = BlendMode::Opaque,
                            const ClipRect* clip = nullptr)
{
    if (!img.valid() || dw <= 0 || dh <= 0)
        return;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    if (!clipTargetRect(fb, clip, dx, dy, dw, dh, x0, y0, x1, y1))
        return;
    for (int j = y0; j < y1; ++j)
    {
        // 目标像素中心映射回源图坐标（最近邻）
        const int sy = std::min(img.height - 1, int((float(j - dy) + 0.5f) * float(img.height) / float(dh)));
        for (int i = x0; i < x1; ++i)
        {
            const int sx = std::min(img.width - 1, int((float(i - dx) + 0.5f) * float(img.width) / float(dw)));
            plotBlend(fb, i, j, tintPixel(img.at(sx, sy), tint), mode, clip);
        }
    }
}

// ------------------------------------------------------------
// 仿射变换拷贝：xform 把“图像空间 [0,w]×[0,h]”映射到设备像素。
// 用反向映射（遍历目标包围盒 + 逆变换求源坐标）保证旋转/缩放不留空洞。
// bilinear=true 时对源图做双线性采样（缩小时更平滑），否则最近邻。
// ------------------------------------------------------------
template <class FB>
void blitImageTransformed(FB& fb, const sr::Image& img, const Mat3& xform,
                                 Color tint = sr::colors::white, bool bilinear = false,
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
template <class FB>
void blitImageRotated(FB& fb, const sr::Image& img,
                             float cx, float cy, float scale, float angleRad,
                             Color tint = sr::colors::white, bool bilinear = false,
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
template <class DstFB, class SrcFB>
void blitFramebuffer(DstFB& dst, const SrcFB& src, int dx, int dy,
                            BlendMode mode = BlendMode::AlphaBlend,
                            const ClipRect* clip = nullptr)
{
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    if (!clipTargetRect(dst, clip, dx, dy, src.width(), src.height(), x0, y0, x1, y1))
        return;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            plotBlend(dst, x, y, src.getPixel(x - dx, y - dy), mode, clip);
}

// ------------------------------------------------------------
// 纹理四边形：用 Texture（带 mipmap / 双线性 / 三线性过滤）填满设备矩形。
// 相比 sr::Image 的最近邻缩放，放大更平滑、缩小无闪烁。
// (x,y) 为矩形左下角，w/h 为设备像素尺寸。
// ------------------------------------------------------------
template <class FB>
void blitTexture(FB& fb, const Texture& tex, int x, int y, int w, int h,
                        Color tint = sr::colors::white,
                        BlendMode mode = BlendMode::Opaque,
                        const ClipRect* clip = nullptr)
{
    if (!tex.valid() || w <= 0 || h <= 0)
        return;
    // 先把目标矩形夹到屏幕/裁剪区内：屏外像素不必采样。
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    if (!clipTargetRect(fb, clip, x, y, w, h, x0, y0, x1, y1))
        return;
    // 采样点取像素中心：用 i/(w-1) 会把首尾像素挤到纹理边界，
    // 配合 Repeat 寻址在 u=1 处绕回开头。同时传导数才能让三线性选对 mip。
    const float du = 1.0f / float(w), dv = 1.0f / float(h);
    // 轴对齐矩形的屏幕空间导数是常量 → mip 层只需算一次。
    // 逐像素重算一次 sqrt+log2 是整张 blit 的主要开销（实测占一半以上）。
    const auto filter = tex.getFilter();
    const bool useLod = filter == Texture::Filter::Trilinear;
    const float lod = useLod ? tex.lodForPixel(du, 0.0f, 0.0f, dv) : 0.0f;
    for (int py = y0; py < y1; ++py)
    {
        const float v = (float(py - y) + 0.5f) * dv;
        for (int px = x0; px < x1; ++px)
        {
            const float u = (float(px - x) + 0.5f) * du;
            // 2D 合成用 ClampToEdge：Repeat 在放大小图时会把对边颜色带进来，
            // 图集边缘整列串色。带 alpha 的入口才能保留纹理透明度。
            const Color col = filter == Texture::Filter::Nearest ? tex.sampleNearestClamped(u, v)
                              : useLod ? tex.sampleColorClamped(u, v, lod)
                                       : tex.sampleColorClamped(u, v, 0.0f);
            plotBlend(fb, px, py, tintPixel(col, tint), mode, clip);
        }
    }
}

// ------------------------------------------------------------
// 纹理四边形（任意仿射）：xform 把图空间 [0,1]×[0,1] 映射到设备像素。
//
// blitTexture 只支持轴对齐矩形；旋转/斜切下必须有这条路径，
// 否则上层只能“静默不画”（Renderer2D::texture 曾如此）。
// 用逆变换反向映射，保证旋转后不留空洞。
// ------------------------------------------------------------
template <class FB>
void blitTextureTransformed(FB& fb, const Texture& tex, const Mat3& xform,
                                   Color tint = sr::colors::white, bool bilinear = true,
                                   BlendMode mode = BlendMode::Opaque,
                                   const ClipRect* clip = nullptr)
{
    if (!tex.valid() || !std::isfinite(xform.determinant()) || std::abs(xform.determinant()) < 1e-12f)
        return;
    // 图空间四角 → 设备包围盒
    const Vec2f c0 = xform.transformPoint({0.0f, 0.0f});
    const Vec2f c1 = xform.transformPoint({1.0f, 0.0f});
    const Vec2f c2 = xform.transformPoint({1.0f, 1.0f});
    const Vec2f c3 = xform.transformPoint({0.0f, 1.0f});
    if (!std::isfinite(c0.x) || !std::isfinite(c0.y) || !std::isfinite(c1.x) || !std::isfinite(c1.y) ||
        !std::isfinite(c2.x) || !std::isfinite(c2.y) || !std::isfinite(c3.x) || !std::isfinite(c3.y))
        return;

    const float minXf = std::min({c0.x, c1.x, c2.x, c3.x});
    const float maxXf = std::max({c0.x, c1.x, c2.x, c3.x});
    const float minYf = std::min({c0.y, c1.y, c2.y, c3.y});
    const float maxYf = std::max({c0.y, c1.y, c2.y, c3.y});
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    if (!clipTargetRect(fb, clip, (long long)std::floor(minXf), (long long)std::floor(minYf),
                        (long long)std::ceil(maxXf) - (long long)std::floor(minXf),
                        (long long)std::ceil(maxYf) - (long long)std::floor(minYf), x0, y0, x1, y1))
        return;

    const Mat3 inv = xform.inversed();
    // 屏幕 1 像素对应的 UV 步长（估计），让三线性能选对 mip
    const float deviceW = std::max(1.0f, std::max(std::abs(c1.x - c0.x), std::abs(c1.y - c0.y)));
    const float deviceH = std::max(1.0f, std::max(std::abs(c3.x - c0.x), std::abs(c3.y - c0.y)));
    const float du = 1.0f / deviceW, dv = 1.0f / deviceH;
    const auto filter = tex.getFilter();
    const bool useLod = filter == Texture::Filter::Trilinear;
    const float lod = useLod ? tex.lodForPixel(du, 0.0f, 0.0f, dv) : 0.0f;

    for (int py = y0; py < y1; ++py)
    {
        for (int px = x0; px < x1; ++px)
        {
            const Vec2f ip = inv.transformPoint({float(px) + 0.5f, float(py) + 0.5f});
            // 只画图空间内部：越界不允许 Repeat 绕回，否则旋转四边形会把对边贴过来
            if (!(ip.x >= 0.0f && ip.x < 1.0f && ip.y >= 0.0f && ip.y < 1.0f))
                continue;
            // bilinear=false 仍走双线性但固定第 0 层（与旧行为一致）；
            // 所有路径都用 ClampToEdge，避免旋转后边缘串色。
            Color col;
            if (filter == Texture::Filter::Nearest)
                col = tex.sampleNearestClamped(ip.x, ip.y);
            else
                col = tex.sampleColorClamped(ip.x, ip.y, (bilinear && useLod) ? lod : 0.0f);
            plotBlend(fb, px, py, tintPixel(col, tint), mode, clip);
        }
    }
}

} // namespace sr::draw2d
