#pragma once
#include <cstddef>
#include <cstdint>

#include "softrenderer/core/color.h"

// ============================================================
// 存储像素格式：与 Color（运算域）分离
//
// 【为什么需要这一层】Color 同时承担三个互相冲突的角色：
//   存储单元  —— Framebuffer 的像素数组，越紧凑越好
//   运算域    —— 着色输出、blend 五种混合、纹理采样，需要逐通道算数 + 8 位 alpha
//   IO 契约   —— io/tga.cpp、io/image_write.cpp 按 RGBA8 通道顺序读写
// 把 Color 直接改成 RGB565 会牺牲后两个角色：alpha 语义丢失、按 Color 内存
// 布局工作的写出路径静默产出错图。因此保留 Color 作为唯一运算域，
// 另设"存储格式类型"，量化只发生在写入帧缓冲的那一刻（GPU 的
// "render target 是 565、着色器内部是高精度" 分工）。
//
// 【参考实现】AGG 的 pixfmt_rgb24 / pixfmt_rgb565 是一格式一类型、
// 渲染器模板化；pixman 是格式码 + 布局描述。本项目取 AGG 做法，
// 因为它是 header-only 且目标含无堆、无 RTTI 的裸机：
// 格式进入类型 ⇒ 每像素零分支、零虚调用、无运行时格式表。
//
// 【PixelT 契约】Framebuffer<PixelT> 只要求：
//   static constexpr size_t kBytes;                      单个像素字节数
//   static PixelT fromColor(const Color&);               RGBA8888 → 存储域（量化点）
//   static PixelT fromFloat(float r, float g, float b);   运算域 → 存储域
//   Color toColor() const;                               存储域 → RGBA8888（回读）
//   operator==                                           测试与回归比对
// 加上平凡拷贝，其余能力由 BasicFramebuffer 提供。
//
// 【能力边界】Color565 / Gray8 没有 alpha 通道，而 blend.h 的五种混合模式
// 全部依赖 8 位 alpha，故 2D 子系统固定使用 RGBA8888。见 ADR-005。
// ============================================================

namespace sr
{

// ------------------------------------------------------------
// 运算域浮点 → 8 位通道的钳制换算。
//
// 【为什么不复用 Color::fromFloat】那条路径会把 alpha 通道也钳制并换算成字节，
// 而紧凑格式没有 alpha 位——实测烟花管线的色调映射 pass 因此慢 3%。
// 单独放一个 struct 是让 Color565 与 Gray8 共用同一份实现，避免两份钳制逻辑漂移。
// 语义与 Color::fromFloat 完全一致：NaN 的两个比较都为假 ⇒ 落到 0。
// ------------------------------------------------------------
struct PixelQuantize
{
    static uint8_t toByte(float v) noexcept
    {
        const float c = v > 0.0f ? (v < 1.0f ? v : 1.0f) : 0.0f;
        return uint8_t(c * 255.0f + 0.5f);
    }
};

// ------------------------------------------------------------
// RGB565：16 位打包，R 5 位 / G 6 位 / B 5 位。
//
// G 分到 6 位不是随意分配：人眼对绿色最敏感，同样的位数给绿色时
// 视觉误差最小。代价是三个通道的量化步长不同，做对位比较时须按通道区分。
// ------------------------------------------------------------
struct Color565
{
    uint16_t v = 0;

    static constexpr size_t kBytes = 2;

    // 四舍五入量化：round(x * 31 / 255)。
    // 不能写成 x >> 3（截断）：实测 x=5 时截断得 0、四舍五入得 1；
    // 全输入范围上截断的最大偏差 7 级，本式 4 级。
    static constexpr uint8_t quantize5(uint8_t x) noexcept
    {
        return uint8_t((unsigned(x) * 31u + 127u) / 255u);
    }
    static constexpr uint8_t quantize6(uint8_t x) noexcept
    {
        return uint8_t((unsigned(x) * 63u + 127u) / 255u);
    }
    // 位复制扩展（高位补低位）。这个形式保证 expand(qmax) == 255，
    // 从而 fromColor(toColor(p)) == p 对全部 65536 个取值成立（有穷举测试锁定）。
    static constexpr uint8_t expand5(uint8_t q) noexcept
    {
        return uint8_t((unsigned(q) << 3) | (unsigned(q) >> 2));
    }
    static constexpr uint8_t expand6(uint8_t q) noexcept
    {
        return uint8_t((unsigned(q) << 2) | (unsigned(q) >> 4));
    }

    constexpr Color565() noexcept = default;
    explicit constexpr Color565(uint16_t raw) noexcept : v(raw) {}

    static constexpr Color565 fromColor(const Color& c) noexcept
    {
        return Color565(uint16_t((unsigned(quantize5(c.r)) << 11) | (unsigned(quantize6(c.g)) << 5) |
                                 unsigned(quantize5(c.b))));
    }

    // 运算域 → 存储域（只换算用到的三个通道，见 PixelQuantize 的说明）。
    static Color565 fromFloat(float r, float g, float b, float = 1.0f) noexcept
    {
        return Color565(uint16_t((unsigned(quantize5(PixelQuantize::toByte(r))) << 11) |
                                 (unsigned(quantize6(PixelQuantize::toByte(g))) << 5) |
                                 unsigned(quantize5(PixelQuantize::toByte(b)))));
    }

    Color toColor() const noexcept
    {
        return Color(expand5(uint8_t((v >> 11) & 0x1Fu)), expand6(uint8_t((v >> 5) & 0x3Fu)),
                     expand5(uint8_t(v & 0x1Fu)));
    }

    friend constexpr bool operator==(Color565 a, Color565 b) noexcept { return a.v == b.v; }
};

// ------------------------------------------------------------
// 8 位灰度：单色 OLED / 墨水屏 / 遮罩的原生布局。
//
// 权重取 Rec.601 的 76/150/29（和恰为 255），(sum + 127) / 255 完成四舍五入。
// 【为什么不用 77/150/29 + >>8】那组权重和为 256，右移 8 位等于除以 256，
// 纯绿 (0,255,0) 会得 149，而 Rec.601 应为 150（0.587 × 255 = 149.7）。
// 和为 255 的权重配合 /255 才与常数除法的精确舍入一致；除数由编译器
// 强度折减为乘高位加移位（与 core/blend.h 的 div255 同一做法）。
// 全整数、无浮点，可在无 FPU 的 Cortex-M0/M3 上直接使用。
// ------------------------------------------------------------
struct Gray8
{
    uint8_t v = 0;

    static constexpr size_t kBytes = 1;

    constexpr Gray8() noexcept = default;
    explicit constexpr Gray8(uint8_t raw) noexcept : v(raw) {}

    static Gray8 fromColor(const Color& c) noexcept
    {
        const unsigned sum = 76u * c.r + 150u * c.g + 29u * c.b;
        // 上界 (76+150+29) * 255 + 127 = 65152，除以 255 得 255，不会溢出 uint8_t。
        return Gray8(uint8_t((sum + 127u) / 255u));
    }

    // 与 Color565::fromFloat 同理：只换算亮度需要的三个分量。
    static Gray8 fromFloat(float r, float g, float b, float = 1.0f) noexcept
    {
        const unsigned sum = 76u * PixelQuantize::toByte(r) + 150u * PixelQuantize::toByte(g) +
                             29u * PixelQuantize::toByte(b);
        return Gray8(uint8_t((sum + 127u) / 255u));
    }

    Color toColor() const noexcept { return Color(v, v, v); }

    friend constexpr bool operator==(Gray8 a, Gray8 b) noexcept { return a.v == b.v; }
};

}  // namespace sr
