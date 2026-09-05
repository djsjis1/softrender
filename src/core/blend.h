#pragma once
#include <cstdint>
#include "core/color.h"

// ============================================================
// Alpha 混合模式
//
// 实时渲染中半透明物体的合成公式：
//   result = src * srcFactor + dst * dstFactor
//
// 常用模式：
//   AlphaBlend:    src*srcA + dst*(1-srcA)     —— 标准半透明
//   Additive:      src*1 + dst*1               —— 发光/粒子
//   Multiply:      src*dst + dst*0             —— 阴影/暗化
//   Premultiplied: src*1 + dst*(1-srcA)        —— 预乘 alpha（推荐）
// ============================================================
enum class BlendMode
{
    Opaque,       // 不混合，直接覆盖
    AlphaBlend,   // 标准 alpha 混合
    Additive,     // 加法混合
    Multiply,     // 乘法混合
    Premultiplied // 预乘 alpha
};

// 对单个像素做混合，返回混合后颜色
inline Color blendPixels(Color src, Color dst, BlendMode mode)
{
    switch (mode)
    {
    case BlendMode::Opaque:
        return src;
    case BlendMode::AlphaBlend:
    {
        float sa = src.a / 255.0f;
        float da = 1.0f - sa;
        return Color(
            uint8_t(src.r * sa + dst.r * da),
            uint8_t(src.g * sa + dst.g * da),
            uint8_t(src.b * sa + dst.b * da),
            uint8_t(std::min(255.0f, src.a + dst.a * da)));
    }
    case BlendMode::Additive:
    {
        float sa = src.a / 255.0f;
        return Color(
            uint8_t(std::min(255.0f, float(src.r) * sa + dst.r)),
            uint8_t(std::min(255.0f, float(src.g) * sa + dst.g)),
            uint8_t(std::min(255.0f, float(src.b) * sa + dst.b)),
            255);
    }
    case BlendMode::Multiply:
    {
        float sa = src.a / 255.0f;
        return Color(
            uint8_t(src.r * dst.r / 255.0f * sa + dst.r * (1.0f - sa)),
            uint8_t(src.g * dst.g / 255.0f * sa + dst.g * (1.0f - sa)),
            uint8_t(src.b * dst.b / 255.0f * sa + dst.b * (1.0f - sa)),
            255);
    }
    case BlendMode::Premultiplied:
    {
        float da = 1.0f - src.a / 255.0f;
        return Color(
            uint8_t(std::min(255.0f, float(src.r) + dst.r * da)),
            uint8_t(std::min(255.0f, float(src.g) + dst.g * da)),
            uint8_t(std::min(255.0f, float(src.b) + dst.b * da)),
            255);
    }
    }
    return src;
}
