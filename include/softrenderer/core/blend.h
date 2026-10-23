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

// 整数除以 255：(v + (v >> 8) + 128) >> 8
// 等价于 v / 255，对所有 v ∈ [0, 65025] 精确无误差
// 仅用移位和加法，无浮点、无硬件除法，Cortex-M0 也能跑
inline int div255(int v) noexcept
{
    return (v + (v >> 8) + 128) >> 8;
}

// 整数混合核心：(src × α + dst × (255-α)) / 255
inline int blendChannel(int src, int dst, int alpha, int invAlpha) noexcept
{
    return div255(src * alpha + dst * invAlpha);
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
        return Color(
            clampU8(blendChannel(src.r, dst.r, a, inv)),
            clampU8(blendChannel(src.g, dst.g, a, inv)),
            clampU8(blendChannel(src.b, dst.b, a, inv)),
            clampU8(blendChannel(src.a, dst.a, a, inv)));

    case BlendMode::Additive:
        return Color(
            clampU8(div255(src.r * a) + dst.r),
            clampU8(div255(src.g * a) + dst.g),
            clampU8(div255(src.b * a) + dst.b),
            255);

    case BlendMode::Multiply:
        return Color(
            clampU8(blendChannel(div255(src.r * dst.r), dst.r, a, inv)),
            clampU8(blendChannel(div255(src.g * dst.g), dst.g, a, inv)),
            clampU8(blendChannel(div255(src.b * dst.b), dst.b, a, inv)),
            255);

    case BlendMode::Premultiplied:
        return Color(
            clampU8(src.r + div255(dst.r * inv)),
            clampU8(src.g + div255(dst.g * inv)),
            clampU8(src.b + div255(dst.b * inv)),
            255);
    }
    return src;
}

}  // namespace sr
