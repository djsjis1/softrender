// ============================================================
// 核心算法单元测试（Google Test）
//
// 覆盖：向量运算、矩阵变换、重心坐标、裁剪、深度缓冲、纹理采样
// 运行：cmake --build build --target unit_tests && ctest --test-dir build
// ============================================================
#include <gtest/gtest.h>

#include <entt/entt.hpp>

#include "core/arena.h"
#include "core/benchmark.h"
#include "core/blend.h"
#include "core/command_buffer.h"
#include "core/depthbuffer.h"
#include "core/framebuffer.h"
#include "core/gbuffer.h"
#include "core/resource_manager.h"
#include "math/mat3.h"
#include "math/mat4.h"
#include "math/quat.h"
#include "math/rect.h"
#include "math/transform.h"
#include "math/transform2d.h"
#include "math/vec.h"
#include "raster/blit2d.h"
#include "raster/clip.h"
#include "raster/rasterizer_fast.h"
#include "raster/renderer2d.h"
#include "raster/shape2d.h"
#include "raster/triangle.h"
#include "scene/camera.h"
#include "scene/components.h"
#include "scene/cubemap.h"
#include "scene/image_loader.h"
#include "scene/mesh.h"
#include "scene/particle_system.h"
#include "scene/render_queue.h"
#include "scene/skeleton.h"
#include "scene/texture.h"

// ==================== 向量 ====================

TEST(Vec, DotOrthogonal)
{
    Vec3f a{1, 0, 0}, b{0, 1, 0};
    EXPECT_NEAR(dot(a, b), 0.0f, 1e-6f);
}

TEST(Vec, DotParallel)
{
    Vec3f a{0, 3, 0}, b{0, 5, 0};
    EXPECT_NEAR(dot(a, b), 15.0f, 1e-6f);
}

TEST(Vec, CrossPerpendicular)
{
    Vec3f a{1, 0, 0}, b{0, 1, 0};
    Vec3f c = cross(a, b);
    EXPECT_NEAR(c.x, 0.0f, 1e-6f);
    EXPECT_NEAR(c.y, 0.0f, 1e-6f);
    EXPECT_NEAR(c.z, 1.0f, 1e-6f);
}

TEST(Vec, NormalizeLength)
{
    Vec3f v{3, 4, 0};
    Vec3f n = v.normalized();
    EXPECT_NEAR(n.length(), 1.0f, 1e-5f);
}

TEST(Vec, LerpMidpoint)
{
    Vec3f a{0, 0, 0}, b{2, 4, 6};
    Vec3f m = lerp(a, b, 0.5f);
    EXPECT_NEAR(m.x, 1.0f, 1e-6f);
    EXPECT_NEAR(m.y, 2.0f, 1e-6f);
    EXPECT_NEAR(m.z, 3.0f, 1e-6f);
}

TEST(Vec, ReflectMirror)
{
    // 入射 (1,-1,0) 打在法线 (0,1,0) 上，反射应为 (1,1,0)
    Vec3f I = normalize(Vec3f{1, -1, 0});
    Vec3f N{0, 1, 0};
    Vec3f R = reflect(I, N);
    EXPECT_NEAR(R.x, I.x, 1e-5f);
    EXPECT_NEAR(R.y, -I.y, 1e-5f);
    EXPECT_NEAR(R.z, 0.0f, 1e-5f);
}

TEST(Vec, Distance)
{
    Vec3f a{0, 0, 0}, b{3, 4, 0};
    EXPECT_NEAR(distance(a, b), 5.0f, 1e-5f);
    EXPECT_NEAR(distanceSq(a, b), 25.0f, 1e-5f);
}

TEST(Vec, ClampMinMax)
{
    Vec3f v{-1, 0.5f, 2};
    Vec3f c = clamp(v, 0.0f, 1.0f);
    EXPECT_NEAR(c.x, 0.0f, 1e-6f);
    EXPECT_NEAR(c.y, 0.5f, 1e-6f);
    EXPECT_NEAR(c.z, 1.0f, 1e-6f);
    Vec3f lo = min(Vec3f{1, 5, 3}, Vec3f{2, 4, 3});
    EXPECT_NEAR(lo.y, 4.0f, 1e-6f);
    Vec3f hi = max(Vec3f{1, 5, 3}, Vec3f{2, 4, 3});
    EXPECT_NEAR(hi.x, 2.0f, 1e-6f);
}

TEST(Vec, ScalarLeftMultiply)
{
    Vec3f v{1, 2, 3};
    Vec3f r = 2.0f * v;  // 左乘
    EXPECT_NEAR(r.x, 2.0f, 1e-6f);
    EXPECT_NEAR(r.z, 6.0f, 1e-6f);
}

