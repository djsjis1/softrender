#pragma once
#include <algorithm>
#include <cassert>
#include <limits>
#include <vector>

// ============================================================
// DepthBuffer（Z-buffer）：与 Framebuffer 同尺寸的深度图，
// 每个像素记录"当前画上去的最近片元的深度"。
//
// 【为什么需要它】阶段 3 左半屏的穿插问题：三角形是按数组
// 顺序画的，后画的盖住先画的，与远近无关。深度缓冲让每个
// 像素只保留最近的片元，实现正确遮挡（画家算法的像素级版本）。
//
// 【深度约定】经 viewport 变换后 z ∈ [0,1]，0=近平面，1=远平面。
// 初始化为 +∞（这里用 1e9），片元深度更小才通过测试并覆盖。
// ============================================================
class DepthBuffer
{
   public:
    DepthBuffer(int width, int height) : width_(width), height_(height), depth_(size_t(width) * height, kFar) {}

    int width() const { return width_; }
    int height() const { return height_; }

    void clear(float v = kFar) { std::fill(depth_.begin(), depth_.end(), v); }

    // 深度测试 + 更新：若 z 更近则写入并返回 true（应绘制该像素）
    bool testAndSet(int x, int y, float z)
    {
        if (x < 0 || x >= width_ || y < 0 || y >= height_)
            return false;
        float& slot = depth_[size_t(y) * width_ + x];
        if (z < slot)
        {
            slot = z;
            return true;
        }
        return false;
    }

    // 整行原始指针：供光栅化内层循环免边界检查直访。
    //
    // 【调用契约】Release 下不做任何检查，调用方必须保证 0 <= y < height()，
    // 且横向访问限制在 [0, width()) 内。与 Framebuffer::row 同一契约，
    // 由 renderer.h 的包围盒 clamp 保证。
    float* row(int y)
    {
        assert(y >= 0 && y < height_);
        return depth_.data() + size_t(y) * width_;
    }

    // 读一个深度值。越界返回 kFar。
    //
    // 【为什么返回 kFar 而非 0】kFar 语义是「无穷远，任何东西都能画在它前面」，
    // 越界像素按「最远」处理不会错误地遮挡任何合法片元；返回 0 则会让越界
    // 位置表现得像「最近的表面」，反而掩盖问题。
    //
    // 【为什么原来连 assert 都没有】旧实现直接 depth_[y * width_ + x]，
    // 是三个访问器中唯一完全无保护的。testAndSet 有运行时检查、row 有 assert，
    // 而 get 什么都没有 —— 这种不对称最容易在 Review 中被漏掉。
    // 理由与 Framebuffer::getPixel 一致：assert 在 Release（NDEBUG）下失效，
    // 届时越界读是未定义行为。
    float get(int x, int y) const
    {
        assert(x >= 0 && x < width_ && y >= 0 && y < height_);
        if (x < 0 || x >= width_ || y < 0 || y >= height_)
            return kFar;
        return depth_[size_t(y) * width_ + x];
    }

    static constexpr float kFar = 1e9f;

   private:
    int width_, height_;
    std::vector<float> depth_;
};
