#pragma once
#include <cstddef>
#include <algorithm>
#include <array>
#include <cmath>

#include "softrenderer/core/framebuffer.h"
#include "softrenderer/core/msaa_framebuffer.h"
#include "softrenderer/render/raster/coverage.h"
#include "softrenderer/render/raster/triangle.h"
#include "softrenderer/math/vec.h"

namespace sr {
// 常量颜色的教学 MSAA 管线；每个样本的颜色必须与其深度在同一分支提交。
//
// 【超大三角形】coverage 的坐标上限是 ±2^20，超出会整体拒绝。而屏幕坐标
// 可以远超视口（例如相机贴地时的地板三角形），直接交给 coverage 会让
// 整块几何静默消失。此时先按视口矩形做 2D 裁剪再光栅化：颜色与深度都是
// 屏幕空间线性量，直接线性插值即正确。
template <int Samples>
class MSAAFlatRenderer
{
    static_assert(Samples == 1 || Samples == 4, "only center or rotated 4x sampling is supported");

   public:
    MSAAFlatRenderer(int width, int height) : buffer_(width, height) {}
    void clear(Color background) { buffer_.clear(background); }
    void rasterize(const Vec2f& p0, const Vec2f& p1, const Vec2f& p2, float z0, float z1, float z2, Color color)
    {
        if (!std::isfinite(z0) || !std::isfinite(z1) || !std::isfinite(z2))
            return;
        const float limit = float(TriangleCoverage::coordinateLimit);
        const Vec2f v[3] = {p0, p1, p2};
        const float z[3] = {z0, z1, z2};
        for (const Vec2f& point : v)
        {
            if (!std::isfinite(point.x) || !std::isfinite(point.y))
                return;
            if (point.x < -limit || point.x > limit || point.y < -limit || point.y > limit)
            {
                clipAndRasterize(v, z, color);
                return;
            }
        }
        rasterizeTriangle(p0, p1, p2, z0, z1, z2, color);
    }
    // 按行直写目标帧缓冲：不再分配整帧临时的 vector<Color>（1080p 下约 8 MB/帧），
    // 也顺手省掉 setPixel 的逐像素边界检查——行列范围已由 min 夹取保证。
    void resolveTo(Framebuffer& destination) const
    {
        const int rows = std::min(buffer_.height(), destination.height());
        const int columns = std::min(buffer_.width(), destination.width());
        for (int y = 0; y < rows; ++y)
            buffer_.resolveRow(y, destination.row(y), columns);
    }

   private:
    struct ClipPoint
    {
        Vec2f p;
        float z;
    };

