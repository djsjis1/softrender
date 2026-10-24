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
        }
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
    static bool contains(const Values& values) { return (values[0] | values[1] | values[2]) >= 0; }
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
    int64_t minX_ = 0, maxX_ = 0, minY_ = 0, maxY_ = 0;
};
}  // namespace sr
