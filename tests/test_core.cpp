// ============================================================
// 核心算法单元测试（Google Test）
//
// 覆盖：向量运算、矩阵变换、重心坐标、裁剪、深度缓冲、纹理采样
// 运行：cmake --build build --target unit_tests && ctest --test-dir build
// ============================================================
#include <gtest/gtest.h>

#include <entt/entt.hpp>
#include <filesystem>
#include <type_traits>

#include "core/arena.h"
#include "core/benchmark.h"
#include "core/blend.h"
#include "core/command_buffer.h"
#include "core/depthbuffer.h"
#include "core/framebuffer.h"
#include "core/gbuffer.h"
#include "core/image_write.h"
#include "core/log.h"
#include "core/resource_manager.h"
#include "core/tga.h"
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
#include "test/pixel_regression.h"

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

// get 的越界兜底。旧实现 get() 连 assert 都没有，直接下标访问，
// 是三个访问器（testAndSet / row / get）中唯一完全无保护的。
TEST(DepthBuffer, depthBuffer_get_outOfBounds_returnsFar)
{
#ifndef NDEBUG
    GTEST_SKIP() << "越界哨兵仅在 Release（NDEBUG）下可测：Debug 下 assert 会先中止进程";
#endif
    DepthBuffer db(4, 4);
    // 四个方向都要测：只测一侧会漏掉「x 检查误用了 height_」这类笔误
    EXPECT_FLOAT_EQ(db.get(-1, 0), DepthBuffer::kFar);
    EXPECT_FLOAT_EQ(db.get(4, 0), DepthBuffer::kFar);
    EXPECT_FLOAT_EQ(db.get(0, -1), DepthBuffer::kFar);
    EXPECT_FLOAT_EQ(db.get(0, 4), DepthBuffer::kFar);
    // 合法位置仍返回真实值：确认兜底没有把正常路径也短路掉
    db.testAndSet(1, 1, 0.25f);
    EXPECT_FLOAT_EQ(db.get(1, 1), 0.25f);
}

// ==================== 帧缓冲边界 ====================

