/** @file renderer_test.cpp
 *  @brief 验证 discard、深度提交与遮挡顺序的公共渲染行为。
 */
#include "raster/renderer.h"

#include <gtest/gtest.h>

namespace sr::tests
{
namespace
{
struct MaskShader : Shader
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
    renderer.clear(framebuffer, colors::black);
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
        renderer.clear(framebuffer, colors::black);
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
