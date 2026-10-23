// ============================================================
// resource 模块单元测试：网格、纹理（含 mipmap）、Cubemap 与程序化生成
//
// 资源只管理内存中的数据，所有用例都不触碰文件系统。
// ============================================================
#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "softrenderer/core/image.h"
#include "softrenderer/resource/cubemap.h"
#include "softrenderer/resource/mesh.h"
#include "softrenderer/resource/mesh_generator.h"
#include "softrenderer/resource/texture.h"
#include "softrenderer/resource/texture_generator.h"

using namespace sr;

TEST(Texture, MipmapGenerated)
{
    sr::Image img;
    img.width = 8;
    img.height = 8;
    img.pixels.resize(64, Color(128, 128, 128));
    Texture tex(img);
    EXPECT_GE(tex.mipLevels(), 4);  // 8→4→2→1
}

TEST(Texture, BilinearUniform)
{
    sr::Image img;
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
    sr::Image img;
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
    Mesh cube = sr::meshgen::cube(1.0f);
    EXPECT_EQ(cube.triangleCount(), 12);
}

TEST(Mesh, SphereNormalsUnitLength)
{
    Mesh sphere = sr::meshgen::sphere(1.0f, 16, 8);
    for (auto& v : sphere.vertices)
    {
        EXPECT_NEAR(v.normal.length(), 1.0f, 1e-4f);
    }
}

TEST(Mesh, PlaneGridVertexCount)
{
    Mesh plane = sr::meshgen::planeGrid(10.0f, 4, 1.0f);
    // (4+1)*(4+1) = 25 顶点
    EXPECT_EQ(plane.vertices.size(), 25u);
    // 4*4*2 = 32 三角形
    EXPECT_EQ(plane.triangleCount(), 32);
}

TEST(Mesh, ComputeTangentsNonZero)
{
    Mesh cube = sr::meshgen::cube(1.0f);
    computeTangents(cube);
    for (auto& v : cube.vertices)
    {
        EXPECT_GT(v.tangent.lengthSq(), 0.0f);
    }
}

// ==================== 渲染队列 ====================

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