TEST(Vec, CompoundAssignment)
{
    Vec3f v{1, 1, 1};
    v += Vec3f{1, 2, 3};
    EXPECT_NEAR(v.y, 3.0f, 1e-6f);
    v *= 2.0f;
    EXPECT_NEAR(v.z, 8.0f, 1e-6f);
}

TEST(Vec, Equality)
{
    Vec3f a{1, 2, 3}, b{1, 2, 3}, c{1, 2, 4};
    EXPECT_TRUE(a == b);
    EXPECT_TRUE(a != c);
}

TEST(Vec, Vec2Normalized)
{
    Vec2f v{3, 4};
    Vec2f n = v.normalized();
    EXPECT_NEAR(n.length(), 1.0f, 1e-5f);
    EXPECT_NEAR(dot(v, v), 25.0f, 1e-5f);  // 2D 点积
}

TEST(Vec, GenericDot4D)
{
    // 通用 dot 无需重写即可支持 4D（利用编译期 size + operator[]）
    Vec4f a{1, 2, 3, 4}, b{5, 6, 7, 8};
    EXPECT_NEAR(dot(a, b), 70.0f, 1e-5f);
    // 通用 lerp / distance 同样支持 4D
    Vec4f m = lerp(a, b, 0.5f);
    EXPECT_NEAR(m.x, 3.0f, 1e-5f);
    EXPECT_NEAR(m.w, 6.0f, 1e-5f);
    EXPECT_NEAR(distance(a, b), 8.0f, 1e-5f);
}

TEST(Vec, Cross2Sign)
{
    Vec2f a{1, 0}, b{0, 1};
    EXPECT_GT(cross2(a, b), 0.0f);  // 逆时针 = 正
    EXPECT_LT(cross2(b, a), 0.0f);  // 顺时针 = 负
}

TEST(Vec, AddSubtract)
{
    Vec3f a{1, 2, 3}, b{4, 5, 6};
    Vec3f s = a + b;
    EXPECT_FLOAT_EQ(s.x, 5.0f);
    EXPECT_FLOAT_EQ(s.y, 7.0f);
    EXPECT_FLOAT_EQ(s.z, 9.0f);
    Vec3f d = b - a;
    EXPECT_FLOAT_EQ(d.x, 3.0f);
}

TEST(Vec, ScalarMultiply)
{
    Vec3f v{1, 2, 3};
    Vec3f r = v * 2.0f;
    EXPECT_FLOAT_EQ(r.x, 2.0f);
    EXPECT_FLOAT_EQ(r.y, 4.0f);
    EXPECT_FLOAT_EQ(r.z, 6.0f);
}

// ==================== 矩阵 ====================

TEST(Mat4, IdentityMultiply)
{
    Mat4 I = Mat4::identity();
    Vec4f v{1, 2, 3, 1};
    Vec4f r = I * v;
    EXPECT_NEAR(r.x, 1.0f, 1e-6f);
    EXPECT_NEAR(r.y, 2.0f, 1e-6f);
    EXPECT_NEAR(r.z, 3.0f, 1e-6f);
    EXPECT_NEAR(r.w, 1.0f, 1e-6f);
}

TEST(Mat4, TranslatePoint)
{
    Mat4 T = Mat4::translate(1, 2, 3);
    Vec3f p = T.transformPoint({0, 0, 0});
    EXPECT_NEAR(p.x, 1.0f, 1e-6f);
    EXPECT_NEAR(p.y, 2.0f, 1e-6f);
    EXPECT_NEAR(p.z, 3.0f, 1e-6f);
}

TEST(Mat4, TranslateDirectionUnchanged)
{
    Mat4 T = Mat4::translate(10, 20, 30);
    Vec3f d = T.transformDir({1, 0, 0});
    EXPECT_NEAR(d.x, 1.0f, 1e-6f);
    EXPECT_NEAR(d.y, 0.0f, 1e-6f);
    EXPECT_NEAR(d.z, 0.0f, 1e-6f);
}

TEST(Mat4, InverseIdentity)
{
    Mat4 I = Mat4::identity();
    Mat4 inv = I.inversed();
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            EXPECT_NEAR(inv.m[i][j], I.m[i][j], 1e-5f);
}

TEST(Mat4, InverseRoundtrip)
{
    Mat4 M = Mat4::translate(1, 2, 3) * Mat4::rotateY(0.5f) * Mat4::scale(2, 2, 2);
    Mat4 inv = M.inversed();
    Mat4 prod = M * inv;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            EXPECT_NEAR(prod.m[i][j], (i == j ? 1.0f : 0.0f), 1e-4f);
}

TEST(Mat4, RotateY90)
{
    Mat4 R = Mat4::rotateY(3.14159265f / 2.0f);
    Vec3f p = R.transformPoint({1, 0, 0});
    EXPECT_NEAR(p.x, 0.0f, 1e-4f);
    EXPECT_NEAR(p.z, -1.0f, 1e-4f);
}

