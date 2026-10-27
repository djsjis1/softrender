#pragma once
#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <limits>
#include <utility>  // std::pair（kMSAA4Offsets 的元素类型）
#include <vector>

#include "softrenderer/core/color.h"
#include "softrenderer/core/depthbuffer.h"

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
// （ADR-001 第 2 批）。引用方：examples/stage11_msaa.cpp 与
// tests/unit/coverage_math_test.cpp。
//
// 【非法尺寸】统一落到 0×0（与 DepthBuffer 同口径，见 depthbuffer.h），构造
// **不抛异常**：本头属于 sr_freestanding_headers_check 的裸机子集，以
// -fno-exceptions -fno-rtti 编译，公共头里任何 throw 都会破坏裸机可用性。
// ============================================================

namespace sr {

template <int N>
class MSAAFramebuffer
{
    static_assert(N > 0 && N <= (std::numeric_limits<int>::max() - N / 2) / 255);

   public:
    MSAAFramebuffer(int width, int height) : MSAAFramebuffer(normalizeDimensions(width, height)) {}

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
        if (x < 0 || x >= width_ || y < 0 || y >= height_ || sampleIdx < 0 || sampleIdx >= N || !std::isfinite(z))
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
        if (x < 0 || x >= width_ || y < 0 || y >= height_ || sampleIdx < 0 || sampleIdx >= N)
            return;
        samples_[(size_t(y) * width_ + x) * N + sampleIdx] = c;
    }

    // 深度测试与颜色写入合并。
    //
    // 光栅化热路径里 testDepth() + setSample() 会各自做一遍边界检查
    // 和一次索引乘法，而两者用的是同一个样本索引。合并后每样本只算一次，
    // 语义不变：仍然只有深度通过才写颜色（不变量：颜色与深度同分支提交）。
    bool testAndWriteSample(int x, int y, int sampleIdx, float z, Color c)
    {
        if (x < 0 || x >= width_ || y < 0 || y >= height_ || sampleIdx < 0 || sampleIdx >= N || !std::isfinite(z))
            return false;
        const size_t index = (size_t(y) * width_ + x) * N + sampleIdx;
        float& slot = depth_[index];
        if (!(z < slot))
            return false;
        slot = z;
        samples_[index] = c;
        return true;
    }

    // 统计某像素被覆盖的样本数
    int coverageCount(int x, int y) const
    {
        if (x < 0 || x >= width_ || y < 0 || y >= height_)
            return 0;
        int count = 0;
        for (int i = 0; i < N; ++i)
            if (depth_[(size_t(y) * width_ + x) * N + i] < DepthBuffer::kFar)
                ++count;
        return count;
    }

    // Resolve 单行：把 y 行每个像素的 N 个样本平均后写入 dst[0..count)。
    //
    // 拆出行版本是为了让呈现路径（MSAAFlatRenderer::resolveTo）按行直写目标
    // 帧缓冲，不必先分配一整张 W×H 的临时 vector（1080p 下约 8 MB/帧）。
    // 平均公式与 resolve() 共用本函数，不会出现两份实现走偏。
    void resolveRow(int y, Color* dst, int count) const
    {
        assert(y >= 0 && y < height_);
        assert(dst != nullptr || count <= 0);
        if (y < 0 || y >= height_ || dst == nullptr || count <= 0)
            return;
        if (count > width_)
            count = width_;
        const Color* src = samples_.data() + size_t(y) * size_t(width_) * N;
        for (int x = 0; x < count; ++x)
        {
            int r = 0, g = 0, b = 0;
            for (int i = 0; i < N; ++i)
            {
                const Color& c = src[size_t(x) * N + i];
                r += c.r;
                g += c.g;
                b += c.b;
            }
            // 落盘时的 /N 是四舍五入：与全帧版本逐像素一致的唯一公式。
            dst[x] = Color(uint8_t((r + N / 2) / N), uint8_t((g + N / 2) / N), uint8_t((b + N / 2) / N));
        }
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
            resolveRow(y, dst.data() + size_t(y) * size_t(width_), width_);
    }

   private:
    struct Dimensions
    {
        int width;
        int height;
    };

    // 非法尺寸统一归一化为 0×0：任一边非正，或 width*height*N 会越过容器的
    // max_size()。两条路径都返回 {0,0}，不留「width() 返回 INT_MAX 但缓冲为空」
    // 的半有效状态 —— DepthBuffer 的构造器注释记录了同样的取舍。
    static Dimensions normalizeDimensions(int width, int height)
    {
        if (width <= 0 || height <= 0)
            return {0, 0};
        const size_t limit = std::min(std::vector<Color>().max_size(), std::vector<float>().max_size());
        if (size_t(width) > limit / size_t(height) / N)
            return {0, 0};
        return {width, height};
    }

    // 接收已归一化的尺寸：width()/height() 与实际缓冲长度始终一致。
    explicit MSAAFramebuffer(Dimensions dims)
        : width_(dims.width),
          height_(dims.height),
          samples_(size_t(dims.width) * size_t(dims.height) * N),
          depth_(samples_.size(), DepthBuffer::kFar)
    {
    }

    int width_, height_;
    std::vector<Color> samples_;
    std::vector<float> depth_;
};

// 4x MSAA 标准旋转网格采样偏移（像素坐标的分数偏移）
inline constexpr std::array<std::pair<float, float>, 4> kMSAA4Offsets = {
    {{0.375f, 0.125f}, {0.125f, 0.375f}, {-0.125f, -0.375f}, {-0.375f, -0.125f}}};

}  // namespace sr