    // 与 coverage 不同的职责：这里只限制几何到像素矩形内，不做覆盖归属。
    void rasterizeTriangle(const Vec2f& p0, const Vec2f& p1, const Vec2f& p2, float z0, float z1, float z2, Color color)
    {
        const auto coverage = TriangleCoverage::create(p0, p1, p2);
        if (!coverage || !std::isfinite(z0) || !std::isfinite(z1) || !std::isfinite(z2))
            return;
        if (std::abs(cross2(p1 - p0, p2 - p0)) < 1e-8f)
            return;
        const auto bounds = coverage->bounds(buffer_.width(), buffer_.height());
        // 边函数 E0/E1/E2 分别对应 v2/v0/v1 的权重（同 Renderer::rasterTriangle）。
        const double invArea = 1.0 / double(coverage->area());

        // 样本偏移是像素内的固定值（kMSAA4Offsets 均为 1/8 像素），
        // 所以每个样本的边函数值 = 像素中心值 + 常量增量。
        // 预计算这组增量后，逐样本只剩 3 次加法，不再重新量化坐标、
        // 不再重算 9 次乘法（那是这条管线最大的浪费）。
        using Values = TriangleCoverage::Values;
        std::array<Values, Samples> sampleDelta{};
        for (int sample = 0; sample < Samples; ++sample)
        {
            int64_t offsetX = 0, offsetY = 0;
            if constexpr (Samples == 4)
            {
                const auto& offset = kMSAA4Offsets[sample];
                offsetX = int64_t(std::llround(offset.first * float(TriangleCoverage::subpixelScale)));
                offsetY = int64_t(std::llround(offset.second * float(TriangleCoverage::subpixelScale)));
            }
            for (size_t edge = 0; edge < 3; ++edge)
            {
                const auto& e = coverage->edges()[edge];
                sampleDelta[size_t(sample)][edge] = e.a * offsetX + e.b * offsetY;
            }
        }

        // 主循环骨架与 Renderer::rasterTriangle 共用 sr::forEachCoveredPixel。
        forEachCoveredPixel(
            *coverage, bounds, [](int) {},
            [&](int x, int y, const Values& pixelValues) {
                for (int sample = 0; sample < Samples; ++sample)
                {
                    const Values& delta = sampleDelta[size_t(sample)];
                    const Values values{pixelValues[0] + delta[0], pixelValues[1] + delta[1],
                                        pixelValues[2] + delta[2]};
                    if (!TriangleCoverage::contains(values))
                        continue;
                    // 权重映射与着色管线同一份实现（sr::barycentricFromValues）
                    const Vec3f bc = barycentricFromValues(*coverage, values, invArea);
                    const float z = bc.x * z0 + bc.y * z1 + bc.z * z2;
                    // 深度测试与颜色写入合并：两者共用样本索引，分开调用会重复边界检查
                    buffer_.testAndWriteSample(x, y, sample, z, color);
                }
            });
    }

    // Sutherland-Hodgman 对像素矩形 [0,width]×[0,height] 裁剪，再三角扇拆分。
    void clipAndRasterize(const Vec2f v[3], const float z[3], Color color)
    {
        std::array<ClipPoint, 16> polygon, scratch;
        int count = 3;
        for (int i = 0; i < 3; ++i)
            polygon[size_t(i)] = {v[i], z[i]};

        const float w = float(buffer_.width());
        const float h = float(buffer_.height());
        auto distance = [w, h](const ClipPoint& c, int plane) {
            switch (plane)
            {
                case 0: return c.p.x;      // 内侧：x >= 0
                case 1: return w - c.p.x;  // 内侧：x <= width
                case 2: return c.p.y;      // 内侧：y >= 0
                default: return h - c.p.y; // 内侧：y <= height
            }
        };

        for (int plane = 0; plane < 4 && count > 0; ++plane)
        {
            int produced = 0;
            for (int i = 0; i < count; ++i)
            {
                const ClipPoint& cur = polygon[size_t(i)];
                const ClipPoint& nxt = polygon[size_t((i + 1) % count)];
                const float dCur = distance(cur, plane);
                const float dNxt = distance(nxt, plane);
                const bool inCur = dCur >= 0.0f;
                const bool inNxt = dNxt >= 0.0f;
                if (inCur && produced < int(scratch.size()))
                    scratch[size_t(produced++)] = cur;
                if (inCur != inNxt && produced < int(scratch.size()))
                {
                    const float t = dCur / (dCur - dNxt);
                    scratch[size_t(produced++)] = {{cur.p.x + (nxt.p.x - cur.p.x) * t, cur.p.y + (nxt.p.y - cur.p.y) * t},
                                                   cur.z + (nxt.z - cur.z) * t};
                }
            }
            for (int i = 0; i < produced; ++i)
                polygon[size_t(i)] = scratch[size_t(i)];
            count = produced;
        }

        for (int i = 1; i + 1 < count; ++i)
            rasterizeTriangle(polygon[0].p, polygon[size_t(i)].p, polygon[size_t(i + 1)].p, polygon[0].z,
                              polygon[size_t(i)].z, polygon[size_t(i + 1)].z, color);
    }

    MSAAFramebuffer<Samples> buffer_;
};
}  // namespace sr
