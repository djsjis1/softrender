#pragma once
#include <cstddef>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>

#include "softrenderer/math/vec.h"

namespace sr {
struct RasterBounds
{
    int minX = 0, maxX = -1, minY = 0, maxY = -1;
    bool empty() const { return minX > maxX || minY > maxY; }
};

// 覆盖专用的 8 位子像素网格，偏置不可用于属性插值。详见 ADR-003。
class TriangleCoverage
{
   public:
    static constexpr int64_t subpixelScale = 256;
    static constexpr int coordinateLimit = 1 << 20;
    using Values = std::array<int64_t, 3>;
    struct Edge
    {
        int64_t a = 0, b = 0, c = 0;
        int64_t stepX() const { return a * subpixelScale; }
        int64_t stepY() const { return b * subpixelScale; }
    };

    // 非有限、超范围或量化后退化的几何返回空值，调用方不绘制。
    static std::optional<TriangleCoverage> create(const Vec2f& v0, const Vec2f& v1, const Vec2f& v2)
    {
        const std::array<Vec2f, 3> vertices{v0, v1, v2};
        std::array<int64_t, 3> x{}, y{};
        for (size_t i = 0; i < 3; ++i)
        {
            if (!quantize(vertices[i].x, x[i]) || !quantize(vertices[i].y, y[i]))
                return std::nullopt;
        }
        const int64_t area = (x[1] - x[0]) * (y[2] - y[0]) - (y[1] - y[0]) * (x[2] - x[0]);
        if (!area)
            return std::nullopt;
        TriangleCoverage result;
        const int64_t sign = area > 0 ? 1 : -1;
        for (size_t i = 0; i < 3; ++i)
        {
            const size_t j = (i + 1) % 3;
            const int64_t dx = (x[j] - x[i]) * sign, dy = (y[j] - y[i]) * sign;
            // y 向上：顶边向左、左边向下；其他边在整数域减一排除等号。
            const bool inclusive = dy < 0 || (dy == 0 && dx < 0);
            result.edges_[i] = {-dy, dx, dy * x[i] - dx * y[i] - (inclusive ? 0 : 1)};
            result.bias_[i] = inclusive ? 0 : 1;
        }
        result.area_ = area * sign;
        result.minX_ = *std::min_element(x.begin(), x.end());
        result.maxX_ = *std::max_element(x.begin(), x.end());
        result.minY_ = *std::min_element(y.begin(), y.end());
        result.maxY_ = *std::max_element(y.begin(), y.end());
        return result;
    }
    RasterBounds bounds(int width, int height) const
    {
        if (width <= 0 || height <= 0)
            return {};
        auto floorPixel = [](int64_t q) {
            return static_cast<int>((q < 0 ? q - subpixelScale + 1 : q) / subpixelScale);
        };
        auto ceilPixel = [](int64_t q) {
            return static_cast<int>((q > 0 ? q + subpixelScale - 1 : q) / subpixelScale);
        };
        return {std::max(0, floorPixel(minX_)), std::min(width - 1, ceilPixel(maxX_)), std::max(0, floorPixel(minY_)),
                std::min(height - 1, ceilPixel(maxY_))};
    }
    Values atPixel(int x, int y) const
    {
        if (x < -coordinateLimit || x > coordinateLimit || y < -coordinateLimit || y > coordinateLimit)
            return {-1, -1, -1};
        return evaluate(int64_t(x) * subpixelScale + subpixelScale / 2, int64_t(y) * subpixelScale + subpixelScale / 2);
    }
    bool containsSample(float x, float y) const
    {
        int64_t qx = 0, qy = 0;
        return quantize(x, qx) && quantize(y, qy) && contains(evaluate(qx, qy));
    }

    // 采样点命中时输出扣除 top-left 偏置的边函数值，未命中（或坐标非有限）返回 false。
    // 调用方用 out[i] / area() 就是重心坐标，不必再做一次浮点边函数——
    // 两个数学上等价的算法各算一遍是 MSAA 管线的主要浪费（实测占一半开销）。
    bool sampleInside(float x, float y, Values& out) const
    {
        int64_t qx = 0, qy = 0;
        if (!quantize(x, qx) || !quantize(y, qy))
            return false;
        const Values values = evaluate(qx, qy);
        if (!contains(values))
            return false;
        out = unbiased(values);
        return true;
    }
    static bool contains(const Values& values) { return (values[0] | values[1] | values[2]) >= 0; }

