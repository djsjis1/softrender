// ============================================================
// render 模块单元测试：裁剪、渲染队列与快速光栅化
//
// render 不依赖 io/runtime/scene：本文件只用 core/resource/math 的头。
// ============================================================
#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "softrenderer/core/color.h"
#include "softrenderer/core/depthbuffer.h"
#include "softrenderer/core/framebuffer.h"
#include "softrenderer/math/vec.h"
#include "softrenderer/render/pipeline/clip.h"
#include "softrenderer/render/pipeline/render_queue.h"
#include "softrenderer/render/raster/rasterizer_fast.h"

using namespace sr;

TEST(Clip, FullyInside)
{
    ClipVertex tri[3];
    tri[0].pos = {0, 0, -1, 2};
    tri[1].pos = {0.5f, 0, -1, 2};
    tri[2].pos = {0, 0.5f, -1, 2};
    ClipVertex out[kMaxClipVerts];
    int n = clipTriangleFullFrustum(tri, out);
    EXPECT_EQ(n, 3);
}

TEST(Clip, FullyBehindFar)
{
    ClipVertex tri[3];
    tri[0].pos = {0, 0, 5, 1};  // w-z = -4 < 0
    tri[1].pos = {0, 0, 6, 1};
    tri[2].pos = {0, 0, 7, 1};
    ClipVertex out[kMaxClipVerts];
    int n = clipTriangleFullFrustum(tri, out);
    EXPECT_EQ(n, 0);
}

TEST(Clip, PartiallyClipped)
{
    // 一个顶点在 near 外（w+z < 0）
    ClipVertex tri[3];
    tri[0].pos = {0, 0, -3, 1};  // w+z = -2 < 0 → outside near
    tri[1].pos = {0, 0, -1, 2};  // w+z = 1 > 0 → inside
    tri[2].pos = {0, 0, -1, 2};  // inside
    ClipVertex out[kMaxClipVerts];
    int n = clipTriangleFullFrustum(tri, out);
    EXPECT_GE(n, 3);
    EXPECT_LE(n, 4);
}

// ==================== 深度缓冲 ====================

TEST(RenderQueue, OpaqueSortedNearToFar)
{
    RenderQueue q;
    DrawItem a;
    a.layer = RenderLayer::Opaque;
    a.objectIndex = 0;
    a.materialId = 1;
    DrawItem b;
    b.layer = RenderLayer::Opaque;
    b.objectIndex = 1;
    b.materialId = 1;
    DrawItem c;
    c.layer = RenderLayer::Opaque;
    c.objectIndex = 2;
    c.materialId = 1;
    q.add(c);
    q.add(a);
    q.add(b);
    std::vector<Vec3f> positions = {{0, 0, 10}, {0, 0, 2}, {0, 0, 5}};
    q.sort({0, 0, 0}, positions);
    EXPECT_EQ(q.items()[0].objectIndex, 1);
    EXPECT_EQ(q.items()[1].objectIndex, 2);
    EXPECT_EQ(q.items()[2].objectIndex, 0);
}

TEST(RenderQueue, TransparentSortedFarToNear)
{
    RenderQueue q;
    DrawItem a;
    a.layer = RenderLayer::Transparent;
    a.objectIndex = 0;
    DrawItem b;
    b.layer = RenderLayer::Transparent;
    b.objectIndex = 1;
    q.add(a);
    q.add(b);
    std::vector<Vec3f> positions = {{0, 0, 2}, {0, 0, 10}};
    q.sort({0, 0, 0}, positions);
    EXPECT_EQ(q.items()[0].objectIndex, 1);
    EXPECT_EQ(q.items()[1].objectIndex, 0);
}

TEST(RenderQueue, LayerPriority)
{
    RenderQueue q;
    DrawItem t;
    t.layer = RenderLayer::Transparent;
    t.objectIndex = 0;
    DrawItem o;
    o.layer = RenderLayer::Opaque;
    o.objectIndex = 1;
    q.add(t);
    q.add(o);
    std::vector<Vec3f> positions = {{0, 0, 1}, {0, 0, 1}};
    q.sort({0, 0, 0}, positions);
    EXPECT_EQ(q.items()[0].layer, RenderLayer::Opaque);
    EXPECT_EQ(q.items()[1].layer, RenderLayer::Transparent);
}

// ==================== Arena 内存池 ====================

