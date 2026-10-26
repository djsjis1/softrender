/** @file renderer_test.cpp
 *  @brief 验证 discard、深度提交与遮挡顺序的公共渲染行为。
 */
#include "softrenderer/render/pipeline/renderer.h"
#include "softrenderer/render/pipeline/surface_shader.h"

#include <gtest/gtest.h>

using namespace sr;

namespace sr::tests
{
namespace
{
struct MaskShader : SurfaceShader
{
    bool discardAll = false;
    bool discardLeft = false;

    Vec4f vertex(const Vertex& in, Varyings& out) override
    {
        out.uv = {(in.position.x + 1.0f) * 0.5f, (in.position.y + 1.0f) * 0.5f};
        return {in.position.x, in.position.y, in.position.z, 1.0f};
    }

    bool fragment(const Varyings& in, Vec3f& color) override
    {
        color = baseColor;
        return !discardAll && !(discardLeft && in.uv.x < 0.5f);
    }
};

Mesh makeTriangle(float zOffset)
{
    Mesh mesh;
    mesh.vertices = {{{-1, -1, -0.6f + zOffset}, {}, {}, {}},
                     {{1, -1, -0.2f + zOffset}, {}, {}, {}},
                     {{-1, 1, -0.4f + zOffset}, {}, {}, {}}};
    mesh.indices = {0, 1, 2};
    return mesh;
}

// 16x16 直角三角形的屏幕坐标为 (0,0)/(16,0)/(0,16)，从几何位置独立算深度。
float expectedDepth(int x, int y, float zOffset)
{
    return 0.2f + 0.2f * (x + 0.5f) / 16.0f + 0.1f * (y + 0.5f) / 16.0f + zOffset * 0.5f;
}
} // namespace

TEST(RendererDiscard, drawMesh_discardAll_preservesClearDepthAndColor)
{
    Renderer renderer(16, 16);
    Framebuffer framebuffer(16, 16);
    renderer.clear(framebuffer, sr::colors::black);
    MaskShader shader;
    shader.discardAll = true;
    renderer.drawMesh(framebuffer, makeTriangle(0), shader);

    EXPECT_FLOAT_EQ(renderer.depth().get(2, 2), DepthBuffer::kFar);
    EXPECT_EQ(framebuffer.getPixel(2, 2).r, 0);
}

TEST(RendererDiscard, drawMesh_partialDiscard_backgroundVisibleInEitherOrder)
{
    for (bool backgroundFirst : {false, true})
    {
        SCOPED_TRACE(backgroundFirst);
        Renderer renderer(16, 16);
        Framebuffer framebuffer(16, 16);
        renderer.clear(framebuffer, sr::colors::black);
        MaskShader foreground;
        foreground.baseColor = {1, 0, 0};
        foreground.discardLeft = true;
        MaskShader background;
        background.baseColor = {0, 0, 1};

        if (backgroundFirst)
            renderer.drawMesh(framebuffer, makeTriangle(0.8f), background);
        renderer.drawMesh(framebuffer, makeTriangle(0), foreground);
        if (!backgroundFirst)
            renderer.drawMesh(framebuffer, makeTriangle(0.8f), background);

        EXPECT_EQ(framebuffer.getPixel(2, 2).b, 255);
        EXPECT_NEAR(renderer.depth().get(2, 2), expectedDepth(2, 2, 0.8f), 1e-6f);
        EXPECT_EQ(framebuffer.getPixel(10, 2).r, 255);
        EXPECT_NEAR(renderer.depth().get(10, 2), expectedDepth(10, 2, 0), 1e-6f);
    }
}

TEST(RendererDiscard, drawMesh_smallerFramebuffer_clampsWrites)
{
    Renderer renderer(16, 16);
    Framebuffer framebuffer(4, 3);
    MaskShader shader;
    shader.baseColor = {0, 1, 0};
    renderer.drawMesh(framebuffer, makeTriangle(0), shader);
    EXPECT_EQ(framebuffer.getPixel(3, 2).g, 255);
    EXPECT_FLOAT_EQ(renderer.depth().get(4, 2), DepthBuffer::kFar);
}
} // namespace sr::tests

// ==================== 顶点缓存（post-transform cache） ====================
//
// 契约：一次 drawMesh 内同一索引的共享顶点只执行一次 shader.vertex；
// 输出像素与"每三角形重复着色"完全一致（顶点着色在同一次 draw 内确定性）。

namespace sr::tests
{
namespace
{
struct CountingShader : SurfaceShader
{
    int vertexCalls = 0;
    Vec3f baseColor{1, 0, 0};

    Vec4f vertex(const Vertex& in, Varyings& out) override
    {
        ++vertexCalls;
        out.color = baseColor;
        return {in.position.x, in.position.y, in.position.z, 1.0f};
    }

