#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
#include "core/color.h"
#include "core/depthbuffer.h"
#include "core/framebuffer.h"
#include "math/vec.h"

// ============================================================
// FloatFramebuffer：HDR 浮点帧缓冲。
//
// 普通 Framebuffer 每通道 8 位 [0,255]，无法存 >1 的亮度。
// PBR 光照 + 强光会产生远超 1 的值（高光、自发光），
// 需要浮点缓冲暂存，最后经色调映射压回 [0,1] 再显示。
//
// 同时自带深度缓冲，可直接用于主渲染 pass。
// ============================================================
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

    // ---- 后处理工具 ----

    // ACES 近似色调映射：把 HDR [0,∞) 压到 [0,1]，带肩部滚降
    static Vec3f acesToneMap(Vec3f x)
    {
        const float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
        auto f = [&](float v)
        {
            return std::clamp((v * (a * v + b)) / (v * (c * v + d) + e), 0.0f, 1.0f);
        };
        return {f(x.x), f(x.y), f(x.z)};
    }

    // Gamma 校正（线性 → sRGB 近似）
    static Vec3f gammaCorrect(Vec3f linear)
    {
        auto g = [](float v)
        { return std::pow(std::max(v, 0.0f), 1.0f / 2.2f); };
        return {g(linear.x), g(linear.y), g(linear.z)};
    }

    // 把 HDR 缓冲经色调映射 + gamma 转到 8 位 Framebuffer
    void resolveToLDR(Framebuffer &dst) const
    {
        for (int y = 0; y < height_; ++y)
            for (int x = 0; x < width_; ++x)
            {
                Vec3f hdr = pixels_[size_t(y) * width_ + x];
                Vec3f mapped = gammaCorrect(acesToneMap(hdr));
                dst.setPixel(x, y, Color::fromFloat(mapped.x, mapped.y, mapped.z));
            }
    }

    // 亮部提取：只保留亮度 > threshold 的像素（用于 Bloom）
    void brightPass(FloatFramebuffer &dst, float threshold) const
    {
        dst.clear();
        for (int y = 0; y < height_; ++y)
            for (int x = 0; x < width_; ++x)
            {
                Vec3f c = pixels_[size_t(y) * width_ + x];
                float lum = 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
                if (lum > threshold)
                    dst.pixels_[size_t(y) * width_ + x] = c;
            }
    }

    // 可分离高斯模糊（水平或垂直 pass）
    void gaussianBlur(FloatFramebuffer &dst, bool horizontal, int radius) const
    {
        dst.clear();
        // 预计算 1D 高斯核
        std::vector<float> kernel(radius * 2 + 1);
        float sigma = radius / 3.0f;
        float sum = 0;
        for (int i = -radius; i <= radius; ++i)
        {
            float w = std::exp(-(i * i) / (2 * sigma * sigma));
            kernel[i + radius] = w;
            sum += w;
        }
        for (auto &w : kernel)
            w /= sum;

        for (int y = 0; y < height_; ++y)
        {
            for (int x = 0; x < width_; ++x)
            {
                Vec3f acc{0, 0, 0};
                for (int i = -radius; i <= radius; ++i)
                {
                    int sx = horizontal ? std::clamp(x + i, 0, width_ - 1) : x;
                    int sy = horizontal ? y : std::clamp(y + i, 0, height_ - 1);
                    acc = acc + pixels_[size_t(sy) * width_ + sx] * kernel[i + radius];
                }
                dst.pixels_[size_t(y) * width_ + x] = acc;
            }
        }
    }

    // 加法合成：this += other * intensity
    void addBlend(const FloatFramebuffer &other, float intensity = 1.0f)
    {
        for (size_t i = 0; i < pixels_.size(); ++i)
            pixels_[i] = pixels_[i] + other.pixels_[i] * intensity;
    }

private:
    int width_, height_;
    std::vector<Vec3f> pixels_;
    DepthBuffer depth_;
};
