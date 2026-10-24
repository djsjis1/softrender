#pragma once
#include <cstddef>
#include <algorithm>
#include <cmath>
#include <vector>

#include "softrenderer/core/framebuffer.h"
#include "softrenderer/core/msaa_framebuffer.h"
#include "softrenderer/render/raster/coverage.h"
#include "softrenderer/render/raster/triangle.h"
#include "softrenderer/math/barycentric.h"

namespace sr {
// 常量颜色的教学 MSAA 管线；每个样本的颜色必须与其深度在同一分支提交。
template <int Samples>
class MSAAFlatRenderer
{
    static_assert(Samples == 1 || Samples == 4, "only center or rotated 4x sampling is supported");

   public:
    MSAAFlatRenderer(int width, int height) : buffer_(width, height) {}
    void clear(Color background) { buffer_.clear(background); }
    void rasterize(const Vec2f& p0, const Vec2f& p1, const Vec2f& p2, float z0, float z1, float z2, Color color)
    {
        const auto coverage = TriangleCoverage::create(p0, p1, p2);
        if (!coverage || !std::isfinite(z0) || !std::isfinite(z1) || !std::isfinite(z2))
            return;
        if (std::abs(cross2(p1 - p0, p2 - p0)) < 1e-8f)
            return;
        const auto bounds = coverage->bounds(buffer_.width(), buffer_.height());
        for (int y = bounds.minY; y <= bounds.maxY; ++y)
            for (int x = bounds.minX; x <= bounds.maxX; ++x)
            {
                for (int sample = 0; sample < Samples; ++sample)
                {
                    float sx = x + 0.5f, sy = y + 0.5f;
                    if constexpr (Samples == 4)
                    {
                        sx += kMSAA4Offsets[sample].first;
                        sy += kMSAA4Offsets[sample].second;
                    }
                    if (!coverage->containsSample(sx, sy))
                        continue;
                    const Vec3f bc = sr::barycentric(p0, p1, p2, {sx, sy});
                    const float z = bc.x * z0 + bc.y * z1 + bc.z * z2;
                    if (buffer_.testDepth(x, y, sample, z))
                        buffer_.setSample(x, y, sample, color);
                }
            }
    }
    void resolveTo(Framebuffer& destination) const
    {
        std::vector<Color> pixels;
        buffer_.resolve(pixels);
        for (int y = 0; y < std::min(buffer_.height(), destination.height()); ++y)
            for (int x = 0; x < std::min(buffer_.width(), destination.width()); ++x)
                destination.setPixel(x, y, pixels[size_t(y) * buffer_.width() + x]);
    }

   private:
    MSAAFramebuffer<Samples> buffer_;
};
}  // namespace sr