    // 三边无偏边函数之和恒为此值（量化单位），是重心坐标的分母。
    int64_t area() const { return area_; }

    // 扣除 top-left 规则的 -1 偏置。覆盖归属需要偏置，属性插值不需要：
    // 直接用带偏置的值算重心坐标会把这份偏置换算进深度与 UV。
    Values unbiased(const Values& values) const
    {
        return {values[0] + bias_[0], values[1] + bias_[1], values[2] + bias_[2]};
    }
    const std::array<Edge, 3>& edges() const { return edges_; }
    // 扫描游标仅用于 bounds() 返回的有限矩形（允许行/列结束后的一次步进）。
    // 不得对调用方构造的任意 Values 或无限次累计调用这些快速路径。
    void advanceX(Values& values) const
    {
        for (size_t i = 0; i < 3; ++i)
            values[i] += edges_[i].stepX();
    }
    void advanceY(Values& values) const
    {
        for (size_t i = 0; i < 3; ++i)
            values[i] += edges_[i].stepY();
    }

   private:
    TriangleCoverage() = default;
    static bool quantize(float value, int64_t& out)
    {
        if (!std::isfinite(value) || std::abs(value) > coordinateLimit)
            return false;
        // 已知范围后可直接截断：与 round 的中点远离零规则一致，避免每顶点的运行库调用。
        const double scaled = double(value) * subpixelScale;
        out = static_cast<int64_t>(scaled + (scaled < 0 ? -0.5 : 0.5));
        return true;
    }
    Values evaluate(int64_t x, int64_t y) const
    {
        Values values{};
        for (size_t i = 0; i < 3; ++i)
            values[i] = edges_[i].a * x + edges_[i].b * y + edges_[i].c;
        return values;
    }
    std::array<Edge, 3> edges_{};
    std::array<int64_t, 3> bias_{}; // 每边在 c 上附加的 top-left 偏置（0 或 1）
    int64_t area_ = 0;
    int64_t minX_ = 0, maxX_ = 0, minY_ = 0, maxY_ = 0;
};

// ============================================================
// 两个公共件：所有“基于覆盖三角形的逐像素循环”都该走这里
//
// 【为何要抽出来】Renderer（着色管线）与 MSAAFlatRenderer（多样本）原本各写
// 一套主循环，两套里的权重映射（E0→v2 / E1→v0 / E2→v1）也各算一遍。
// 这种重复的危险不在代码量，而在“改一处忘另一处”：覆盖规则或权重映射只要
// 两边不一致，就会出现“普通渲染对、开 MSAA 不对”这种很难查的现象。
//
// 【性能】两个回调都是模板参数，会在调用点内联展开；onRow 专供调用方
// 在行首缓存行指针（不必逐像素重算 row(y)），所以热路径上没有额外间接。
// ============================================================

// 边函数值 → 重心坐标。三处调用点共用同一份映射，避免各写一遍、各错一次。
inline Vec3f barycentricFromValues(const TriangleCoverage &coverage, const TriangleCoverage::Values &values,
                                   double invArea)
{
    const TriangleCoverage::Values raw = coverage.unbiased(values);
    return {float(double(raw[1]) * invArea), float(double(raw[2]) * invArea), float(double(raw[0]) * invArea)};
}

// 遍历包围盒内每个像素：onRow(y) 在每行开头调用一次，onPixel(x, y, values) 逐像素调用。
// 调用方负责先自行判定 bounds 是否为空。
template <class OnRow, class OnPixel>
inline void forEachCoveredPixel(const TriangleCoverage &coverage, const RasterBounds &bounds, OnRow &&onRow,
                                OnPixel &&onPixel)
{
    auto coverageRow = coverage.atPixel(bounds.minX, bounds.minY);
    for (int y = bounds.minY; y <= bounds.maxY; ++y)
    {
        onRow(y);
        auto values = coverageRow;
        for (int x = bounds.minX; x <= bounds.maxX; ++x)
        {
            onPixel(x, y, values);
            coverage.advanceX(values);
        }
        coverage.advanceY(coverageRow);
    }
}

}  // namespace sr