TEST(RasterizerFast, IncrementalFillsTriangle)
{
    Framebuffer fb(32, 32, Color(0, 0, 0));
    DepthBuffer zb(32, 32);
    Vec2f v0{4, 4}, v1{28, 4}, v2{16, 28};
    raster::fillTriangleIncremental(fb, zb, v0, v1, v2, 0.5f, 0.5f, 0.5f, Color(255, 255, 255));
    // 中心像素应在三角形内
    Color center = fb.getPixel(16, 12);
    EXPECT_EQ(center.r, 255);
    // 角落像素应在三角形外
    Color corner = fb.getPixel(0, 0);
    EXPECT_EQ(corner.r, 0);
}

TEST(RasterizerFast, SSE2MatchesScalar)
{
    Framebuffer fb1(64, 64, Color(0, 0, 0));
    Framebuffer fb2(64, 64, Color(0, 0, 0));
    DepthBuffer zb1(64, 64), zb2(64, 64);
    Vec2f v0{10, 5}, v1{55, 10}, v2{30, 58};
    raster::fillTriangleIncremental(fb1, zb1, v0, v1, v2, 0.5f, 0.5f, 0.5f, Color(200, 100, 50));
#if SR_HAS_SSE2
    raster::fillTriangleSSE2(fb2, zb2, v0, v1, v2, 0.5f, 0.5f, 0.5f, Color(200, 100, 50));
#else
    raster::fillTriangleIncremental(fb2, zb2, v0, v1, v2, 0.5f, 0.5f, 0.5f, Color(200, 100, 50));
#endif
    // 两帧缓冲应完全相同
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
        {
            EXPECT_EQ(fb1.getPixel(x, y).r, fb2.getPixel(x, y).r);
            EXPECT_EQ(fb1.getPixel(x, y).g, fb2.getPixel(x, y).g);
            EXPECT_EQ(fb1.getPixel(x, y).b, fb2.getPixel(x, y).b);
        }
}

// 用面积法独立计算重心坐标，不复用被测代码的边函数实现。
// 为什么要独立算：若用被测代码自己的边函数推期望值，权重映射错位时
// 两边会一起错，测试就失去了发现能力。
// 返回 {w0, w1, w2}，分别为 v0 / v1 / v2 的权重，和为 1。
static Vec3f barycentricByArea(const Vec2f &a, const Vec2f &b, const Vec2f &c, const Vec2f &p)
{
    float denom = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    float w0 = ((b.y - c.y) * (p.x - c.x) + (c.x - b.x) * (p.y - c.y)) / denom;
    float w1 = ((c.y - a.y) * (p.x - c.x) + (a.x - c.x) * (p.y - c.y)) / denom;
    return Vec3f{w0, w1, 1.0f - w0 - w1};
}

// 深度插值必须用「非平坦深度」测试。
// 三个顶点深度全相等时，任何权重排列都算出同一个值，无法区分正确实现
// 与「重心权重错位」的实现——本项目的 RasterizerFast 套件曾因此漏掉真实 bug。

TEST(RasterizerFast, fillTriangleIncremental_slopeDepthMatchesBarycentric)
{
    constexpr int kSize = 32;
    Framebuffer fb(kSize, kSize, Color(0, 0, 0));
    DepthBuffer zb(kSize, kSize);

    const Vec2f v0{4, 4}, v1{28, 4}, v2{16, 28};
    // 三个深度互不相等，且差异足够大，使权重错位的误差远超浮点容差
    constexpr float z0 = 0.1f, z1 = 0.5f, z2 = 0.9f;
    raster::fillTriangleIncremental(fb, zb, v0, v1, v2, z0, z1, z2, Color(255, 255, 255));

    // 采样点取「靠近各顶点」的位置，而非几何重心 (16,12)。
    // 重心处 w0=w1=w2=1/3，正确映射与错位映射都得到 (0.1+0.5+0.9)/3 = 0.5，
    // 是对称盲点，测不出问题。
    struct Sample
    {
        int x, y;
        const char *what;
    };
    const Sample samples[] = {
        {6, 5, "靠近 v0"},
        {25, 5, "靠近 v1"},
        {16, 24, "靠近 v2"},
        {16, 12, "几何重心（对称盲点，仅作补充）"},
    };

    for (const Sample &s : samples)
    {
        // 像素中心而非整数坐标：光栅化按 (x+0.5, y+0.5) 判定
        const Vec2f pixelCenter{s.x + 0.5f, s.y + 0.5f};
        const Vec3f w = barycentricByArea(v0, v1, v2, pixelCenter);

        // 前置校验：采样点必须真的落在三角形内，否则测的是「未绘制像素」
        ASSERT_GT(w.x, 0.0f) << s.what << " 不在三角形内";
        ASSERT_GT(w.y, 0.0f) << s.what << " 不在三角形内";
        ASSERT_GT(w.z, 0.0f) << s.what << " 不在三角形内";

        const float expectedZ = w.x * z0 + w.y * z1 + w.z * z2;
        EXPECT_NEAR(zb.get(s.x, s.y), expectedZ, 1e-4f)
            << s.what << "：深度插值权重映射错误（w0=" << w.x << " w1=" << w.y << " w2=" << w.z << "）";
    }
}