// ==================== 变换 ====================

TEST(Transform, PerspectiveWEqualsNegZ)
{
    Mat4 P = transform::perspective(1.0f, 1.0f, 0.1f, 100.0f);
    Vec4f v{0, 0, -5, 1};
    Vec4f clip = P * v;
    EXPECT_NEAR(clip.w, 5.0f, 1e-5f);
}

TEST(Transform, LookAtEyeAtOrigin)
{
    Mat4 V = transform::lookAt({0, 0, 0}, {0, 0, -1}, {0, 1, 0});
    Vec3f r = V.transformPoint({0, 0, 0});
    EXPECT_NEAR(r.x, 0.0f, 1e-5f);
    EXPECT_NEAR(r.y, 0.0f, 1e-5f);
    EXPECT_NEAR(r.z, 0.0f, 1e-5f);
}

TEST(Transform, OrthoCenterMapsToZero)
{
    Mat4 O = transform::ortho(-5, 5, -5, 5, 0.1f, 100.0f);
    Vec4f center = O * Vec4f{0, 0, -50.05f, 1};  // z 中心
    EXPECT_NEAR(center.x, 0.0f, 1e-4f);
    EXPECT_NEAR(center.y, 0.0f, 1e-4f);
}

// ==================== 重心坐标 ====================

TEST(Barycentric, VertexReturnsUnit)
{
    Vec2f v0{0, 0}, v1{1, 0}, v2{0, 1};
    Vec3f bc = barycentric(v0, v1, v2, v0);
    EXPECT_NEAR(bc.x, 1.0f, 1e-5f);
    EXPECT_NEAR(bc.y, 0.0f, 1e-5f);
    EXPECT_NEAR(bc.z, 0.0f, 1e-5f);
}

TEST(Barycentric, Centroid)
{
    Vec2f v0{0, 0}, v1{3, 0}, v2{0, 3};
    Vec3f bc = barycentric(v0, v1, v2, {1, 1});
    EXPECT_NEAR(bc.x, 1.0f / 3.0f, 1e-4f);
    EXPECT_NEAR(bc.y, 1.0f / 3.0f, 1e-4f);
    EXPECT_NEAR(bc.z, 1.0f / 3.0f, 1e-4f);
}

TEST(Barycentric, OutsideNegative)
{
    Vec2f v0{0, 0}, v1{1, 0}, v2{0, 1};
    Vec3f bc = barycentric(v0, v1, v2, {2, 2});
    EXPECT_TRUE(bc.x < 0 || bc.y < 0 || bc.z < 0);
}

TEST(Barycentric, SumToOne)
{
    Vec2f v0{0, 0}, v1{4, 0}, v2{1, 3};
    Vec3f bc = barycentric(v0, v1, v2, {1.5f, 1.0f});
    EXPECT_NEAR(bc.x + bc.y + bc.z, 1.0f, 1e-5f);
}

// ==================== 裁剪 ====================

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

TEST(DepthBuffer, CloserWins)
{
    DepthBuffer db(4, 4);
    EXPECT_TRUE(db.testAndSet(1, 1, 0.5f));
    EXPECT_FALSE(db.testAndSet(1, 1, 0.8f));
    EXPECT_TRUE(db.testAndSet(1, 1, 0.3f));
}

TEST(DepthBuffer, OutOfBounds)
{
    DepthBuffer db(4, 4);
    EXPECT_FALSE(db.testAndSet(-1, 0, 0.5f));
    EXPECT_FALSE(db.testAndSet(0, 4, 0.5f));
}

// ==================== 纹理 ====================

TEST(Texture, MipmapGenerated)
{
    tga::Image img;
    img.width = 8;
    img.height = 8;
    img.pixels.resize(64, Color(128, 128, 128));
    Texture tex(img);
    EXPECT_GE(tex.mipLevels(), 4);  // 8→4→2→1
}

TEST(Texture, BilinearUniform)
{
    tga::Image img;
    img.width = 4;
    img.height = 4;
    img.pixels.resize(16, Color(200, 100, 50));
    Texture tex(img);
    Vec3f c = tex.sampleBilinear(0.3f, 0.7f);
    EXPECT_NEAR(c.x, 200.0f / 255.0f, 0.01f);
    EXPECT_NEAR(c.y, 100.0f / 255.0f, 0.01f);
    EXPECT_NEAR(c.z, 50.0f / 255.0f, 0.01f);
}

TEST(Texture, NearestReturnsExactTexel)
{
    tga::Image img;
    img.width = 2;
    img.height = 2;
    img.pixels = {Color(255, 0, 0), Color(0, 255, 0), Color(0, 0, 255), Color(255, 255, 0)};
    Texture tex(img);
    Vec3f c = tex.sampleNearest(0.0f, 0.0f);
    EXPECT_NEAR(c.x, 1.0f, 0.01f);
    EXPECT_NEAR(c.y, 0.0f, 0.01f);
}

