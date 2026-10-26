#pragma once
#include <cassert>
#include <cstddef>
#include <algorithm>
#include <cmath>
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
// 【裸机】外部存储构造（非拥有）：像素（与可选深度）放调用方指定的
// 内存，构造零分配、析构不释放。外部存储缓冲是**定尺寸**的：
// resize 到不同尺寸是空操作（自分配版本才允许重建）。
//
// 【命名空间】FloatFramebuffer 已迁入 namespace sr（ADR-001 第 2 批）。
// 引用方：post_effects.h、gbuffer.h、demo_stars.cpp、stage11_postprocess.cpp。
// ============================================================

namespace sr
{

class FloatFramebuffer
{
public:
    // 0×0 空缓冲（不分配）：供 BloomScratch 等聚合体默认构造、
    // 以及"先声明后 move 赋值"的用法。渲染前须 resize 或移动赋入。
    // ownsPixels_ 必须为 true：空缓冲属自分配形态，resize 才会走重建路径
    //（外部存储是定尺寸的，resize 只做同尺寸清空）；析构 delete[] nullptr 安全。
    FloatFramebuffer() : width_(0), height_(0), pixels_(nullptr), ownsPixels_(true), depth_(0, 0) {}

    // 非法尺寸统一落到 0×0：与 resize() 同口径。旧实现直接把 int 负数转 size_t 再相乘，
    // 会在容器分配处抛 length_error，而 resize 只是忽略非法尺寸。
    FloatFramebuffer(int width, int height)
        : width_(width > 0 && height > 0 ? width : 0), height_(width > 0 && height > 0 ? height : 0),
          pixels_(nullptr), ownsPixels_(true), depth_(width_, height_)
    {
        const size_t n = size_t(width_) * size_t(height_);
        pixels_ = new Vec3f[n];
        std::fill(pixels_, pixels_ + n, Vec3f{0, 0, 0});
    }

    // 外部存储（非拥有）：像素数组由调用方提供；深度仍为自分配。
    // pixels 须容纳 width*height 个 Vec3f（按 sizeof(Vec3f) 对齐），不做填充。
    FloatFramebuffer(int width, int height, Vec3f* pixels)
        : width_(width > 0 && height > 0 ? width : 0), height_(width > 0 && height > 0 ? height : 0),
          pixels_(pixels), ownsPixels_(false), depth_(width_, height_)
    {
        assert(width > 0 && height > 0);
        assert(pixels != nullptr);
    }

    // 外部存储（非拥有）完整版：像素与深度都由调用方提供，构造全程零分配。
    // depthStorage 须容纳 width*height 个 float。
    FloatFramebuffer(int width, int height, Vec3f* pixels, float* depthStorage)
        : width_(width > 0 && height > 0 ? width : 0), height_(width > 0 && height > 0 ? height : 0),
          pixels_(pixels), ownsPixels_(false), depth_(width_, height_, depthStorage)
    {
        assert(width > 0 && height > 0);
        assert(pixels != nullptr && depthStorage != nullptr);
    }

    ~FloatFramebuffer()
    {
        if (ownsPixels_)
            delete[] pixels_;
    }

    // 存储改裸指针后禁拷贝；移动转移指针与所属权（含外部存储形态）。
    FloatFramebuffer(const FloatFramebuffer&) = delete;
    FloatFramebuffer& operator=(const FloatFramebuffer&) = delete;
    FloatFramebuffer(FloatFramebuffer&& other) noexcept
        : width_(other.width_), height_(other.height_), pixels_(other.pixels_),
          ownsPixels_(other.ownsPixels_), depth_(std::move(other.depth_))
    {
        other.width_ = other.height_ = 0;
        other.pixels_ = nullptr;
        other.ownsPixels_ = false;
    }
    FloatFramebuffer& operator=(FloatFramebuffer&& other) noexcept
    {
        if (this != &other)
        {
            if (ownsPixels_)
                delete[] pixels_;
            width_ = other.width_;
            height_ = other.height_;
            pixels_ = other.pixels_;
            ownsPixels_ = other.ownsPixels_;
            depth_ = std::move(other.depth_);
            other.width_ = other.height_ = 0;
            other.pixels_ = nullptr;
            other.ownsPixels_ = false;
        }
        return *this;
    }

    int width() const { return width_; }
    int height() const { return height_; }

    // 重新分配尺寸（清空内容）。
    // 外部存储缓冲是定尺寸的：尺寸一致时只清空内容，不一致时空操作。
    void resize(int w, int h)
    {
        if (w < 0 || h < 0)
            return;
        if (!ownsPixels_)
        {
            if (w == width_ && h == height_)
                clear();
            return;
        }
        // 自分配路径：可抛的分配全部发生在局部对象上，最后才提交释放旧存储。
        DepthBuffer depth(w, h);
        Vec3f* pixels = new Vec3f[size_t(w) * size_t(h)];
        std::fill(pixels, pixels + size_t(w) * size_t(h), Vec3f{0, 0, 0});
        delete[] pixels_;
        pixels_ = pixels;
        depth_ = std::move(depth);
        width_ = w;
        height_ = h;
    }

    void clear(Vec3f bg = {0, 0, 0})
    {
        const size_t n = size_t(width_) * size_t(height_);
        for (size_t i = 0; i < n; ++i)
            pixels_[i] = bg;
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
    Vec3f getPixel(int x, int y) const
    {
        assert(x >= 0 && x < width_ && y >= 0 && y < height_);
        if (x < 0 || x >= width_ || y < 0 || y >= height_)
            return Vec3f{0, 0, 0};
        return pixels_[size_t(y) * width_ + x];
    }

    DepthBuffer &depth() { return depth_; }
    const DepthBuffer &depth() const { return depth_; }
    const Vec3f *data() const { return pixels_; }

    // 行指针：与 Framebuffer::row 同一口径的快速路径，供后处理等效果算子原地写入。
    // 调用方必须自行保证 0 <= y < height()（Release 下不做检查）。
    Vec3f *row(int y) { return pixels_ + size_t(y) * width_; }
    const Vec3f *row(int y) const { return pixels_ + size_t(y) * width_; }

private:
    int width_, height_;
    Vec3f* pixels_;
    // true = 构造时自行分配并在析构释放；false = 外部存储，析构不释放。
    bool ownsPixels_;
    DepthBuffer depth_;
};

}  // namespace sr