    bool fragment(const Varyings& in, Vec3f& color) override
    {
        color = in.color;
        return true;
    }
};

// 索引网格：4 个共享顶点、2 个三角形（共享边 (0,2)）
Mesh makeSharedQuad()
{
    Mesh mesh;
    mesh.vertices = {{{-1, -1, -0.5f}, {}, {}, {}},
                     {{1, -1, -0.5f}, {}, {}, {}},
                     {{1, 1, -0.5f}, {}, {}, {}},
                     {{-1, 1, -0.5f}, {}, {}, {}}};
    mesh.indices = {0, 1, 2, 0, 2, 3};
    return mesh;
}

// 同一几何的展开版本：6 个独立顶点，无共享
Mesh makeExpandedQuad()
{
    Mesh mesh = makeSharedQuad();
    Mesh expanded;
    for (int i : {0, 1, 2, 0, 2, 3})
        expanded.vertices.push_back(mesh.vertices[size_t(i)]);
    expanded.indices = {0, 1, 2, 3, 4, 5};
    return expanded;
}
} // namespace

TEST(RendererVertexCache, sharedVerticesShadedOncePerDraw)
{
    Renderer renderer(16, 16);
    Framebuffer fb(16, 16);
    CountingShader shader;
    renderer.drawMesh(fb, makeSharedQuad(), shader);
    // 旧实现是 3×2=6 次；缓存后共享顶点只着色一次
    EXPECT_EQ(shader.vertexCalls, 4);

    // 跨 drawMesh 不复用：第二次 draw 全量重算
    renderer.drawMesh(fb, makeSharedQuad(), shader);
    EXPECT_EQ(shader.vertexCalls, 8);
}

TEST(RendererVertexCache, indexedMatchesExpandedPixelwise)
{
    Renderer r1(32, 32), r2(32, 32);
    Framebuffer f1(32, 32), f2(32, 32);
    CountingShader s1, s2;
    r1.drawMesh(f1, makeSharedQuad(), s1);
    r2.drawMesh(f2, makeExpandedQuad(), s2);

    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
        {
            EXPECT_EQ(f1.getPixel(x, y), f2.getPixel(x, y)) << "x=" << x << " y=" << y;
            EXPECT_FLOAT_EQ(r1.depth().get(x, y), r2.depth().get(x, y));
        }
}

TEST(RendererVertexCache, growingMeshBetweenDrawsReshadesNewRange)
{
    Renderer renderer(16, 16);
    Framebuffer fb(16, 16);
    CountingShader shader;
    Mesh small = makeSharedQuad();
    renderer.drawMesh(fb, small, shader);
    EXPECT_EQ(shader.vertexCalls, 4);

    // 顶点数变多：stamp 数组扩容后 epoch 重置，新调用全量重算
    Mesh bigger = makeExpandedQuad();
    renderer.drawMesh(fb, bigger, shader);
    EXPECT_EQ(shader.vertexCalls, 10);
}

// ==================== 外部深度存储（裸机免堆路径） ====================

TEST(RendererBaremetal, externalDepthStorageMatchesOwningPath)
{
    std::vector<float> extDepth(32 * 32);
    Renderer extRenderer(32, 32, extDepth.data());
    Framebuffer extFb(32, 32);
    Renderer ownRenderer(32, 32);
    Framebuffer ownFb(32, 32);

    MaskShader shader;
    shader.baseColor = {0, 1, 0};
    extRenderer.clear(extFb, sr::colors::black);
    ownRenderer.clear(ownFb, sr::colors::black);
    extRenderer.drawMesh(extFb, makeTriangle(0.8f), shader);
    ownRenderer.drawMesh(ownFb, makeTriangle(0.8f), shader);

    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
        {
            EXPECT_EQ(extFb.getPixel(x, y), ownFb.getPixel(x, y)) << "x=" << x << " y=" << y;
            EXPECT_FLOAT_EQ(extDepth[size_t(y) * 32 + x], ownRenderer.depth().get(x, y));
        }
}
} // namespace sr::tests

// ==================== 外部顶点缓存（裸机零堆路径） ====================

namespace sr::tests
{

TEST(RendererBaremetal, externalVertexCacheMatchesInternalPath)
{
    // 同一网格分别用内部 vector 缓存与外部静态数组缓存渲染，逐像素一致。
    const Mesh quad = makeSharedQuad();
    Renderer r1(24, 24), r2(24, 24);
    Framebuffer f1(24, 24), f2(24, 24);

    static ClipVertex extCache[8];
    static uint32_t extStamps[8];
    r2.setExternalVertexCache(extCache, extStamps, 8);

    CountingShader s1, s2;
    s1.baseColor = {0.2f, 0.6f, 0.9f};
    s2.baseColor = {0.2f, 0.6f, 0.9f};
    r1.drawMesh(f1, quad, s1);
    r2.drawMesh(f2, quad, s2);

    EXPECT_EQ(s2.vertexCalls, 4) << "外部缓存同样每共享顶点只着色一次";
    for (int y = 0; y < 24; ++y)
        for (int x = 0; x < 24; ++x)
            EXPECT_EQ(f1.getPixel(x, y), f2.getPixel(x, y)) << "x=" << x << " y=" << y;

    // 二次 draw：外部 stamps 跨 draw 保留，epoch 递增后全部重算
    r2.drawMesh(f2, quad, s2);
    EXPECT_EQ(s2.vertexCalls, 8);
}

TEST(RendererBaremetal, externalVertexCacheFallsBackWhenTooSmall)
{
    Renderer renderer(16, 16);
    Framebuffer fb(16, 16);
    static ClipVertex extCache[2];
    static uint32_t extStamps[2];
    renderer.setExternalVertexCache(extCache, extStamps, 2); // 容量 2 < 顶点数 4

    CountingShader shader;
    renderer.drawMesh(fb, makeSharedQuad(), shader);
    // 回退到内部 vector：结果仍正确（4 次去重着色），只是走了堆路径
    EXPECT_EQ(shader.vertexCalls, 4);
}
} // namespace sr::tests