// ==================== 网格 ====================

TEST(Mesh, CubeHas12Triangles)
{
    Mesh cube = meshgen::cube(1.0f);
    EXPECT_EQ(cube.triangleCount(), 12);
}

TEST(Mesh, SphereNormalsUnitLength)
{
    Mesh sphere = meshgen::sphere(1.0f, 16, 8);
    for (auto& v : sphere.vertices)
    {
        EXPECT_NEAR(v.normal.length(), 1.0f, 1e-4f);
    }
}

TEST(Mesh, PlaneGridVertexCount)
{
    Mesh plane = meshgen::planeGrid(10.0f, 4, 1.0f);
    // (4+1)*(4+1) = 25 顶点
    EXPECT_EQ(plane.vertices.size(), 25u);
    // 4*4*2 = 32 三角形
    EXPECT_EQ(plane.triangleCount(), 32);
}

TEST(Mesh, ComputeTangentsNonZero)
{
    Mesh cube = meshgen::cube(1.0f);
    computeTangents(cube);
    for (auto& v : cube.vertices)
    {
        EXPECT_GT(v.tangent.lengthSq(), 0.0f);
    }
}

// ==================== 渲染队列 ====================

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

TEST(Arena, BasicAlloc)
{
    sr::ArenaAllocator arena(1024);
    void* p = arena.alloc(64);
    ASSERT_NE(p, nullptr);
    EXPECT_GE(arena.used(), 64u);
}

TEST(Arena, Alignment)
{
    sr::ArenaAllocator arena(1024);
    void* p = arena.alloc(1, 16);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % 16, 0u);
}

TEST(Arena, Reset)
{
    sr::ArenaAllocator arena(1024);
    arena.alloc(512);
    EXPECT_GT(arena.used(), 0u);
    arena.reset();
    EXPECT_EQ(arena.used(), 0u);
}

TEST(Arena, Overflow)
{
    sr::ArenaAllocator arena(64);
    // 分配超过初始容量，应自动扩展
    void* p = arena.alloc(128);
    ASSERT_NE(p, nullptr);
}

// ==================== Alpha 混合 ====================

TEST(Blend, OpaqueReturnsSrc)
{
    Color src(100, 150, 200, 128);
    Color dst(10, 20, 30, 255);
    Color r = blendPixels(src, dst, BlendMode::Opaque);
    EXPECT_EQ(r.r, src.r);
    EXPECT_EQ(r.g, src.g);
    EXPECT_EQ(r.b, src.b);
}

TEST(Blend, AlphaBlendHalf)
{
    Color src(200, 0, 0, 128);  // ~50% alpha
    Color dst(0, 0, 200, 255);
    Color r = blendPixels(src, dst, BlendMode::AlphaBlend);
    // 结果应在 src 和 dst 之间
    EXPECT_GT(r.r, 0);
    EXPECT_GT(r.b, 0);
    EXPECT_LT(r.r, 200);
}

TEST(Blend, AdditiveClamps)
{
    Color src(200, 200, 200, 255);
    Color dst(200, 200, 200, 255);
    Color r = blendPixels(src, dst, BlendMode::Additive);
    EXPECT_EQ(r.r, 255);  // 应 clamp 到 255
}

// ==================== 快速光栅化器 ====================

