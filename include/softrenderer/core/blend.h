#pragma once
#include <cstdint>

#include "softrenderer/core/color.h"

// ============================================================
// Alpha 混合模式
//
// 决定"前景色画到已有背景色上，最终像素是什么颜色"。
// 被 shape2d.h 的 plotBlend 逐像素调用，是 2D 渲染热路径。
//
// 全部使用整数运算（无浮点、无标准库依赖），
// 可直接运行在无 FPU 的 MCU（Cortex-M0/M3）上。
// ============================================================

namespace sr
{

enum class BlendMode : uint8_t
{
    // ── 不透明 ──────────────────────────────────────────
    // 前景直接盖住背景，完全看不到后面的内容。
    // 用于：实体方块、不透明精灵、UI 实色背景。
    // 性能：零计算，直接返回 src。
    Opaque,

    // ── 半透明 ──────────────────────────────────────────
    // 前景和背景按比例混合。α=0 全透明（只看到背景），
    // α=255 不透明（等同于 Opaque），中间值如玻璃/烟雾。
    // 用于：透明 UI、淡入淡出、水面、玻璃、阴影遮罩。
    AlphaBlend,

    // ── 加法叠加 ────────────────────────────────────────
    // 前景亮度加到背景上，越叠越亮，不会变暗。
    // 黑色加任何颜色 = 原色不变（黑色是加法的"透明色"）。
    // 用于：火焰、光晕、激光、粒子特效、爆炸光芒。
    Additive,

    // ── 乘法变暗 ────────────────────────────────────────
    // 前景和背景相乘，结果总是比两者都暗或相等。
    // 白色乘任何颜色 = 原色不变（白色是乘法的"透明色"）。
    // 用于：阴影投射、暗角效果、纹理叠加做旧、遮罩。
    Multiply,

    // ── 预乘 Alpha ──────────────────────────────────────
    // 与 AlphaBlend 视觉效果相同，但要求纹理的 RGB 已经
    // 预乘过 α（即存储时 R/G/B 已乘过 α/255）。
    // 比 AlphaBlend 少一次乘法，是游戏引擎和 GPU 的标准做法。
    // 用于：预乘纹理的精灵、GPU 导出的图集、字体渲染。
    Premultiplied
};

// 整数钳制到 [0, 255]
inline uint8_t clampU8(int v) noexcept
{
    return v < 0 ? 0 : v > 255 ? 255 : static_cast<uint8_t>(v);
}

// 四舍五入到最近整数：round(v / 255)，输入为 0 时返回 0。
//
// 不能改用移位近似：旧写法 (v + (v>>8) + 128) >> 8 在 v=33023 时得 129，
// 正确值应为 130；任何近似形式都需先做全输入范围验证。除数是编译期常量，
// 编译器会把 /255 强度折减为乘高位 + 移位，不会发出真正的除法指令。
inline int div255(int v) noexcept
{
    return v <= 0 ? 0 : (v + 127) / 255;
}

// 整数混合核心：(src × α + dst × (255-α)) / 255
inline int blendChannel(int src, int dst, int alpha, int invAlpha) noexcept
{
    return div255(src * alpha + dst * invAlpha);
}

// source-over 的结果 alpha：As + Ad × (1 − As)
//
// 单独成函数是因为旧实现把 alpha 当作 RGB 又乘了一次源 alpha，
// 实际算的是 As×As + Ad×(1−As)：半透明源盖在不透明目标上会得到 191
// 而不是 255，后续图层拿这个 alpha 继续合成就会偏暗。
inline int sourceOverAlpha(int srcAlpha, int dstAlpha) noexcept
{
    return srcAlpha + div255(dstAlpha * (255 - srcAlpha));
}

// 单像素混合：src（前景）画到 dst（背景）上，按 mode 决定最终颜色
inline Color blendPixels(Color src, Color dst, BlendMode mode) noexcept
{
    const int a = src.a;
    const int inv = 255 - a;

    switch (mode)
    {
    case BlendMode::Opaque:
        return src;

    case BlendMode::AlphaBlend:
    {
        const int outA = sourceOverAlpha(a, dst.a);
        // 目标不透明（Framebuffer 默认情况）时 outA 恒为 255，颜色退化为
        // 普通线性混合，避免逐通道除法。
        if (outA >= 255)
            return Color(clampU8(blendChannel(src.r, dst.r, a, inv)),
                         clampU8(blendChannel(src.g, dst.g, a, inv)),
                         clampU8(blendChannel(src.b, dst.b, a, inv)), 255);
        if (outA <= 0)
            return Color(0, 0, 0, 0);
        // 透明目标：直通色要先按加权和求出再除以结果 alpha，
        // 才能把预乘后的颜色还原成直通（straight）表示。
        const int dstWeight = div255(dst.a * inv);
        auto channel = [&](int s, int d) { return clampU8((s * a + d * dstWeight) / outA); };
        return Color(channel(src.r, dst.r), channel(src.g, dst.g), channel(src.b, dst.b),
                     static_cast<uint8_t>(outA));
    }

    case BlendMode::Additive:
        return Color(
            clampU8(div255(src.r * a) + dst.r),
            clampU8(div255(src.g * a) + dst.g),
            clampU8(div255(src.b * a) + dst.b),
            static_cast<uint8_t>(sourceOverAlpha(a, dst.a)));

    case BlendMode::Multiply:
        return Color(
            clampU8(blendChannel(div255(src.r * dst.r), dst.r, a, inv)),
            clampU8(blendChannel(div255(src.g * dst.g), dst.g, a, inv)),
            clampU8(blendChannel(div255(src.b * dst.b), dst.b, a, inv)),
            static_cast<uint8_t>(sourceOverAlpha(a, dst.a)));

    case BlendMode::Premultiplied:
    {
        const int outA = sourceOverAlpha(a, dst.a);
        if (outA <= 0)
            return Color(0, 0, 0, 0);
        // src 的 RGB 已预乘；dst 是直通表示，必须先预乘才能直接相加。
        auto channel = [&](int s, int d) { return clampU8(s + div255(div255(d * dst.a) * inv)); };
        return Color(channel(src.r, dst.r), channel(src.g, dst.g), channel(src.b, dst.b),
                     static_cast<uint8_t>(outA));
    }
    }
    return src;
}

}  // namespace sr