// 顺时针绕序的三角形必须同样被光栅化。
// setupEdges 若不做符号归一化，area2<0 时 invArea 为负且 e 值未翻转，
// 内点判定 e0>=0 && e1>=0 && e2>=0 永不成立 → 三角形被静默丢弃。

TEST(RasterizerFast, fillTriangleFast_clockwiseWinding_stillRasterizes)
{
    constexpr int kSize = 32;
    Framebuffer fbCCW(kSize, kSize, Color(0, 0, 0));
    Framebuffer fbCW(kSize, kSize, Color(0, 0, 0));
    DepthBuffer zbCCW(kSize, kSize), zbCW(kSize, kSize);

    // 同一个几何三角形，两种绕序。交换 v1/v2 时必须同步交换 z1/z2，
    // 否则比较的不是「同一个三角形」。
    const Vec2f v0{4, 4};
    constexpr float zAtV0 = 0.1f, zAtRight = 0.5f, zAtTop = 0.9f;

    // 逆时针：v0 → 右下 → 上方
    raster::fillTriangleFast(fbCCW, zbCCW, v0, Vec2f{28, 4}, Vec2f{16, 28},
                                  zAtV0, zAtRight, zAtTop, Color(255, 255, 255));
    // 顺时针：v0 → 上方 → 右下（顶点顺序反转，深度随顶点一起反转）
    raster::fillTriangleFast(fbCW, zbCW, v0, Vec2f{16, 28}, Vec2f{28, 4},
                                  zAtV0, zAtTop, zAtRight, Color(255, 255, 255));

    // 先确认逆时针版本确实画出了东西，避免「两边都是空图」的假通过
    int drawnCCW = 0;
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x)
            if (fbCCW.getPixel(x, y).r == 255)
                ++drawnCCW;
    ASSERT_GT(drawnCCW, 100) << "逆时针版本未画出三角形，测试前提不成立";

    // 颜色与深度都应逐像素一致
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x)
        {
            EXPECT_EQ(fbCCW.getPixel(x, y).r, fbCW.getPixel(x, y).r)
                << "像素 (" << x << "," << y << ") 颜色不一致：顺时针绕序被静默丢弃";
            EXPECT_NEAR(zbCCW.get(x, y), zbCW.get(x, y), 1e-5f)
                << "像素 (" << x << "," << y << ") 深度不一致";
        }
}

// SSE2 版与标量版必须在「非平坦深度」下一致。
// 原用例三个顶点深度全取 0.5f，两版即使权重映射同样错误也会一致，测不出问题。

TEST(RasterizerFast, fillTriangleSSE2_slopeDepthMatchesScalar)
{
    constexpr int kSize = 64;
    Framebuffer fb1(kSize, kSize, Color(0, 0, 0));
    Framebuffer fb2(kSize, kSize, Color(0, 0, 0));
    DepthBuffer zb1(kSize, kSize), zb2(kSize, kSize);

    const Vec2f v0{10, 5}, v1{55, 10}, v2{30, 58};
    constexpr float z0 = 0.15f, z1 = 0.55f, z2 = 0.85f;

    raster::fillTriangleIncremental(fb1, zb1, v0, v1, v2, z0, z1, z2, Color(200, 100, 50));
#if SR_HAS_SSE2
    raster::fillTriangleSSE2(fb2, zb2, v0, v1, v2, z0, z1, z2, Color(200, 100, 50));
#else
    raster::fillTriangleIncremental(fb2, zb2, v0, v1, v2, z0, z1, z2, Color(200, 100, 50));
#endif

    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x)
        {
            EXPECT_EQ(fb1.getPixel(x, y).r, fb2.getPixel(x, y).r);
            EXPECT_EQ(fb1.getPixel(x, y).g, fb2.getPixel(x, y).g);
            EXPECT_EQ(fb1.getPixel(x, y).b, fb2.getPixel(x, y).b);
            // 深度也要比：这是原用例缺失的关键断言
            EXPECT_NEAR(zb1.get(x, y), zb2.get(x, y), 1e-5f)
                << "像素 (" << x << "," << y << ") 深度不一致";
        }
}


