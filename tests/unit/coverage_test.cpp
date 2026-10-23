#include <gtest/gtest.h>

#include "softrenderer/render/pipeline/msaa_renderer.h"
#include "softrenderer/render/raster/rasterizer_fast.h"
#include "softrenderer/render/pipeline/renderer.h"

using namespace sr;

namespace sr::tests {
namespace {
struct CountingShader : Shader
{
    int calls = 0;
    Vec4f vertex(const Vertex& v, Varyings& out) override
    {
        out.uv = {(v.position.x + 1) * 4, (v.position.y + 1) * 4};
        return {v.position.x, v.position.y, v.position.z, 1};
    }
    bool fragment(const Varyings&, Vec3f&) override
    {
        ++calls;
        return false;
    }
};
Mesh quad(bool reverse = false)
{
    Mesh mesh;
    mesh.vertices = {
        {{-1, -1, 0}, {}, {}, {}}, {{1, -1, 0}, {}, {}, {}}, {{1, 1, 0}, {}, {}, {}}, {{-1, 1, 0}, {}, {}, {}}};
    mesh.indices = reverse ? std::vector<int>{2, 1, 0, 3, 2, 0} : std::vector<int>{0, 1, 2, 0, 2, 3};
    return mesh;
}
}  // namespace
TEST(RasterCoverage, drawMesh_sharedDiagonal_invokesExactlyOncePerPixel)
{
    for (bool reverse : {false, true})
    {
        Renderer renderer(8, 8);
        renderer.cullBackface = false;
        Framebuffer fb(8, 8);
        CountingShader shader;
        renderer.drawMesh(fb, quad(reverse), shader);
        EXPECT_EQ(shader.calls, 64);
    }
}
TEST(RasterCoverage, drawMesh_clippedQuad_hasNoInternalCrackOrDoubleHit)
{
    for (bool reverse : {false, true})
    {
        Renderer renderer(8, 8);
        renderer.cullBackface = false;
        Framebuffer fb(8, 8);
        CountingShader shader;
        auto mesh = quad(reverse);
        // 左侧越出视锥，两个三角形分别裁剪，之后仍应完整覆盖 8x8。
        mesh.vertices[0].position.x = -2;
        mesh.vertices[3].position.x = -2;
        renderer.drawMesh(fb, mesh, shader);
        EXPECT_EQ(shader.calls, 64);
    }
}
TEST(MSAACoverage, rasterize_centerOutside_commitsCoveredSample)
{
    MSAAFlatRenderer<4> renderer(1, 1);
    renderer.clear(sr::colors::black);
    renderer.rasterize({0.75f, 0.5f}, {1, 0.5f}, {1, 1}, 0.2f, 0.2f, 0.2f, sr::colors::red);
    Framebuffer fb(1, 1);
    renderer.resolveTo(fb);
    EXPECT_EQ(fb.getPixel(0, 0).r, 64);
}
TEST(MSAACoverage, rasterize_backgroundDoesNotOverwriteOccludedSample)
{
    MSAAFlatRenderer<4> renderer(1, 1);
    renderer.clear(sr::colors::black);
    renderer.rasterize({0.75f, 0.5f}, {1, 0.5f}, {1, 1}, 0.2f, 0.2f, 0.2f, sr::colors::red);
    renderer.rasterize({-2, -2}, {4, -2}, {-2, 4}, 0.8f, 0.8f, 0.8f, sr::colors::blue);
    Framebuffer fb(1, 1);
    renderer.resolveTo(fb);
    EXPECT_EQ(fb.getPixel(0, 0).r, 64);
    EXPECT_EQ(fb.getPixel(0, 0).b, 191);
}
}  // namespace sr::tests
