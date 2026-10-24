#pragma once
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
    FloatFramebuffer(int width, int height)
        : width_(width), height_(height),
          pixels_(size_t(width) * height, Vec3f{0, 0, 0}),
          depth_(width, height) {}

    int width() const { return width_; }
    int height() const { return height_; }

    // 重新分配尺寸（清空内容）
    void resize(int w, int h)
    {
        width_ = w;
        height_ = h;
        pixels_.assign(size_t(w) * h, Vec3f{0, 0, 0});
        depth_ = DepthBuffer(w, h);
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

    Vec3f getPixel(int x, int y) const
    {
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
