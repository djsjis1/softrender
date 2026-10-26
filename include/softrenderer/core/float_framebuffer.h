#pragma once
#include <cassert>
#include <cstddef>
#include <algorithm>
#include <cmath>
#include <vector>
#include "softrenderer/core/color.h"
#include "softrenderer/core/depthbuffer.h"
#include "softrenderer/core/framebuffer.h"
#include "softrenderer/math/vec.h"

// ============================================================
// FloatFramebuffer：HDR 浮点帧缓冲。
//
// 普通 Framebuffer 每通道 8 位 [0,255]，无法存 >1 的亮度。
// PBR 光照 + 强光会产生远超 1 的值（高光、自发光），
// 需要浮点缓冲暂存，最后经色调映射压回 [0,1] 再显示。
//
// 同时自带深度缓冲，可直接用于主渲染 pass。
//
// 【命名空间】FloatFramebuffer 已迁入 namespace sr（ADR-001 第 2 批）。
// 引用方：post_effects.h、gbuffer.h、demo_stars.cpp、stage11_postprocess.cpp。
// ============================================================

namespace sr
{

class FloatFramebuffer
{
public:
    // 非法尺寸统一落到 0×0：与 resize() 同口径。旧实现直接把 int 负数转 size_t 再相乘，
    // 会在容器分配处抛 length_error，而 resize 只是忽略非法尺寸。
    FloatFramebuffer(int width, int height)
        : width_(width > 0 && height > 0 ? width : 0), height_(width > 0 && height > 0 ? height : 0),
          pixels_(size_t(width_) * size_t(height_), Vec3f{0, 0, 0}), depth_(width_, height_)
    {
    }

    int width() const { return width_; }
    int height() const { return height_; }

    // 重新分配尺寸（清空内容）
    void resize(int w, int h)
    {
        // 负尺寸会被 size_t 放大成巨值；且必须先构造新数据再改尺寸，
        // 否则 assign 抛 bad_alloc 时 width_/height_ 已改而像素还是旧的。
        if (w < 0 || h < 0)
            return;
        std::vector<Vec3f> pixels(size_t(w) * size_t(h), Vec3f{0, 0, 0});
        DepthBuffer depth(w, h);
        pixels_.swap(pixels);
        depth_ = std::move(depth);
        width_ = w;
        height_ = h;
    }

    void clear(Vec3f bg = {0, 0, 0})
    {
        for (auto &p : pixels_)
            p = bg;
        depth_.clear();
    }

    void setPixel(int x, int y, Vec3f hdrColor)
    {
        if (x < 0 || x >= width_ || y < 0 || y >= height_)
            return;
        pixels_[size_t(y) * width_ + x] = hdrColor;
    }

    // 读一个像素。
    // 【契约与 BasicFramebuffer::getPixel 同构】Debug 下 assert 暴露调用方越界，
    // Release 下返回黑色哨兵（而不是裸下标访问），避免越界读静默传播成 UB。
    // 旧实现连 assert 都没有，与同类的 setPixel（有运行时检查）口径不一致。
    Vec3f getPixel(int x, int y) const
    {
        assert(x >= 0 && x < width_ && y >= 0 && y < height_);
        if (x < 0 || x >= width_ || y < 0 || y >= height_)
            return Vec3f{0, 0, 0};
        return pixels_[size_t(y) * width_ + x];
    }

    DepthBuffer &depth() { return depth_; }
    const Vec3f *data() const { return pixels_.data(); }

    // 行指针：与 Framebuffer::row 同一口径的快速路径，供后处理等效果算子原地写入。
    // 调用方必须自行保证 0 <= y < height()（Release 下不做检查）。
    Vec3f *row(int y) { return pixels_.data() + size_t(y) * width_; }
    const Vec3f *row(int y) const { return pixels_.data() + size_t(y) * width_; }

private:
    int width_, height_;
    std::vector<Vec3f> pixels_;
    DepthBuffer depth_;
};

} // namespace sr
