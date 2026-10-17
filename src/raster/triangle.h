#pragma once
#include <algorithm>

#include "core/framebuffer.h"
#include "math/vec.h"
#include "raster/coverage.h"
#include "raster/line.h"

// ============================================================
// 三角形光栅化：边函数 + 重心坐标法
//
// 【为什么是三角形】任何多边形都能拆成三角形；三个点必共面、
// 必凸，插值有唯一解——所以整个实时渲染世界都建立在三角形上。
//
// 【边函数 edge function】
//   E(p) = cross2(b - a, p - a)
//   几何意义：p 在有向边 a->b 的哪一侧（正=左侧）。
//   若 p 对三条边 (v0->v1, v1->v2, v2->v0) 的边函数同号，
//   则 p 在三角形内部。
//
// 【重心坐标 barycentric coordinates】
//   任意点 p 可写成 p = α*v0 + β*v1 + γ*v2（α+β+γ=1）。
//   α β γ 恰好等于 p 与对边构成的子三角形面积占比，
//   而"面积"正是边函数值的一半：
//     α = E12(p)/E, β = E20(p)/E, γ = E01(p)/E  （E 为整个三角形面积×2）
//   点在内部 ⇔ α β γ 全部 ≥ 0。
//   更妙的是：任何顶点属性（颜色/深度/UV/法线）都能用
//     attr(p) = α*attr0 + β*attr1 + γ*attr2
//   平滑插值到三角形内部——这是整条渲染管线的插值基石。
// ============================================================

// 计算 p 关于三角形 (v0,v1,v2) 的重心坐标。
// 返回 (α, β, γ)；若三角形退化（面积为 0）返回负值标记。
inline Vec3f barycentric(const Vec2f &v0, const Vec2f &v1, const Vec2f &v2, const Vec2f &p)
{
    float area2 = cross2(v1 - v0, v2 - v0);  // 整个三角形的 2 倍有向面积
    if (std::abs(area2) < 1e-8f)
        return {-1, -1, -1};  // 退化三角形
    // 每个分量 = 对边与 p 构成的子三角形面积 / 总面积
    float alpha = cross2(v2 - v1, p - v1) / area2;  // 对应 v0
    float beta = cross2(v0 - v2, p - v2) / area2;   // 对应 v1
    float gamma = 1.0f - alpha - beta;              // 三者和恒为 1
    return {alpha, beta, gamma};
}

// 填充纯色三角形：包围盒遍历 + 重心坐标内外测试
inline void fillTriangle(Framebuffer &fb, Vec2f v0, Vec2f v1, Vec2f v2, Color color)
{
    // 1. 求包围盒并夹取到屏幕范围（只遍历可能覆盖的像素）
    const auto coverage = sr::TriangleCoverage::create(v0, v1, v2);
    if (!coverage)
        return;
    const auto bounds = coverage->bounds(fb.width(), fb.height());
    if (bounds.empty())
        return;
    const int minX = bounds.minX, maxX = bounds.maxX, minY = bounds.minY, maxY = bounds.maxY;
    auto coverageRow = coverage->atPixel(minX, minY);

    // 2. 逐像素：采样点取像素中心 (+0.5)，共享边由 top-left 规则唯一归属。
    for (int y = minY; y <= maxY; ++y)
    {
        auto coveragePixel = coverageRow;
        for (int x = minX; x <= maxX; ++x)
        {
            const bool covered = sr::TriangleCoverage::contains(coveragePixel);
            coverage->advanceX(coveragePixel);
            if (!covered)
                continue;
            fb.setPixel(x, y, color);
        }
        coverage->advanceY(coverageRow);
    }
}

// 顶点色插值版本：三个顶点各带颜色，内部像素用重心坐标混合。
// OpenGL 里 varying 变量的插值就是同一件事。
inline void fillTriangleGradient(Framebuffer &fb, Vec2f v0, Vec2f v1, Vec2f v2, Color c0, Color c1, Color c2)
{
    const auto coverage = sr::TriangleCoverage::create(v0, v1, v2);
    if (!coverage)
        return;
    const auto bounds = coverage->bounds(fb.width(), fb.height());
    if (bounds.empty())
        return;
    const int minX = bounds.minX, maxX = bounds.maxX, minY = bounds.minY, maxY = bounds.maxY;
    auto coverageRow = coverage->atPixel(minX, minY);

    for (int y = minY; y <= maxY; ++y)
    {
        auto coveragePixel = coverageRow;
        for (int x = minX; x <= maxX; ++x)
        {
            Vec3f bc = barycentric(v0, v1, v2, {x + 0.5f, y + 0.5f});
            const bool covered = sr::TriangleCoverage::contains(coveragePixel);
            coverage->advanceX(coveragePixel);
            if (!covered)
                continue;
            // attr(p) = α*attr0 + β*attr1 + γ*attr2 —— 对颜色的三路混合
            Color c(uint8_t(std::clamp(bc.x * c0.r + bc.y * c1.r + bc.z * c2.r, 0.0f, 255.0f)),
                    uint8_t(std::clamp(bc.x * c0.g + bc.y * c1.g + bc.z * c2.g, 0.0f, 255.0f)),
                    uint8_t(std::clamp(bc.x * c0.b + bc.y * c1.b + bc.z * c2.b, 0.0f, 255.0f)));
            fb.setPixel(x, y, c);
        }
        coverage->advanceY(coverageRow);
    }
}

// 线框三角形（复用阶段 1 的画线）
inline void wireTriangle(Framebuffer &fb, Vec2f v0, Vec2f v1, Vec2f v2, Color color)
{
    drawLine(fb, (int)v0.x, (int)v0.y, (int)v1.x, (int)v1.y, color);
    drawLine(fb, (int)v1.x, (int)v1.y, (int)v2.x, (int)v2.y, color);
    drawLine(fb, (int)v2.x, (int)v2.y, (int)v0.x, (int)v0.y, color);
}
