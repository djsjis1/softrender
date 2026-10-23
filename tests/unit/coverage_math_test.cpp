#include <gtest/gtest.h>

#include <climits>
#include <limits>

#include "softrenderer/render/raster/coverage.h"
#include "softrenderer/render/pipeline/msaa_renderer.h"
#include "softrenderer/render/raster/rasterizer_fast.h"

using namespace sr;

namespace sr::tests {
TEST(SubpixelCoverage, contains_sharedRectangle_ownsEachSampleOnceInEitherWinding)
{
    for (bool firstReverse : {false, true})
        for (bool secondReverse : {false, true})
        {
            const Vec2f a{1.5f, 1.5f}, b{7.5f, 1.5f}, c{7.5f, 7.5f}, d{1.5f, 7.5f};
            const auto first = TriangleCoverage::create(a, firstReverse ? c : b, firstReverse ? b : c);
            const auto second = TriangleCoverage::create(a, secondReverse ? d : c, secondReverse ? c : d);
            ASSERT_TRUE(first);
            ASSERT_TRUE(second);
            for (int y = 0; y < 9; ++y)
                for (int x = 0; x < 9; ++x)
                {
                    const int hits = int(first->containsSample(x + 0.5f, y + 0.5f)) +
                                     int(second->containsSample(x + 0.5f, y + 0.5f));
                    // y 向上：左/顶含边界，右/底不含。独立矩形期望，不复用被测边方程。
                    EXPECT_EQ(hits, int(x >= 1 && x < 7 && y > 1 && y <= 7)) << x << ',' << y;
                }
        }
}
TEST(SubpixelCoverage, contains_fractionalQuad_coversMSAASamplesWithoutOverlap)
{
    const auto first = TriangleCoverage::create({-0.125f, -0.375f}, {7.875f, -0.375f}, {7.875f, 5.625f});
    const auto second = TriangleCoverage::create({-0.125f, -0.375f}, {7.875f, 5.625f}, {-0.125f, 5.625f});
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    for (int y = -1; y < 8; ++y)
        for (int x = -1; x < 10; ++x)
            for (const auto& offset : kMSAA4Offsets)
            {
                const float sx = x + 0.5f + offset.first, sy = y + 0.5f + offset.second;
                const int hits = int(first->containsSample(sx, sy)) + int(second->containsSample(sx, sy));
                EXPECT_EQ(hits, int(sx >= -0.125f && sx < 7.875f && sy > -0.375f && sy <= 5.625f));
            }
}
TEST(SubpixelCoverage, create_invalidAndCollapsedGeometry_returnsEmpty)
{
    for (float invalid : {NAN, INFINITY, -INFINITY, 2.0f * TriangleCoverage::coordinateLimit})
        EXPECT_FALSE(TriangleCoverage::create({invalid, 0}, {0, 2}, {2, 0}));
    EXPECT_FALSE(TriangleCoverage::create({0, 0}, {1, 1}, {2, 2}));
    EXPECT_FALSE(TriangleCoverage::create({0, 0}, {0.0001f, 0}, {0, 0.0001f}));
}
TEST(SubpixelCoverage, contains_coordinateLimit_doesNotOverflow)
{
    constexpr float limit = float(TriangleCoverage::coordinateLimit);
    const auto coverage = TriangleCoverage::create({-limit, -limit}, {limit, -limit}, {-limit, limit});
    ASSERT_TRUE(coverage);
    EXPECT_TRUE(coverage->containsSample(-1, -1));
    EXPECT_FALSE(coverage->containsSample(1, 1));
    EXPECT_FALSE(TriangleCoverage::contains(coverage->atPixel(std::numeric_limits<int>::max(), 0)));
}
TEST(SubpixelCoverage, advance_differentScanOrigins_producesIdenticalEdges)
{
    const auto coverage = TriangleCoverage::create({-0.123f, 0.456f}, {90.987f, 10.654f}, {2.321f, 60.789f});
    ASSERT_TRUE(coverage);
    auto row = coverage->atPixel(0, 0);
    for (int y = 0; y < 64; ++y)
    {
        auto pixel = row;
        for (int x = 0; x < 96; ++x)
        {
            EXPECT_EQ(pixel, coverage->atPixel(x, y));
            coverage->advanceX(pixel);
        }
        coverage->advanceY(row);
    }
}
TEST(RasterCoverage, fillTriangleFast_scalarAndSIMD_agreeAcrossTailsAndWinding)
{
    for (int width = 1; width <= 17; ++width)
        for (bool reverse : {false, true})
        {
            Framebuffer scalar(width, 12), fast(width, 12);
            DepthBuffer scalarZ(width, 12), fastZ(width, 12);
            const Vec2f a{-0.125f, 0.375f}, b{float(width) + 5.25f, 1.125f}, c{2.875f, 10.75f};
            raster::fillTriangleIncremental(scalar, scalarZ, a, reverse ? c : b, reverse ? b : c, 0.1f,
                                                 reverse ? 0.3f : 0.7f, reverse ? 0.7f : 0.3f, sr::colors::red);
            raster::fillTriangleFast(fast, fastZ, a, reverse ? c : b, reverse ? b : c, 0.1f, reverse ? 0.3f : 0.7f,
                                          reverse ? 0.7f : 0.3f, sr::colors::red);
            for (int y = 0; y < 12; ++y)
                for (int x = 0; x < width; ++x)
                {
                    ASSERT_EQ(scalar.getPixel(x, y).r, fast.getPixel(x, y).r) << width << ',' << x << ',' << y;
                    EXPECT_NEAR(scalarZ.get(x, y), fastZ.get(x, y), 1e-6f);
                }
        }
}
TEST(RasterCoverage, fillTriangle_sharedDiagonal_isDrawOrderIndependent)
{
    for (bool fast : {false, true})
    {
        Framebuffer first(8, 8), second(8, 8);
        DepthBuffer za(8, 8), zb(8, 8);
        auto draw = [&](Framebuffer& fb, DepthBuffer& depth, bool upper) {
            const Vec2f a{0, 0}, b = upper ? Vec2f{8, 8} : Vec2f{8, 0}, c = upper ? Vec2f{0, 8} : Vec2f{8, 8};
            if (fast)
                raster::fillTriangleFast(fb, depth, a, b, c, 0.3f, 0.3f, 0.3f, upper ? sr::colors::blue : sr::colors::red);
            else
                fillTriangle(fb, a, b, c, upper ? sr::colors::blue : sr::colors::red);
        };
        draw(first, za, false);
        draw(first, za, true);
        draw(second, zb, true);
        draw(second, zb, false);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
            {
                const auto a = first.getPixel(x, y), b = second.getPixel(x, y);
                EXPECT_EQ(a.r, b.r);
                EXPECT_EQ(a.b, b.b);
                EXPECT_EQ(int(a.r) + a.b, 255);
            }
    }
}
TEST(RasterCoverage, fillTriangle_invalidAndSmallerDepth_doesNotWriteOutside)
{
    Framebuffer fb(4, 4);
    DepthBuffer depth(1, 1);
    raster::fillTriangleFast(fb, depth, {-2, -2}, {8, -2}, {-2, 8}, 0.1f, 0.2f, 0.3f, sr::colors::red);
    EXPECT_EQ(fb.getPixel(0, 0).r, 255);
    EXPECT_EQ(fb.getPixel(1, 0).r, 0);
    for (float invalid : {NAN, INFINITY, 1e30f})
    {
        fillTriangle(fb, {invalid, 0}, {1, 0}, {0, 1}, sr::colors::blue);
        fillTriangleGradient(fb, {invalid, 0}, {1, 0}, {0, 1}, sr::colors::red, sr::colors::green, sr::colors::blue);
        raster::fillTriangleFast(fb, depth, {invalid, 0}, {1, 0}, {0, 1}, 0.1f, 0.2f, 0.3f, sr::colors::blue);
    }
    EXPECT_EQ(fb.getPixel(0, 0).r, 255);
}
TEST(MSAACoverage, buffer_invalidDimensionsAndSampleIndices_areRejected)
{
    EXPECT_THROW((MSAAFramebuffer<4>(-1, 1)), std::invalid_argument);
    EXPECT_THROW((MSAAFramebuffer<4>(0, 1)), std::invalid_argument);
    EXPECT_THROW((MSAAFramebuffer<4>(INT_MAX, INT_MAX)), std::length_error);
    MSAAFramebuffer<4> buffer(1, 1);
    buffer.clear(sr::colors::black);
    EXPECT_FALSE(buffer.testDepth(0, 0, -1, 0.1f));
    EXPECT_FALSE(buffer.testDepth(0, 0, 4, 0.1f));
    EXPECT_FALSE(buffer.testDepth(0, 0, 0, NAN));
    buffer.setSample(0, 0, 4, sr::colors::red);
    buffer.setSample(0, 0, -1, sr::colors::red);
    EXPECT_EQ(buffer.coverageCount(-1, 0), 0);
    EXPECT_EQ(buffer.coverageCount(1, 0), 0);
    std::vector<Color> output;
    buffer.resolve(output);
    EXPECT_EQ(output[0].r, 0);
}
TEST(MSAACoverage, rasterize_partialOcclusion_isOrderIndependent)
{
    for (bool backgroundFirst : {false, true})
    {
        MSAAFlatRenderer<4> renderer(1, 1);
        renderer.clear(sr::colors::black);
        auto background = [&] { renderer.rasterize({-2, -2}, {4, -2}, {-2, 4}, 0.6f, 0.8f, 0.9f, sr::colors::blue); };
        if (backgroundFirst)
            background();
        renderer.rasterize({0.75f, 0.5f}, {1, 0.5f}, {1, 1}, 0.1f, 0.2f, 0.3f, sr::colors::red);
        if (!backgroundFirst)
            background();
        Framebuffer fb(1, 1);
        renderer.resolveTo(fb);
        EXPECT_EQ(fb.getPixel(0, 0).r, 64);
        EXPECT_EQ(fb.getPixel(0, 0).b, 191);
    }
}
}  // namespace sr::tests