// setPixel 早就有运行时边界检查（越界静默忽略），而 getPixel 只有 assert。
// Release 下 assert 被编译掉，越界读就是 UB —— 这个不对称是本用例要锁住的契约。
TEST(Framebuffer, framebuffer_getPixel_outOfBounds_returnsSentinel)
{
#ifndef NDEBUG
    GTEST_SKIP() << "越界哨兵仅在 Release（NDEBUG）下可测：Debug 下 assert 会先中止进程";
#endif
    // 用一个非黑的填充色，这样「读到 colors::black 哨兵」与「读到真实像素」可区分
    Framebuffer fb(4, 4, Color(10, 20, 30, 255));

    EXPECT_EQ(fb.getPixel(-1, 0).r, 0);
    EXPECT_EQ(fb.getPixel(4, 0).r, 0);
    EXPECT_EQ(fb.getPixel(0, -1).r, 0);
    EXPECT_EQ(fb.getPixel(0, 4).r, 0);
    // 哨兵是 colors::black，其 a 通道为默认值 255（而非 0）
    EXPECT_EQ(fb.getPixel(-1, -1).a, 255);

    // 合法位置不受影响
    EXPECT_EQ(fb.getPixel(0, 0).r, 10);
    EXPECT_EQ(fb.getPixel(3, 3).b, 30);

    // setPixel 越界仍是静默忽略，不得抛异常也不得写内存
    fb.setPixel(-1, -1, Color(255, 255, 255));
    fb.setPixel(4, 4, Color(255, 255, 255));
    EXPECT_EQ(fb.getPixel(0, 0).r, 10);
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

// toUint8Round 必须四舍五入而非截断，并双向钳制。
// 旧实现四处都是 uint8_t(float)，直接截断小数 → 每次混合系统性偏暗最多 1/255，
// 粒子 / 透明排序 / 2D 抗锯齿多层叠加后会累积出可见偏差。
TEST(Blend, toUint8Round_roundsAndClamps)
{
    EXPECT_EQ(toUint8Round(0.0f), 0);
    EXPECT_EQ(toUint8Round(0.4f), 0);
    EXPECT_EQ(toUint8Round(0.5f), 1);  // 截断会给 0，这是两者可区分的最小用例
    EXPECT_EQ(toUint8Round(0.6f), 1);
    EXPECT_EQ(toUint8Round(254.4f), 254);
    EXPECT_EQ(toUint8Round(254.9f), 255);
    EXPECT_EQ(toUint8Round(255.0f), 255);

    // 钳制上界：float → uint8_t 在值超出 [0,255] 时是 UB，
    // 旧实现靠调用方逐个写 std::min，现在收敛到函数内部。
    EXPECT_EQ(toUint8Round(255.4f), 255);
    EXPECT_EQ(toUint8Round(300.0f), 255);
    EXPECT_EQ(toUint8Round(1.0e9f), 255);
    // 钳制下界：负值转无符号整型同样是 UB
    EXPECT_EQ(toUint8Round(-0.4f), 0);
    EXPECT_EQ(toUint8Round(-10.0f), 0);
}

// 构造一个「截断与四舍五入结果不同」的真实混合用例。
// src.r=1, src.a=128 → sa = 128/255 = 0.50196，dst.r=0
// → 1*0.50196 + 0*0.49804 = 0.50196：截断得 0，四舍五入得 1。
TEST(Blend, blendPixels_alphaBlendRoundsInsteadOfTruncating)
{
    Color src(1, 1, 1, 128);
    Color dst(0, 0, 0, 255);
    Color r = blendPixels(src, dst, BlendMode::AlphaBlend);
    EXPECT_EQ(r.r, 1);  // 旧的截断实现会得到 0
    EXPECT_EQ(r.g, 1);
    EXPECT_EQ(r.b, 1);
}

// 四种混合模式均不得因钳制缺失而回绕（旧实现 Additive / Premultiplied 可达 510）。
TEST(Blend, blendPixels_allModesNeverWrapAround)
{
    const BlendMode modes[] = {BlendMode::Opaque, BlendMode::AlphaBlend, BlendMode::Additive,
                               BlendMode::Multiply, BlendMode::Premultiplied};
    const Color extreme[] = {Color(255, 255, 255, 255), Color(255, 255, 255, 255)};
    for (BlendMode m : modes)
    {
        Color r = blendPixels(extreme[0], extreme[1], m);
        EXPECT_EQ(r.r, 255) << "mode=" << static_cast<int>(m);
        EXPECT_EQ(r.g, 255) << "mode=" << static_cast<int>(m);
        EXPECT_EQ(r.b, 255) << "mode=" << static_cast<int>(m);
    }
}

// ==================== 图像加载（外部输入校验） ====================

// 图像文件属不可信外部输入，testing_rules.md「异常测试」要求覆盖「文件缺失」。
// imageloader::loadImage 全仓零调用方，但它是 header-only 库的公共 API，
// 下游用户会直接调，因此失败路径必须有测试锁定。
TEST(ImageLoader, loadImage_missingFile_returnsFalse)
{
    tga::Image img;
    EXPECT_FALSE(imageloader::loadImage("sr_definitely_not_here_12345.png", img));
    // 失败时不得留下半初始化的输出（否则调用方会拿 width/height 去算偏移）
    EXPECT_FALSE(img.valid());
    EXPECT_TRUE(img.pixels.empty());
}

TEST(ImageLoader, loadImage_emptyPath_returnsFalse)
{
    tga::Image img;
    EXPECT_FALSE(imageloader::loadImage("", img));
    EXPECT_FALSE(img.valid());
}

// 写→读往返：同时验证 imagewriter 与 imageloader，以及两者共用的 y 向上约定。
//
// 为什么用系统临时目录而不提交 fixture：testing_rules.md 要求测试不得往仓库落盘；
// output/ 是示例产物目录，测试写进去会与示例输出混淆。
// 往返测试比静态 fixture 更强：它同时验证了 writeImage 的翻转与
// stbi_set_flip_vertically_on_load 的反翻转是否真的抵消。
TEST(ImageLoader, loadImage_roundTripPreservesPixelsAndOrientation)
{
    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() / "sr_imageloader_roundtrip.png";

    // 4x3 非正方形，且只在两个对角置色：若 y 翻转出错，两个角会互换
    Framebuffer fb(4, 3, Color(0, 0, 0, 255));
    fb.setPixel(0, 0, Color(255, 0, 0, 255));  // 左下角红
    fb.setPixel(3, 2, Color(0, 255, 0, 255));  // 右上角绿

    if (!imagewriter::writeImage(tmp.string(), fb))
    {
        GTEST_SKIP() << "无法向系统临时目录写出 PNG（权限或路径问题），非被测代码缺陷";
    }

    tga::Image img;
    ASSERT_TRUE(imageloader::loadImage(tmp.string(), img));
    EXPECT_EQ(img.width, 4);
    EXPECT_EQ(img.height, 3);
    ASSERT_EQ(img.pixels.size(), size_t(12));

    // y 向上约定必须在往返中保持：左下角仍是红、右上角仍是绿
    EXPECT_EQ(img.at(0, 0).r, 255);
    EXPECT_EQ(img.at(0, 0).g, 0);
    EXPECT_EQ(img.at(3, 2).g, 255);
    EXPECT_EQ(img.at(3, 2).r, 0);
    // 中间像素应为黑
    EXPECT_EQ(img.at(1, 1).r, 0);
    EXPECT_EQ(img.at(1, 1).g, 0);

    std::error_code ec;
    fs::remove(tmp, ec);  // 清理；删除失败不影响测试结论
}

// ==================== 内置日志格式化 ====================

// sr::formatLog 只在内置（非 spdlog）模式下存在。
// 它被从 Log::log 里拆出来就是为了能这样直接断言，而不用捕获 stderr。
#ifndef SR_USE_SPDLOG

TEST(Log, formatLog_noArgs_returnsFormatString)
{
    EXPECT_EQ(sr::formatLog("no placeholder"), "no placeholder");
    EXPECT_EQ(sr::formatLog(""), "");
}

TEST(Log, formatLog_substitutesBracesInOrder)
{
    EXPECT_EQ(sr::formatLog("a {} b", 42), "a 42 b");
    EXPECT_EQ(sr::formatLog("{} + {} = {}", 1, 2, 3), "1 + 2 = 3");
    EXPECT_EQ(sr::formatLog("{}x{}", 3, 4), "3x4");
}

// 关键回归：std::string 必须能直接传。
// 旧实现用 C 可变参，传 std::string 是 UB（MSVC 报 C4840）。
TEST(Log, formatLog_acceptsNonPodTypes)
{
    EXPECT_EQ(sr::formatLog("path '{}'", std::string("x.png")), "path 'x.png'");
    EXPECT_EQ(sr::formatLog("{} and {}", std::string("a"), 7), "a and 7");
}

// 旧实现把格式串交给 vfprintf，而约定写的是 {}，所以占位符从不被替换。
// 本用例锁定「参数不得静默丢失」这个行为。
TEST(Log, formatLog_extraArgsAreAppendedNotDropped)
{
    EXPECT_EQ(sr::formatLog("only {}", 1, 2), "only 1 2");
    EXPECT_EQ(sr::formatLog("none", 5), "none 5");
}

// 占位符多于参数时降级为保留字面量 {}，而非像 fmt 那样抛异常或崩溃。
TEST(Log, formatLog_extraBracesAreKeptLiteral)
{
    EXPECT_EQ(sr::formatLog("{} and {}", 1), "1 and {}");
    // 未匹配的 {} 在串中间与串尾都应原样保留
    EXPECT_EQ(sr::formatLog("a {} b {} c", 1), "a 1 b {} c");
    EXPECT_EQ(sr::formatLog("{}{}", 1), "1{}");
}

// 旧实现把格式串直接交给 vfprintf，文本里的字面 '%' 会被当转换符解析 → UB。
// 现在消息经单个 %s 输出，字面 '%' 安全。
TEST(Log, formatLog_literalPercentIsSafe)
{
    EXPECT_EQ(sr::formatLog("100% done"), "100% done");
    EXPECT_EQ(sr::formatLog("100% {} ok", 1), "100% 1 ok");
    EXPECT_EQ(sr::formatLog("%s %d %f 不应被解析", 1), "%s %d %f 不应被解析 1");
}

// 等级名映射：旧实现用常量数组且**漏了 Off**，传 Off 会越界读。
TEST(Log, levelName_coversAllEnumValues)
{
    EXPECT_STREQ(sr::levelName(sr::LogLevel::Trace), "TRACE");
    EXPECT_STREQ(sr::levelName(sr::LogLevel::Debug), "DEBUG");
    EXPECT_STREQ(sr::levelName(sr::LogLevel::Info), "INFO ");
    EXPECT_STREQ(sr::levelName(sr::LogLevel::Warn), "WARN ");
    EXPECT_STREQ(sr::levelName(sr::LogLevel::Error), "ERROR");
    EXPECT_STREQ(sr::levelName(sr::LogLevel::Critical), "CRIT ");
    EXPECT_STREQ(sr::levelName(sr::LogLevel::Off), "OFF  ");
    // 越界的枚举值不得崩溃（旧实现会数组越界）
    EXPECT_STREQ(sr::levelName(static_cast<sr::LogLevel>(99)), "?????");
}

TEST(Log, setLevel_roundTrips)
{
    const sr::LogLevel saved = sr::Log::level();
    sr::Log::setLevel(sr::LogLevel::Error);
    EXPECT_EQ(sr::Log::level(), sr::LogLevel::Error);
    sr::Log::setLevel(saved);  // 测试必须独立，不得泄露全局状态
    EXPECT_EQ(sr::Log::level(), saved);
}

#endif // !SR_USE_SPDLOG

// ==================== 像素回归工具 ====================

// regression::compare 是全部端到端回归测试的判定核心。
// 若它恒返回 passed=true，那么 tests/integration 的三个场景就全部是假通过。
// 本套件直接验证它的判定逻辑，与基准图文件无关。
//
// 通过判据（见 pixel_regression.h）：不匹配像素占比 < 0.1% **且** 平均差 < 0.5。
// 两个条件是 **且** 关系，下面分别验证各自能单独触发失败。
TEST(PixelRegression, compare_identicalFramebuffers_passes)
{
    Framebuffer a(64, 64, Color(10, 20, 30));
    Framebuffer b(64, 64, Color(10, 20, 30));
    const regression::CompareResult r = regression::compare(a, b);
    EXPECT_TRUE(r.passed);
    EXPECT_EQ(r.totalPixels, 64 * 64);
    EXPECT_EQ(r.mismatchedPixels, 0);
    EXPECT_FLOAT_EQ(r.maxDelta, 0.0f);
    EXPECT_FLOAT_EQ(r.avgDelta, 0.0f);
}

// 单像素差异小于 pixelThreshold（2.0）：不计入不匹配，也不得拉高平均差到阈值上
TEST(PixelRegression, compare_singlePixelWithinThreshold_passes)
{
    Framebuffer a(64, 64, Color(10, 20, 30));
    Framebuffer b(64, 64, Color(10, 20, 30));
    b.setPixel(32, 32, Color(11, 20, 30));  // 差 1，小于阈值 2.0
    const regression::CompareResult r = regression::compare(a, b);
    EXPECT_TRUE(r.passed);
    EXPECT_EQ(r.mismatchedPixels, 0);
    EXPECT_FLOAT_EQ(r.maxDelta, 1.0f);
}

// 单个「热像素」（差异远超阈值）仍应通过：
// 占比 1/4096 = 0.000244 < 0.001，平均差 200/4096 = 0.049 < 0.5。
// 这是容差设计的核心：抗锯齿/浮点微差不应误报回归。
TEST(PixelRegression, compare_singleHotPixel_passesWithinTolerance)
{
    Framebuffer a(64, 64, Color(10, 20, 30));
    Framebuffer b(64, 64, Color(10, 20, 30));
    b.setPixel(32, 32, Color(210, 20, 30));  // 差 200，远超阈值
    const regression::CompareResult r = regression::compare(a, b);
    EXPECT_TRUE(r.passed);
    EXPECT_EQ(r.mismatchedPixels, 1);
    EXPECT_FLOAT_EQ(r.maxDelta, 200.0f);
    EXPECT_LT(r.mismatchRatio, 0.001f);
}

// 大面积差异必须失败：占比条件触发
TEST(PixelRegression, compare_largeAreaDiff_fails)
{
    Framebuffer a(64, 64, Color(10, 20, 30));
    Framebuffer b(64, 64, Color(10, 20, 30));
    for (int y = 0; y < 32; ++y)  // 上半屏全改
        for (int x = 0; x < 64; ++x)
            b.setPixel(x, y, Color(200, 200, 200));
    const regression::CompareResult r = regression::compare(a, b);
    EXPECT_FALSE(r.passed);
    EXPECT_EQ(r.mismatchedPixels, 32 * 64);
    EXPECT_GT(r.mismatchRatio, 0.4f);
}

// 平均差条件必须能**单独**触发失败：
// 每个像素只差 1（均低于 pixelThreshold 2.0，所以 mismatchedPixels = 0），
// 但 avgDelta = 1.0 > 0.5。若只有占比条件，这种「全图系统性偏移」会被漏掉。
TEST(PixelRegression, compare_uniformSmallDrift_failsOnAvgDelta)
{
    Framebuffer a(64, 64, Color(10, 20, 30));
    Framebuffer b(64, 64, Color(11, 21, 31));  // 每通道均差 1
    const regression::CompareResult r = regression::compare(a, b);
    EXPECT_EQ(r.mismatchedPixels, 0);  // 没有像素超过单像素阈值
    EXPECT_FLOAT_EQ(r.avgDelta, 1.0f); // 但平均差超了 0.5
    EXPECT_FALSE(r.passed);
}

// 尺寸不一致时必须失败，且不得越界读取（旧实现无此保护会直接读越界）
TEST(PixelRegression, compare_sizeMismatch_failsSafely)
{
    Framebuffer a(64, 64, Color(10, 20, 30));
    Framebuffer b(32, 32, Color(10, 20, 30));
    const regression::CompareResult r = regression::compare(a, b);
    EXPECT_FALSE(r.passed);
    EXPECT_EQ(r.totalPixels, 0);
}

// goldenPath 必须用编译期注入的绝对路径，不能依赖工作目录。
// ctest 的工作目录是 build/，若用相对路径会解析成 build/tests/golden/ →
// 找不到基准图 → 旧逻辑自动创建并返回 passed → 测试永远假通过。
TEST(PixelRegression, goldenPath_isAbsoluteAndEndsWithSceneName)
{
    const std::string dir = regression::goldenDir();
    ASSERT_FALSE(dir.empty());
    const std::string path = regression::goldenPath("scene_x");
    EXPECT_EQ(path, dir + "/scene_x.tga");
#ifdef SR_GOLDEN_DIR
    // Windows 盘符形式（C:/...）或 POSIX 根形式（/...）都算绝对路径
    const bool isAbsolute = (path.size() > 1 && path[1] == ':') || path[0] == '/';
    EXPECT_TRUE(isAbsolute) << "SR_GOLDEN_DIR 已定义但路径不是绝对的：" << path;
#else
    // 未注入 SR_GOLDEN_DIR 时这个用例没有价值：它会静默退化成只验证字符串拼接。
    // 用 FAIL 而非静默通过，防止 CMake 侧遗漏定义时无人察觉。
    FAIL() << "unit_tests 未定义 SR_GOLDEN_DIR，无法验证绝对路径。"
           << "请检查 CMakeLists.txt 是否给本目标注入了该定义。";
#endif
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
    raster_fast::fillTriangleIncremental(fb, zb, v0, v1, v2, z0, z1, z2, Color(255, 255, 255));

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
    raster_fast::fillTriangleFast(fbCCW, zbCCW, v0, Vec2f{28, 4}, Vec2f{16, 28},
                                  zAtV0, zAtRight, zAtTop, Color(255, 255, 255));
    // 顺时针：v0 → 上方 → 右下（顶点顺序反转，深度随顶点一起反转）
    raster_fast::fillTriangleFast(fbCW, zbCW, v0, Vec2f{16, 28}, Vec2f{28, 4},
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

    raster_fast::fillTriangleIncremental(fb1, zb1, v0, v1, v2, z0, z1, z2, Color(200, 100, 50));
#if SR_HAS_SSE2
    raster_fast::fillTriangleSSE2(fb2, zb2, v0, v1, v2, z0, z1, z2, Color(200, 100, 50));
#else
    raster_fast::fillTriangleIncremental(fb2, zb2, v0, v1, v2, z0, z1, z2, Color(200, 100, 50));
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

// ==================== ECS (EnTT) ====================

// 【编译期验收哨兵】在全局作用域声明与 8 个 ECS 组件同名的类型。
//
// 这能编译通过，本身就证明了 ADR-001 的目标已达成：下游使用者现在
// **可以**定义自己的 Position / Light / Texture 而不与本库冲突。
//
// 若将来有人图省事在 components.h 里加回 `using sr::Position;`（别名桥），
// 本处会立即报**重定义编译错误** —— 因为 using 声明仍会把名字注入全局作用域，
// 根本解决不了撞名。这正是 ADR-001 拒绝对高冲突名字使用别名桥的理由，
// 用编译错误把它锁定下来，而不是只写在文档里。
struct Position
{
    int canary = 0;
};
struct Velocity
{
    int canary = 0;
};
struct Rotation
{
    int canary = 0;
};
struct Scale
{
    int canary = 0;
};
struct MeshRef
{
    int canary = 0;
};
struct MaterialRef
{
    int canary = 0;
};
struct Light
{
    int canary = 0;
};
struct CameraTag
{
    int canary = 0;
};

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
    registry.emplace<sr::Position>(e, Vec3f{1, 2, 3});
    EXPECT_TRUE(registry.all_of<sr::Position>(e));
    EXPECT_FALSE(registry.all_of<sr::Velocity>(e));
    auto& pos = registry.get<sr::Position>(e);
    EXPECT_FLOAT_EQ(pos.pos.x, 1.0f);
    EXPECT_FLOAT_EQ(pos.pos.y, 2.0f);
}

TEST(ECS, ViewSystem)
{
    entt::registry registry;
    for (int i = 0; i < 5; ++i)
    {
        auto e = registry.create();
        registry.emplace<sr::Position>(e, Vec3f{float(i), 0, 0});
        registry.emplace<sr::Velocity>(e, Vec3f{1, 0, 0});
    }
    // System: move
    registry.view<sr::Position, sr::Velocity>().each(
        [](sr::Position& p, sr::Velocity& v) { p.pos = p.pos + v.vel; });
    // Verify: each position should be initial + 1
    int count = 0;
    registry.view<sr::Position>().each([&](sr::Position& p) {
        // 初始值是 0,1,2,3,4，加 1 后应该是 1,2,3,4,5
        EXPECT_GE(p.pos.x, 1.0f);
        EXPECT_LE(p.pos.x, 5.0f);
        ++count;
    });
    EXPECT_EQ(count, 5);
}

// 验证哨兵类型与 sr:: 组件确实是**不同类型**，而非同一类型的两个名字。
// 若 components.h 退化成全局定义（或加了 using 别名），上面的哨兵声明
// 会先报重定义错误；本用例是第二道保险。
TEST(ECS, globalCanaryTypesAreDistinctFromSrComponents)
{
    EXPECT_FALSE((std::is_same<Position, sr::Position>::value));
    EXPECT_FALSE((std::is_same<Light, sr::Light>::value));
    EXPECT_FALSE((std::is_same<CameraTag, sr::CameraTag>::value));

    // 哨兵类型可正常使用，证明全局作用域确实归使用者所有
    Position canary;
    EXPECT_EQ(canary.canary, 0);

    // sr:: 组件也可正常使用，且成员语义未变
    sr::Light light;
    EXPECT_FLOAT_EQ(light.intensity, 1.0f);
    EXPECT_FLOAT_EQ(light.direction.y, -1.0f);

    sr::MeshRef ref;
    EXPECT_EQ(ref.meshIndex, -1);  // -1 = 未设置，不是「第 0 个网格」
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
