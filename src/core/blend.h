#pragma once
#include <algorithm>
#include <cstdint>

#include "core/color.h"

// ============================================================
// Alpha 混合模式
//
// 公式：result = src × srcFactor + dst × dstFactor
//
// 模式说明：
//   Opaque        — 直接覆盖，忽略 dst
//   AlphaBlend    — 标准半透明：src×α + dst×(1-α)
//   Additive      — 加法混合：src + dst（发光/粒子）
//   Multiply      — 乘法混合：src×dst（阴影/暗化）
//   Premultiplied — 预乘 alpha：src + dst×(1-α)（推荐）
// ============================================================

enum class BlendMode : uint8_t
{
    Opaque,
    AlphaBlend,
    Additive,
    Multiply,
    Premultiplied
};

// float → uint8_t：四舍五入 + 钳制到 [0,255]
//
// 为什么不用 std::lround：热路径性能（单条指令 vs 函数调用）
// 为什么在这里钳制：避免调用方重复推理溢出问题
inline uint8_t toUint8Round(float v) noexcept
{
    return uint8_t(std::min(255.0f, std::max(0.0f, v)) + 0.5f);
}

// 像素混合：src（前景）与 dst（背景）按 mode 混合
inline Color blendPixels(Color src, Color dst, BlendMode mode)
{
    switch (mode)
    {
    case BlendMode::Opaque:
        return src;

    case BlendMode::AlphaBlend:
    {
        // 标准半透明：src×α + dst×(1-α)
        float sa = src.a / 255.0f;
        float da = 1.0f - sa;
        return Color(
            toUint8Round(src.r * sa + dst.r * da),
            toUint8Round(src.g * sa + dst.g * da),
            toUint8Round(src.b * sa + dst.b * da),
            toUint8Round(src.a + dst.a * da));
    }

    case BlendMode::Additive:
    {
        // 加法混合：src×α + dst（可达 510，需钳制）
        float sa = src.a / 255.0f;
        return Color(
            toUint8Round(float(src.r) * sa + dst.r),
            toUint8Round(float(src.g) * sa + dst.g),
            toUint8Round(float(src.b) * sa + dst.b),
            255);
    }

    case BlendMode::Multiply:
    {
        // 乘法混合：(src×dst/255)×α + dst×(1-α)
        float sa = src.a / 255.0f;
        return Color(
            toUint8Round(src.r * dst.r / 255.0f * sa + dst.r * (1.0f - sa)),
            toUint8Round(src.g * dst.g / 255.0f * sa + dst.g * (1.0f - sa)),
            toUint8Round(src.b * dst.b / 255.0f * sa + dst.b * (1.0f - sa)),
            255);
    }

    case BlendMode::Premultiplied:
    {
        // 预乘 alpha：src + dst×(1-α)（src 已预乘，可达 510，需钳制）
        float da = 1.0f - src.a / 255.0f;
        return Color(
            toUint8Round(float(src.r) + dst.r * da),
            toUint8Round(float(src.g) + dst.g * da),
            toUint8Round(float(src.b) + dst.b * da),
            255);
    }
    }
    return src;
}