TEST(RasterizerFast, IncrementalFillsTriangle)
{
    Framebuffer fb(32, 32, Color(0, 0, 0));
    DepthBuffer zb(32, 32);
    Vec2f v0{4, 4}, v1{28, 4}, v2{16, 28};
    raster_fast::fillTriangleIncremental(fb, zb, v0, v1, v2, 0.5f, 0.5f, 0.5f, Color(255, 255, 255));
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
    raster_fast::fillTriangleIncremental(fb1, zb1, v0, v1, v2, 0.5f, 0.5f, 0.5f, Color(200, 100, 50));
#if SR_HAS_SSE2
    raster_fast::fillTriangleSSE2(fb2, zb2, v0, v1, v2, 0.5f, 0.5f, 0.5f, Color(200, 100, 50));
#else
    raster_fast::fillTriangleIncremental(fb2, zb2, v0, v1, v2, 0.5f, 0.5f, 0.5f, Color(200, 100, 50));
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

// ==================== ECS (EnTT) ====================

TEST(ECS, CreateAndDestroy)
{
    entt::registry registry;
    auto e1 = registry.create();
    auto e2 = registry.create();
    EXPECT_TRUE(registry.valid(e1));
    EXPECT_TRUE(registry.valid(e2));
    registry.destroy(e1);
    EXPECT_FALSE(registry.valid(e1));
    EXPECT_TRUE(registry.valid(e2));
}

TEST(ECS, EmplaceAndGet)
{
    entt::registry registry;
    auto e = registry.create();
    registry.emplace<Position>(e, Vec3f{1, 2, 3});
    EXPECT_TRUE(registry.all_of<Position>(e));
    EXPECT_FALSE(registry.all_of<Velocity>(e));
    auto& pos = registry.get<Position>(e);
    EXPECT_FLOAT_EQ(pos.pos.x, 1.0f);
    EXPECT_FLOAT_EQ(pos.pos.y, 2.0f);
}

TEST(ECS, ViewSystem)
{
    entt::registry registry;
    for (int i = 0; i < 5; ++i)
    {
        auto e = registry.create();
        registry.emplace<Position>(e, Vec3f{float(i), 0, 0});
        registry.emplace<Velocity>(e, Vec3f{1, 0, 0});
    }
    // System: move
    registry.view<Position, Velocity>().each([](Position& p, Velocity& v) { p.pos = p.pos + v.vel; });
    // Verify: each position should be initial + 1
    int count = 0;
    registry.view<Position>().each([&](Position& p) {
        // 初始值是 0,1,2,3,4，加 1 后应该是 1,2,3,4,5
        EXPECT_GE(p.pos.x, 1.0f);
        EXPECT_LE(p.pos.x, 5.0f);
        ++count;
    });
    EXPECT_EQ(count, 5);
}

// ==================== GBuffer ====================

TEST(GBuffer, WriteAndRead)
{
    GBuffer gbuf;
    gbuf.resize(4, 4);
    EXPECT_TRUE(gbuf.write(1, 1, 0.5f, Vec3f{1, 2, 3}, Vec3f{0, 1, 0}, 0.3f, Vec3f{0.8f, 0.2f, 0.1f}, 0.0f));
    EXPECT_FALSE(gbuf.write(1, 1, 0.8f, Vec3f{0, 0, 0}, Vec3f{0, 1, 0}, 0.5f, Vec3f{1, 1, 1}, 1.0f));  // farther
    EXPECT_FLOAT_EQ(gbuf.depth[1 * 4 + 1], 0.5f);
    EXPECT_FLOAT_EQ(gbuf.worldPos[1 * 4 + 1].x, 1.0f);
}

// ==================== Cubemap ====================

TEST(Cubemap, SkyGeneration)
{
    Cubemap cube;
    generateSkyCubemap(cube, 16);
    EXPECT_TRUE(cube.valid());
    // Sample up direction should be zenith-ish (blue)
    Vec3f up = cube.sample(Vec3f{0, 1, 0});
    EXPECT_GT(up.z, up.x);  // blue > red for sky
}

// ==================== Skeleton ====================

TEST(Skeleton, SingleBoneIdentity)
{
    Skeleton skel;
    Bone b;
    b.name = "root";
    b.parent = -1;
    b.localTransform = Mat4::identity();
    b.inverseBindMatrix = Mat4::identity();
    skel.bones.push_back(b);
    auto skin = skel.computeSkinMatrices();
    EXPECT_EQ(skin.size(), 1u);
    // Identity * Identity = Identity
    Vec3f p = skin[0].transformPoint(Vec3f{1, 2, 3});
    EXPECT_NEAR(p.x, 1.0f, 1e-5f);
    EXPECT_NEAR(p.y, 2.0f, 1e-5f);
}

TEST(Skeleton, SkinPosition)
{
    VertexBoneWeights bw;
    bw.boneIndices[0] = 0;
    bw.weights[0] = 1.0f;
    std::vector<Mat4> skinMats = {Mat4::translate(5, 0, 0)};
    Vec3f result = skinPosition(Vec3f{1, 0, 0}, bw, skinMats);
    EXPECT_NEAR(result.x, 6.0f, 1e-5f);
}

// ==================== Benchmark ====================

TEST(Benchmark, TimerRecords)
{
    sr::Bench::instance().clear();
    {
        sr::Timer t("test_timer");
        volatile int x = 0;
        for (int i = 0; i < 1000; ++i)
            x += i;
    }
    auto& results = sr::Bench::instance().results();
    ASSERT_FALSE(results.empty());
    EXPECT_EQ(results.back().name, "test_timer");
    EXPECT_GT(results.back().totalUs, 0.0);
}

// ==================== 四元数 ====================

TEST(Quat, IdentityRotation)
{
    Quat q = Quat::identity();
    Vec3f v{1, 2, 3};
    Vec3f r = q.rotate(v);
    EXPECT_NEAR(r.x, 1.0f, 1e-5f);
    EXPECT_NEAR(r.y, 2.0f, 1e-5f);
    EXPECT_NEAR(r.z, 3.0f, 1e-5f);
}

TEST(Quat, Rotate90Y)
{
    Quat q = Quat::fromAxisAngle(Vec3f{0, 1, 0}, 3.14159265f / 2.0f);
    Vec3f r = q.rotate(Vec3f{1, 0, 0});
    EXPECT_NEAR(r.x, 0.0f, 1e-4f);
    EXPECT_NEAR(r.z, -1.0f, 1e-4f);
}

TEST(Quat, SlerpEndpoints)
{
    Quat a = Quat::identity();
    Quat b = Quat::fromAxisAngle(Vec3f{0, 1, 0}, 1.0f);
    Quat s0 = slerp(a, b, 0.0f);
    Quat s1 = slerp(a, b, 1.0f);
    EXPECT_NEAR(s0.w, a.w, 1e-4f);
    EXPECT_NEAR(s1.w, b.w, 1e-4f);
}

TEST(Quat, ToMat4Consistency)
{
    Quat q = Quat::fromAxisAngle(Vec3f{0, 0, 1}, 0.7f);
    Mat4 m = q.toMat4();
    Vec3f v{1, 0, 0};
    Vec3f r1 = q.rotate(v);
    Vec3f r2 = m.transformDir(v);
    EXPECT_NEAR(r1.x, r2.x, 1e-4f);
    EXPECT_NEAR(r1.y, r2.y, 1e-4f);
    EXPECT_NEAR(r1.z, r2.z, 1e-4f);
}

// ==================== 粒子系统 ====================

TEST(Particle, EmitAndUpdate)
{
    EmitterConfig cfg;
    cfg.rate = 100;
    cfg.maxParticles = 50;
    cfg.gravity = {0, 0, 0};
    ParticleSystem ps(cfg);
    ps.update(0.1f);  // 应发射 10 个
    EXPECT_EQ(ps.aliveCount(), 10);
    ps.update(1.0f);  // 再发射 100，但上限 50
    EXPECT_LE(ps.aliveCount(), 50);
}

TEST(Particle, Lifetime)
{
    EmitterConfig cfg;
    cfg.rate = 100;
    cfg.lifeMin = 0.1f;
    cfg.lifeMax = 0.1f;
    cfg.maxParticles = 50;
    ParticleSystem ps(cfg);
    ps.update(0.05f);  // rate*dt = 5，发射 5 个
    int before = ps.aliveCount();
    EXPECT_GT(before, 0);
    ps.update(0.2f);  // 超过生命周期 0.1s
    EXPECT_EQ(ps.aliveCount(), 0);
}

// ==================== 资源管理器 ====================

TEST(ResourceManager, CacheAndPurge)
{
    ResourceManager::instance().clear();
    // 注册一个资源
    auto tex = std::make_shared<Texture>();
    ResourceManager::instance().registerResource<Texture>("test", tex);
    EXPECT_EQ(ResourceManager::instance().cacheSize(), 1u);
    // 释放引用
    tex.reset();
    ResourceManager::instance().purge();
    EXPECT_EQ(ResourceManager::instance().cacheSize(), 0u);
}

// ==================== 命令缓冲 ====================

TEST(CommandBuffer, SequentialExecution)
{
    CommandBuffer cmd;
    int counter = 0;
    cmd.record([&]() { counter += 1; });
    cmd.record([&]() { counter += 10; });
    cmd.record([&]() { counter += 100; });
    cmd.executeSequential();
    EXPECT_EQ(counter, 111);
}

TEST(CommandBuffer, ParallelExecution)
{
    CommandBuffer cmd;
    std::atomic<int> counter{0};
    for (int i = 0; i < 100; ++i)
        cmd.record([&]() { counter.fetch_add(1); });
    cmd.executeParallel(4);
    EXPECT_EQ(counter.load(), 100);
}

TEST(ThreadPool, SubmitAndWait)
{
    ThreadPool pool(4);
    std::atomic<int> sum{0};
    for (int i = 0; i < 50; ++i)
        pool.submit([&]() { sum.fetch_add(1); });
    pool.waitAll();
    EXPECT_EQ(sum.load(), 50);
}

// ==================== Mat3（2D 仿射）====================

TEST(Mat3, IdentityKeepsPoint)
{
    Mat3 m = Mat3::identity();
    Vec2f p = m.transformPoint({3.0f, -2.0f});
    EXPECT_NEAR(p.x, 3.0f, 1e-5f);
    EXPECT_NEAR(p.y, -2.0f, 1e-5f);
}

TEST(Mat3, Translate)
{
    Mat3 m = Mat3::translate(5.0f, -3.0f);
    Vec2f p = m.transformPoint({1.0f, 1.0f});
    EXPECT_NEAR(p.x, 6.0f, 1e-5f);
    EXPECT_NEAR(p.y, -2.0f, 1e-5f);
}

TEST(Mat3, Rotate90CCW)
{
    Mat3 m = Mat3::rotate(3.14159265f * 0.5f);
    Vec2f p = m.transformPoint({1.0f, 0.0f});
    EXPECT_NEAR(p.x, 0.0f, 1e-5f);
    EXPECT_NEAR(p.y, 1.0f, 1e-5f);
}

TEST(Mat3, InverseRoundTrip)
{
    Mat3 m = Mat3::translate(4.0f, -2.0f) * Mat3::rotate(0.7f) * Mat3::scale(2.0f, 3.0f);
    Vec2f p{1.5f, -0.5f};
    Vec2f q = m.inversed().transformPoint(m.transformPoint(p));
    EXPECT_NEAR(q.x, p.x, 1e-4f);
    EXPECT_NEAR(q.y, p.y, 1e-4f);
}

TEST(Mat3, ToMat4AgreesOnXY)
{
    Mat3 m = Mat3::translate(3.0f, 5.0f) * Mat3::rotate(0.4f);
    Mat4 m4 = m.toMat4();
    Vec2f p2 = m.transformPoint({2.0f, 1.0f});
    Vec3f p3 = m4.transformPoint({2.0f, 1.0f, 0.0f});
    EXPECT_NEAR(p2.x, p3.x, 1e-4f);
    EXPECT_NEAR(p2.y, p3.y, 1e-4f);
    EXPECT_NEAR(p3.z, 0.0f, 1e-4f);
}

// ==================== Rect ====================

TEST(Rect, ContainsAndIntersect)
{
    Recti a(0, 0, 10, 10);
    EXPECT_TRUE(a.contains(5, 5));
    EXPECT_FALSE(a.contains(10, 5)); // 半开：右边界不含
    Recti b(5, 5, 10, 10);
    EXPECT_TRUE(a.intersects(b));
    Recti c(20, 20, 5, 5);
    EXPECT_FALSE(a.intersects(c));
}

TEST(Rect, IntersectionAndMerge)
{
    Recti a(0, 0, 10, 10), b(5, 5, 10, 10);
    Recti inter = a.intersection(b);
    EXPECT_EQ(inter.x, 5);
    EXPECT_EQ(inter.w, 5);
    Recti uni = a.merged(b);
    EXPECT_EQ(uni.x, 0);
    EXPECT_EQ(uni.right(), 15);
}

TEST(Rect, Center)
{
    Rectf r(0.0f, 0.0f, 4.0f, 6.0f);
    Vec2f c = r.center();
    EXPECT_NEAR(c.x, 2.0f, 1e-5f);
    EXPECT_NEAR(c.y, 3.0f, 1e-5f);
}

// ==================== 2D 投影与相机 ====================

TEST(Transform2D, Ortho2DMapsWorldToDevice)
{
    Mat3 o = transform::ortho2D(0.0f, 100.0f, 0.0f, 50.0f, 200, 100);
    Vec2f p = o.transformPoint({100.0f, 50.0f}); // 世界右上角
    EXPECT_NEAR(p.x, 200.0f, 1e-3f);
    EXPECT_NEAR(p.y, 100.0f, 1e-3f);
}

TEST(Camera2D, WorldScreenRoundTrip)
{
    Camera2D cam(800, 600);
    cam.position = {10.0f, -5.0f};
    cam.zoom = 2.0f;
    cam.rotation = 0.3f;
    Vec2f w{3.0f, 7.0f};
    Vec2f back = cam.screenToWorld(cam.worldToScreen(w));
    EXPECT_NEAR(back.x, w.x, 1e-3f);
    EXPECT_NEAR(back.y, w.y, 1e-3f);
}

TEST(Camera2D, CenterMapsToScreenCenter)
{
    Camera2D cam(800, 600);
    cam.position = {4.0f, 9.0f};
    Vec2f s = cam.worldToScreen(cam.position);
    EXPECT_NEAR(s.x, 400.0f, 1e-3f);
    EXPECT_NEAR(s.y, 300.0f, 1e-3f);
}

// ==================== 2D 图元 ====================

TEST(Shape2D, FillRectCoversArea)
{
    Framebuffer fb(20, 20, colors::black);
    sr2d::fillRect(fb, 5, 5, 4, 4, colors::red);
    EXPECT_EQ(fb.getPixel(5, 5).r, 255);
    EXPECT_EQ(fb.getPixel(8, 8).r, 255);
    EXPECT_EQ(fb.getPixel(9, 9).r, 0); // 越出矩形
    EXPECT_EQ(fb.getPixel(0, 0).r, 0);
}

TEST(Shape2D, FillCircleCenterAndOutside)
{
    Framebuffer fb(40, 40, colors::black);
    sr2d::fillCircle(fb, 20.0f, 20.0f, 5.0f, colors::green);
    EXPECT_EQ(fb.getPixel(20, 20).g, 255);
    EXPECT_EQ(fb.getPixel(30, 30).g, 0);
}

TEST(Shape2D, ClipRestrictsDrawing)
{
    Framebuffer fb(40, 40, colors::black);
    sr2d::ClipRect clip(0, 0, 20, 40); // 只允许左半屏
    sr2d::fillRect(fb, 10, 10, 20, 5, colors::red, BlendMode::Opaque, &clip);
    EXPECT_EQ(fb.getPixel(15, 12).r, 255); // 裁剪区内
    EXPECT_EQ(fb.getPixel(25, 12).r, 0);   // 裁剪区外被丢弃
}

TEST(Shape2D, FillPolygonCoversInterior)
{
    Framebuffer fb(40, 40, colors::black);
    std::vector<Vec2f> tri = {{5, 5}, {35, 5}, {20, 35}};
    sr2d::fillPolygon(fb, tri, colors::blue);
    EXPECT_EQ(fb.getPixel(20, 12).b, 255); // 三角形内部
    EXPECT_EQ(fb.getPixel(2, 38).b, 0);    // 外部
}

TEST(Shape2D, AlphaBlendMixes)
{
    Framebuffer fb(4, 4, Color(0, 0, 0));
    sr2d::fillRect(fb, 0, 0, 4, 4, Color(200, 200, 200, 128), BlendMode::AlphaBlend);
    Color c = fb.getPixel(1, 1);
    EXPECT_GT(c.r, 60);
    EXPECT_LT(c.r, 160); // 约一半混合
}

// ==================== 2D 位图合成 ====================

TEST(Blit2D, BlitImageCopies)
{
    tga::Image img;
    img.width = 4;
    img.height = 4;
    img.pixels.assign(16, Color(10, 200, 30));
    Framebuffer fb(10, 10, colors::black);
    sr2d::blitImage(fb, img, 3, 3);
    EXPECT_EQ(fb.getPixel(3, 3).g, 200);
    EXPECT_EQ(fb.getPixel(6, 6).g, 200);
    EXPECT_EQ(fb.getPixel(8, 8).g, 0);
}

TEST(Blit2D, TintScales)
{
    Color c = sr2d::tintPixel(Color(200, 100, 50, 255), Color(255, 0, 0, 255));
    EXPECT_EQ(c.r, 200);
    EXPECT_EQ(c.g, 0);
    EXPECT_EQ(c.b, 0);
}

// ==================== Renderer2D ====================

TEST(Renderer2D, FillRectThroughTransform)
{
    Framebuffer fb(60, 60, colors::black);
    Renderer2D r;
    r.begin(fb);
    r.setBlend(BlendMode::Opaque);
    r.setColor(colors::white);
    r.translate(20.0f, 10.0f);
    r.fillRect(0.0f, 0.0f, 10.0f, 10.0f);
    EXPECT_EQ(fb.getPixel(25, 15).r, 255); // 平移后落点
    EXPECT_EQ(fb.getPixel(5, 5).r, 0);
    r.end();
}

TEST(Renderer2D, ScissorClips)
{
    Framebuffer fb(60, 60, colors::black);
    Renderer2D r;
    r.begin(fb);
    r.setBlend(BlendMode::Opaque);
    r.setColor(colors::red);
    r.setScissor(Recti(0, 0, 30, 60));
    r.fillRect(20.0f, 20.0f, 30.0f, 10.0f);
    EXPECT_EQ(fb.getPixel(25, 25).r, 255); // 裁剪区内
    EXPECT_EQ(fb.getPixel(40, 25).r, 0);   // 裁剪区外
    r.end();
}

TEST(Renderer2D, PushPopRestoresColor)
{
    Framebuffer fb(4, 4);
    Renderer2D r;
    r.begin(fb);
    r.setColor(colors::red);
    r.push();
    r.setColor(colors::blue);
    EXPECT_EQ(r.color.b, 255);
    r.pop();
    EXPECT_EQ(r.color.r, 255); // 恢复
    EXPECT_EQ(r.color.b, 0);
    r.end();
}

TEST(Renderer2D, MeasureTextWidth)
{
    Renderer2D r;
    Vec2f m = r.measureText("ABC", 1.0f);
    EXPECT_NEAR(m.x, 6.0f * 3 - 1.0f, 1e-4f);
    EXPECT_NEAR(m.y, 7.0f, 1e-4f);
}

TEST(Renderer2D, TextDrawsPixels)
{
    Framebuffer fb(80, 20, colors::black);
    Renderer2D r;
    r.begin(fb);
    r.setBlend(BlendMode::Opaque);
    r.setColor(colors::white);
    r.text(2.0f, 4.0f, "A", 2.0f);
    int lit = 0;
    for (int y = 0; y < 20; ++y)
        for (int x = 0; x < 80; ++x)
            if (fb.getPixel(x, y).r > 0)
                ++lit;
    EXPECT_GT(lit, 10);
    r.end();
}
