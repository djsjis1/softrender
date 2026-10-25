#pragma once
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "softrenderer/core/framebuffer.h"

// ============================================================
// 帧缓冲格式转换与呈现放大。
//
// 【为什么集中在一个头】量化与放大如果散落在调用点，每个平台各写一遍，
// 就会出现"某处截断、某处四舍五入"的不一致（本项目历史上 tga 写出、
// image_write、示例各自处理过通道顺序）。这里的定位对标 SDL 的
// SDL_ConvertPixels：转换逻辑只有一份，调用方只给"源和目标"。
//
// 【两种用法】
//   1. convertFramebuffer(src, dst)  —— 同尺寸换格式（8888 → 565 / 灰度）
//   2. NearestUpscaler               —— 小画布渲染 + 逐行放大 + 换格式，
//      对应"渲染分辨率与显示分辨率解耦"。映射表在 resize 时预计算，
//      逐像素无除法；presentRow 逐行输出，可直接喂给 LCD 的行写接口。
//
// 【坐标约定】沿用 Framebuffer：第 0 行是图像最底部一行。放大只做比例映射，
// 不翻转，调用方若需要自上而下写出，自行按 h-1-y 取行。
// ============================================================

namespace sr
{

// 同尺寸（取两者较小区域）逐像素转换，含格式量化。
// src 与 dst 尺寸不同时只转换重叠的左上角区域——不缩放，缩放交给 NearestUpscaler。
template <class SrcFB, class DstFB>
void convertFramebuffer(const SrcFB& src, DstFB& dst)
{
    using SrcPixel = typename SrcFB::Pixel;
    using DstPixel = typename DstFB::Pixel;
    const int w = std::min(src.width(), dst.width());
    const int h = std::min(src.height(), dst.height());
    if (w <= 0 || h <= 0)
        return;
    for (int y = 0; y < h; ++y)
    {
        const SrcPixel* srow = src.row(y);
        DstPixel* drow = dst.row(y);
        if constexpr (std::is_same_v<SrcPixel, DstPixel>)
        {
            // 同格式：直接整行拷贝。
            // 走通用路径会做"展开成 RGBA8888 再量化回来"的往返——对 565 源
            // 虽然无损（有 65536 值穷举测试锁定），但每像素白付一次解包和
            // 两次通道乘除。MCU 上"渲染 565 → 呈现 565"正是这条路径。
            std::copy(srow, srow + w, drow);
        }
        else
        {
            for (int x = 0; x < w; ++x)
                drow[x] = DstPixel::fromColor(srow[x].toColor());
        }
    }
}

// 最近邻放大 + 格式转换。
//
// 【为什么不写进 Framebuffer】放大是"呈现"行为，与像素存储格式正交：
// 同一个放大表既能输出 565（SPI 屏），也能输出灰度（单色 OLED）。
// 因此它持有的是映射表而不是像素，用模板函数接收任意 BasicFramebuffer。
class NearestUpscaler
{
   public:
    // 尺寸非正时清空映射表，valid() 随之为假，present 变成空操作。
    void resize(int srcW, int srcH, int dstW, int dstH)
    {
        if (srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0)
        {
            xmap_.clear();
            ymap_.clear();
            srcW_ = srcH_ = dstW_ = dstH_ = 0;
            return;
        }
        srcW_ = srcW;
        srcH_ = srcH;
        dstW_ = dstW;
        dstH_ = dstH;
        xmap_.resize(size_t(dstW));
        ymap_.resize(size_t(dstH));
        // 目标下标 → 源下标：i * srcSize / dstSize。用 int64 中间量避免
        // 大分辨率下 int 溢出；整数倍放大时结果精确（4→2 得 0,0,1,1）。
        for (int x = 0; x < dstW; ++x)
            xmap_[size_t(x)] = int((int64_t(x) * srcW) / dstW);
        for (int y = 0; y < dstH; ++y)
            ymap_[size_t(y)] = int((int64_t(y) * srcH) / dstH);
    }

    bool valid() const { return dstW_ > 0 && dstH_ > 0 && !xmap_.empty() && !ymap_.empty(); }
    int srcWidth() const { return srcW_; }
    int srcHeight() const { return srcH_; }
    int dstWidth() const { return dstW_; }
    int dstHeight() const { return dstH_; }

    // 输出目标缓冲的第 dy 行。
    // 【契约】src 的尺寸必须与 resize() 时声明的一致（否则映射表失效）；
    // 不一致时 Debug 断言，Release 下直接返回，避免按错误索引读越界。
    template <class SrcFB, class DstFB>
    void presentRow(const SrcFB& src, int dy, DstFB& dst) const
    {
        if (!valid() || dy < 0 || dy >= dstH_ || dy >= dst.height())
            return;
        assert(src.width() == srcW_ && src.height() == srcH_);
        if (src.width() != srcW_ || src.height() != srcH_)
            return;
        using SrcPixel = typename SrcFB::Pixel;
        using DstPixel = typename DstFB::Pixel;
        const SrcPixel* srow = src.row(ymap_[size_t(dy)]);
        DstPixel* drow = dst.row(dy);
        const int w = std::min(dstW_, dst.width());
        if constexpr (std::is_same_v<SrcPixel, DstPixel>)
        {
            // 同格式呈现：只做列映射，不做格式往返（见 convertFramebuffer 的说明）。
            for (int x = 0; x < w; ++x)
                drow[x] = srow[xmap_[size_t(x)]];
        }
        else
        {
            for (int x = 0; x < w; ++x)
                drow[x] = DstPixel::fromColor(srow[xmap_[size_t(x)]].toColor());
        }
    }

    // 整幅输出：逐行调用 presentRow，等价于"小画布 → 屏幕"的一次完整呈现。
    template <class SrcFB, class DstFB>
    void present(const SrcFB& src, DstFB& dst) const
    {
        if (!valid())
            return;
        const int h = std::min(dstH_, dst.height());
        for (int y = 0; y < h; ++y)
            presentRow(src, y, dst);
    }

   private:
    std::vector<int> xmap_;
    std::vector<int> ymap_;
    int srcW_ = 0, srcH_ = 0, dstW_ = 0, dstH_ = 0;
};

}  // namespace sr
