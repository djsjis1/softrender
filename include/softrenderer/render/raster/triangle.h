#pragma once
#include <cstdint>
#include <algorithm>

#include "softrenderer/core/framebuffer.h"
#include "softrenderer/math/barycentric.h"
#include "softrenderer/math/vec.h"
#include "softrenderer/render/raster/coverage.h"

// ============================================================
// 三角形光栅化：覆盖规则 + 顶点属性插值
//
// 【为什么是三角形】任何多边形都能拆成三角形；三个点必共面、
// 必凸，插值有唯一解——所以整个实时渲染世界都建立在三角形上。
//
// 【插值基石】任何顶点属性（颜色/深度/UV/法线）都能用重心坐标
//     attr(p) = α*attr0 + β*attr1 + γ*attr2
// 平滑插值到三角形内部。重心坐标本身是纯数学（见 math/barycentric.h），
// 本文件只负责"哪些像素被覆盖"这个光栅化问题。
//
// 【边界】本文件只做光栅化，不依赖 2D 图元模块。线框三角形属于
// 2D 绘制能力，放在 render/draw2d/shape2d.h 的 sr::draw2d::wireTriangle。
// ============================================================

// 填充纯色三角形：包围盒遍历 + 覆盖规则判定
namespace sr
{

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
            Vec3f bc = sr::barycentric(v0, v1, v2, {x + 0.5f, y + 0.5f});
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

}  // namespace sr
