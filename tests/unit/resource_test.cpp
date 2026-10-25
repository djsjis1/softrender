// ============================================================
// resource 模块单元测试：网格、纹理（含 mipmap）、Cubemap 与程序化生成
//
// 资源只管理内存中的数据，所有用例都不触碰文件系统。
// ============================================================
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
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

// 全退化几何（共线三角形 + 零法线）：切线与法线都无定义。
// 必须产出有限值（Debug 断言、Release NaN 都算错），不能依赖调用方预归一化。
TEST(Mesh, ComputeTangentsHandlesDegenerateNormals)
{
    Mesh mesh;
    mesh.vertices.resize(3);
    mesh.vertices[0].position = {0, 0, 0};
    mesh.vertices[1].position = {1, 0, 0};
    mesh.vertices[2].position = {2, 0, 0}; // 三点共线
    for (auto& v : mesh.vertices)
        v.normal = {0, 0, 0};
    mesh.indices = {0, 1, 2};

    computeTangents(mesh);
    for (const auto& v : mesh.vertices)
    {
        EXPECT_TRUE(std::isfinite(v.tangent.x));
        EXPECT_TRUE(std::isfinite(v.tangent.y));
        EXPECT_TRUE(std::isfinite(v.tangent.z));
        // 退化时给一个合法的单位切线，而不是零向量
        EXPECT_GT(v.tangent.lengthSq(), 0.5f);
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

// ==================== 无效输入与几何朝向 ====================

// 默认构造的 Texture 没有 mip 链：四条采样路径都必须拒绝，而不是访问 mips_[0]。
TEST(Texture, EmptyTextureSamplingIsSafe)
{
    const Texture tex;
    const Vec3f nearest = tex.sampleNearest(0.5f, 0.5f);
    const Vec3f bilinear = tex.sampleBilinear(0.5f, 0.5f);
    const Vec3f trilinear = tex.sampleTrilinear(0.5f, 0.5f, 0.1f, 0.1f, 0.1f, 0.1f);
    const Vec3f unified = tex.sample(0.5f, 0.5f);  // 默认 filter 为 Trilinear
    EXPECT_FLOAT_EQ(nearest.x, 1.0f);
    EXPECT_FLOAT_EQ(bilinear.x, 1.0f);
    EXPECT_FLOAT_EQ(trilinear.x, 1.0f);
    EXPECT_FLOAT_EQ(unified.x, 1.0f);
    EXPECT_FLOAT_EQ(unified.y, 0.0f);
}

// mip 层 0 就是原图本身（不再存副本），且层数计数必须包含它。
TEST(Texture, MipLevelZeroIsOriginalImage)
{
    sr::Image img;
    img.width = 2;
    img.height = 2;
    img.pixels = {Color(255, 0, 0), Color(0, 255, 0), Color(0, 0, 255), Color(255, 255, 0)};
    const Texture tex(img);
    EXPECT_EQ(tex.mipLevels(), 2); // 2x2 → 1x1

    // sampleLevel(0) 与 sampleBilinear 必须等价（都读原图）
    const Vec3f a = tex.sampleBilinear(0.25f, 0.25f);
    const Vec3f b = tex.sampleLevel(0.25f, 0.25f, 0);
    EXPECT_FLOAT_EQ(a.x, b.x);
    EXPECT_FLOAT_EQ(a.y, b.y);
    EXPECT_FLOAT_EQ(a.z, b.z);

    // 越界层号被夹到最后一级，而不是读空数组
    const Vec3f clamped = tex.sampleLevel(0.5f, 0.5f, 99);
    EXPECT_TRUE(std::isfinite(clamped.x));
}

// mip 缩小时 alpha 必须一起平均：三参 Color 构造会把 alpha 置 255。
TEST(Texture, MipmapPreservesAlpha)
{
    sr::Image img;
    img.width = 2;
    img.height = 2;
    img.pixels = {Color(200, 100, 50, 128), Color(200, 100, 50, 128), Color(200, 100, 50, 128),
                  Color(200, 100, 50, 128)};
    const Texture tex(img);
    ASSERT_EQ(tex.mipLevels(), 2);

    const Color level0 = tex.sampleColor(0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f);
    EXPECT_EQ(level0.a, 128);
    // 直接采最高层（1x1）也必须保留 128，而不是变成 255
    const Vec3f top = tex.sampleLevel(0.5f, 0.5f, 1);
    EXPECT_NEAR(top.x, 200.0f / 255.0f, 0.01f);
    const Color topColor = tex.sampleColor(0.5f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f);
    EXPECT_EQ(topColor.a, 128);
}

// 非有限 UV / 导数不得产生 NaN，也不得触发 float→int 的未定义行为。
TEST(Texture, NonFiniteUvIsRejected)
{
    const Texture tex(sr::texgen::checkerboard(8, 2));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    for (float bad : {nan, inf, -inf})
    {
        // 非有限 UV 返回品红兜底，而不是 NaN 或未定义行为
        EXPECT_FLOAT_EQ(tex.sampleNearest(bad, 0.5f).x, 1.0f);
        EXPECT_FLOAT_EQ(tex.sampleBilinear(0.5f, bad).x, 1.0f);
        EXPECT_FLOAT_EQ(tex.sampleTrilinear(bad, bad, 0.1f, 0.1f, 0.1f, 0.1f).x, 1.0f);
    }

    // 有限但极端的 UV 必须仍有有限结果（Repeat 下等价于取小数部分）
    for (float u : {0.5f, 1e9f, -1e9f, 1e-30f})
        EXPECT_TRUE(std::isfinite(tex.sampleBilinear(u, u).x));

    // 非有限导数不得产生 NaN：LOD 退回第 0 层
    EXPECT_TRUE(std::isfinite(tex.sampleTrilinear(0.5f, 0.5f, nan, inf, -inf, nan).x));
    EXPECT_TRUE(std::isfinite(tex.sample(0.5f, 0.5f, nan, nan, nan, nan).x));
}

// 只写 width/height 而忘记 resize 也是合法 C++，valid() 必须能识别。
TEST(Image, ValidRequiresPixelStorage)
{
    sr::Image img;
    img.width = 4;
    img.height = 4;
    EXPECT_FALSE(img.valid());
    img.pixels.resize(16, Color(1, 2, 3));
    EXPECT_TRUE(img.valid());
    img.pixels.resize(15);
    EXPECT_FALSE(img.valid());
    // 尺寸非正时像素总数必须为 0（不依赖 size_t 溢出后的大小比较）
    EXPECT_EQ(sr::Image{}.pixelCount(), 0u);
}

// 只填了前 5 面的 cubemap 必须判为无效：旧实现只看第 0 面。
TEST(Cubemap, InvalidWhenAnyFaceMissing)
{
    sr::Image faces[6];
    for (int i = 0; i < 5; ++i)
    {
        faces[i].width = 2;
        faces[i].height = 2;
        faces[i].pixels.resize(4, Color(10, 20, 30));
    }
    Cubemap cube;
    cube.setFaces(faces);
    EXPECT_FALSE(cube.valid());
    const Vec3f sampled = cube.sample({1, 0, 0});
    EXPECT_FLOAT_EQ(sampled.x, 0.0f);
}

// 球体三角形绕序必须与向外法线一致，否则默认背面剔除会把正面剔掉。
TEST(Mesh, SphereWindingMatchesOutwardNormals)
{
    const Mesh sphere = sr::meshgen::sphere(1.0f, 12, 8);
    ASSERT_GT(sphere.triangleCount(), 0);
    int checked = 0;
    for (int t = 0; t < sphere.triangleCount(); ++t)
    {
        const Vertex& v0 = sphere.vertices[sphere.indices[t * 3 + 0]];
        const Vertex& v1 = sphere.vertices[sphere.indices[t * 3 + 1]];
        const Vertex& v2 = sphere.vertices[sphere.indices[t * 3 + 2]];
        const Vec3f faceNormal = cross(v1.position - v0.position, v2.position - v0.position);
        if (faceNormal.lengthSq() < 1e-12f)
            continue;  // 退化面不计入
        const Vec3f outward = v0.normal + v1.normal + v2.normal;
        EXPECT_GT(dot(faceNormal, outward), 0.0f) << "三角形 " << t << " 绕序与向外法线相反";
        ++checked;
    }
    EXPECT_GT(checked, sphere.triangleCount() / 2);
}

// 退化参数不得触发整数除零。
TEST(TextureGenerator, DegenerateParametersReturnInvalidImage)
{
    EXPECT_FALSE(sr::texgen::checkerboard(64, 0).valid());
    EXPECT_FALSE(sr::texgen::checkerboard(0, 8).valid());
    EXPECT_FALSE(sr::texgen::uvGrid(64, 0).valid());
    EXPECT_FALSE(sr::texgen::normalMapBumps(64, 0).valid());
    // checks > size：格子不足一格，cell 夹到 1 而不是得到 0 后除零
    EXPECT_TRUE(sr::texgen::checkerboard(4, 16).valid());
    EXPECT_TRUE(sr::texgen::uvGrid(4, 16).valid());
}

TEST(MeshGenerator, DegenerateSubdivisionIsClamped)
{
    EXPECT_GT(sr::meshgen::sphere(1.0f, 1, 1).triangleCount(), 0);
    EXPECT_GT(sr::meshgen::planeGrid(1.0f, 0).triangleCount(), 0);
}

// 粗糙度必须影响反射采样：roughness=1 应取到最高 mip（全图平均），
// 而 roughness=0 取第 0 层（保留黑白对比）。旧实现无条件采第 0 层，
// 两个粗糙度会得到完全相同的值。
TEST(Cubemap, ReflectionRoughnessSelectsBlurrierLevel)
{
    sr::Image faces[6];
    for (int f = 0; f < 6; ++f)
    {
        faces[f].width = 8;
        faces[f].height = 8;
        faces[f].pixels.resize(64);
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                // 左半黑右半白：低 roughness 采到纯黑，高 roughness 取到全图平均
                faces[f].pixels[size_t(y) * 8 + x] = x < 4 ? Color(0, 0, 0) : Color(255, 255, 255);
    }
    Cubemap cube;
    cube.setFaces(faces);
    ASSERT_TRUE(cube.valid());

    // 该方向在 +X 面上映射到 u≈0.1、v=0.5，完全落在黑色区域内
    const Vec3f dir{1.0f, 0.0f, 0.8f};
    const Vec3f sharp = cube.sampleReflection(dir, 0.0f);
    const Vec3f blurred = cube.sampleReflection(dir, 1.0f);

    // 第 0 层保留左右对比，最高层是 1x1 的平均值（≈ 0.5）
    EXPECT_LT(sharp.x, 0.05f);
    EXPECT_NEAR(blurred.x, 0.5f, 0.1f);
}

// ==================== Skeleton ====================

