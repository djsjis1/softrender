#pragma once
#include <array>
#include <utility>  // std::pair（kMSAA4Offsets 的元素类型）
#include <vector>

#include "core/color.h"
#include "core/depthbuffer.h"

// ============================================================
// MSAA 帧缓冲：每个像素存 N 个样本（颜色 + 深度）。
//
// 【抗锯齿原理】
// 锯齿的根源：一个像素要么"在三角形内"要么"不在"，二值判定
// 让斜边呈阶梯状。MSAA 的思路：每个像素内取 N 个子采样点，
// 各自独立做"在不在三角形内"的判定和深度测试，但最终颜色
// 只算一次（用像素中心的重心坐标），写入所有被覆盖的样本槽。
// 最后 resolve 时把 N 个样本取平均 → 边缘像素得到"半透明"
// 的混合色 → 锯齿消失。
//
// 与 SSAA（超采样）的区别：SSAA 对每个子样本都跑完整片元
// 着色器（N 倍开销）；MSAA 只跑 1 次片元着色，但做 N 次
// 覆盖判定 + 深度测试。开销远小于 SSAA，效果接近。
//
// 4x MSAA 的旋转网格采样点（像素中心为原点的偏移）：
//   (+3/8, +1/8), (+1/8, +3/8), (-1/8, -3/8), (-3/8, -1/8)
// 这是 GPU 硬件常用的模式，对角线边缘覆盖更均匀。
//
// 【命名空间】MSAAFramebuffer 与 kMSAA4Offsets 已迁入 namespace sr
// （ADR-001 第 2 批）。引用方：仅 stage9_msaa.cpp。
// ============================================================

namespace sr
{

template <int N>
class MSAAFramebuffer
{
   public:
    MSAAFramebuffer(int width, int height)
        : width_(width),
          height_(height),
          samples_(size_t(width) * height * N),
          depth_(size_t(width) * height * N, DepthBuffer::kFar)
    {
    }

    int width() const { return width_; }
    int height() const { return height_; }
    static constexpr int sampleCount() { return N; }

    void clear(Color bg)
    {
        for (auto& s : samples_)
            s = bg;
        for (auto& d : depth_)
            d = DepthBuffer::kFar;
    }

    // 对第 i 个样本做深度测试
    bool testDepth(int x, int y, int sampleIdx, float z)
    {
        if (x < 0 || x >= width_ || y < 0 || y >= height_)
            return false;
        float& slot = depth_[(size_t(y) * width_ + x) * N + sampleIdx];
        if (z < slot)
        {
            slot = z;
            return true;
        }
        return false;
    }

    // 写入第 i 个样本的颜色
    void setSample(int x, int y, int sampleIdx, Color c)
    {
        if (x < 0 || x >= width_ || y < 0 || y >= height_)
            return;
        samples_[(size_t(y) * width_ + x) * N + sampleIdx] = c;
    }

    // 统计某像素被覆盖的样本数
    int coverageCount(int x, int y) const
    {
        int count = 0;
        for (int i = 0; i < N; ++i)
            if (depth_[(size_t(y) * width_ + x) * N + i] < DepthBuffer::kFar)
                ++count;
        return count;
    }

    // Resolve：把 N 个样本平均成一个像素颜色，写入 dst。
    // 注意：必须对全部 N 个样本取平均（而不是只对"被覆盖"的样本），
    // 否则背景色会被丢弃，剪影边缘也不会与背景混合——那就退化成
    // 了普通光栅化。clear() 已把背景色写入所有样本槽，未覆盖的
    // 样本自然就是背景色，全样本平均即标准 MSAA resolve。
    void resolve(std::vector<Color>& dst) const
    {
        dst.resize(size_t(width_) * height_);
        for (int y = 0; y < height_; ++y)
        {
            for (int x = 0; x < width_; ++x)
            {
                int r = 0, g = 0, b = 0;
                for (int i = 0; i < N; ++i)
                {
                    const Color& c = samples_[(size_t(y) * width_ + x) * N + i];
                    r += c.r;
                    g += c.g;
                    b += c.b;
                }
                dst[size_t(y) * width_ + x] =
                    Color(uint8_t((r + N / 2) / N), uint8_t((g + N / 2) / N), uint8_t((b + N / 2) / N));
            }
        }
    }

   private:
    int width_, height_;
    std::vector<Color> samples_;
    std::vector<float> depth_;
};

// 4x MSAA 标准旋转网格采样偏移（像素坐标的分数偏移）
inline constexpr std::array<std::pair<float, float>, 4> kMSAA4Offsets = {
    {{0.375f, 0.125f}, {0.125f, 0.375f}, {-0.125f, -0.375f}, {-0.375f, -0.125f}}};

} // namespace sr
