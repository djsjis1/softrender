#pragma once
#include <cmath>

#include "softrenderer/math/vec.h"

// ============================================================
// 重心坐标：三角形上任意点相对三个顶点的权重。
//
// 这是纯数学，与"光栅化"无关，因此独立于 render/ ——任何需要
// 「点相对三角形的权重」的地方（光栅化、MSAA 子采样、测试里
// 独立推导的期望值）都能直接包含本头。
//
// 【定义】任意点 p 可写成 p = α*v0 + β*v1 + γ*v2（α+β+γ=1）。
//   α β γ 恰好等于 p 与对边构成的子三角形面积占比，
//   而"面积"正是边函数值的一半：
//     α = E12(p)/E, β = E20(p)/E, γ = E01(p)/E  （E 为整个三角形面积×2）
//   点在内部 ⇔ α β γ 全部 ≥ 0。
// ============================================================

namespace sr
{

// 计算 p 关于三角形 (v0,v1,v2) 的重心坐标。
// 返回 (α, β, γ)；若三角形退化（面积为 0）返回负值标记。
inline Vec3f barycentric(const Vec2f &v0, const Vec2f &v1, const Vec2f &v2, const Vec2f &p)
{
    float area2 = cross2(v1 - v0, v2 - v0); // 整个三角形的 2 倍有向面积
    if (std::abs(area2) < 1e-8f)
        return {-1, -1, -1}; // 退化三角形
    // 每个分量 = 对边与 p 构成的子三角形面积 / 总面积
    float alpha = cross2(v2 - v1, p - v1) / area2; // 对应 v0
    float beta = cross2(v0 - v2, p - v2) / area2;  // 对应 v1
    float gamma = 1.0f - alpha - beta;             // 三者和恒为 1
    return {alpha, beta, gamma};
}

} // namespace sr
