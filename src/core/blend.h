#pragma once
#include <algorithm>
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
// 为什么给 BlendMode 指定底层类型：cpp_project_rules.md 要求「枚举值必须有
// 明确的底层类型」。uint8_t 足够容纳 5 个模式，且使本枚举在作为结构体成员
// 或数组下标时占用可预测（与 RenderLayer : uint16_t、CubeFace : int 一致的做法）。
enum class BlendMode : uint8_t
{
    Opaque,       // 不混合，直接覆盖
    AlphaBlend,   // 标准 alpha 混合
    Additive,     // 加法混合
    Multiply,     // 乘法混合
    Premultiplied // 预乘 alpha
};

// float 颜色分量 → uint8_t，四舍五入并钳制到 [0,255]。
//
// 【为什么需要它】旧实现四处都是 uint8_t(float表达式)，**直接截断小数**，
// 每次混合系统性偏暗最多 1/255。particle_system / render_queue / sr2d::plotBlend
// 都依赖本函数，多层叠加会累积出可见的暗化偏差。
//
// 【为什么用 +0.5f 而非 std::lround】混合是逐像素热路径，lround 会引入
// 函数调用与对当前浮点舍入模式的依赖；加法在 MSVC/GCC 下都编译为单条指令。
//
// 【为什么钳制放在这里而不靠调用方】AlphaBlend 与 Multiply 两个分支的上界
// 依赖 sa + da == 1 的数学性质（浮点下 Sterbenz 引理保证 1.0f - sa 精确），
// 理论上不超 255，但逐个调用点重复推理极易出错。把钳制收敛到本函数，
// 使「不溢出」成为后置条件。std::min/std::max 在此编译为无分支的 minss/maxss。
//
// 【max(0) 的必要性】float → 整型的转换在值超出目标类型范围时是 UB，
// 负值同样如此。当前四个分支的输入均非负，但钳制下界可消除这个隐患。
inline uint8_t toUint8Round(float v) noexcept
{
    return uint8_t(std::min(255.0f, std::max(0.0f, v)) + 0.5f);
}

// 对单个像素做混合，返回混合后颜色
inline Color blendPixels(Color src, Color dst, BlendMode mode)
{
    switch (mode)
    {
    case BlendMode::Opaque:
        return src;
    case BlendMode::AlphaBlend:
    {
        // sa + da == 1，结果是 src 与 dst 的凸组合，上界天然不超 255；
        // 钳制由 toUint8Round 统一负责。
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
        // src*sa + dst 可达 510，必须钳制（由 toUint8Round 负责）
        float sa = src.a / 255.0f;
        return Color(
            toUint8Round(float(src.r) * sa + dst.r),
            toUint8Round(float(src.g) * sa + dst.g),
            toUint8Round(float(src.b) * sa + dst.b),
            255);
    }
    case BlendMode::Multiply:
    {
        // src.r * dst.r 是 uint8_t 提升后的 int 运算，最大 65025，不溢出。
        // 整体是 (src*dst/255) 与 dst 的凸组合，上界不超 255。
        float sa = src.a / 255.0f;
        return Color(
            toUint8Round(src.r * dst.r / 255.0f * sa + dst.r * (1.0f - sa)),
            toUint8Round(src.g * dst.g / 255.0f * sa + dst.g * (1.0f - sa)),
            toUint8Round(src.b * dst.b / 255.0f * sa + dst.b * (1.0f - sa)),
            255);
    }
    case BlendMode::Premultiplied:
    {
        // src 已预乘，直接相加可达 510，必须钳制（由 toUint8Round 负责）
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
