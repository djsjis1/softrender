// ============================================================
// 存储像素格式单元的测试：Color565 / Gray8 / BasicFramebuffer<PixelT> /
// convertFramebuffer / NearestUpscaler，以及"同一场景在不同格式下
// 只差量化误差"的端到端一致性。
//
// 【为什么必须用三顶点互异的数据】本项目曾因插值测试全部使用同值输入，
// 使权重排列错误 100% 被掩盖（见 TASK-002）。格式一致性同理：
// 同色三角形无论量化实现怎么错都能算出同一个值，故本文件所有
// 光栅化用例都让三个顶点颜色互不相等。
//
// 【测试依赖】只依赖 core / render / math / resource，不碰 io / runtime，
// 与 check_boundaries.cmake 的分层要求一致。
// ============================================================
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "softrenderer/core/color.h"
#include "softrenderer/core/depthbuffer.h"
#include "softrenderer/core/framebuffer.h"
#include "softrenderer/core/framebuffer_convert.h"
#include "softrenderer/core/image.h"
#include "softrenderer/core/pixel_format.h"
#include "softrenderer/math/vec.h"
#include "softrenderer/render/draw2d/blit2d.h"
#include "softrenderer/render/draw2d/shape2d.h"
#include "softrenderer/render/pipeline/renderer.h"
#include "softrenderer/render/pipeline/shader.h"
#include "softrenderer/render/raster/rasterizer_fast.h"
#include "softrenderer/render/raster/triangle.h"
#include "softrenderer/resource/mesh.h"

using namespace sr;

// ============================================================
// Color565：量化、扩展与往返
// ============================================================

TEST(PixelFormat, Color565SizeIsTwoBytes)
{
    static_assert(sizeof(Color565) == 2, "RGB565 必须恰好占 2 字节，否则帧缓冲内存核算失效");
    EXPECT_EQ(Color565::kBytes, 2u);
    EXPECT_EQ(Color::kBytes, 4u);
    EXPECT_EQ(Gray8::kBytes, 1u);
}

// 四舍五入 vs 截断：x=5 时 round(5*31/255)=1，而截断 (5>>3)=0。
// 这是"用移位近似代替除法"会引入的典型偏差，必须锁定。
TEST(PixelFormat, Color565QuantizeRoundsInsteadOfTruncating)
{
    EXPECT_EQ(Color565::quantize5(5), 1);
    EXPECT_EQ(5 >> 3, 0);
    EXPECT_EQ(Color565::quantize5(255), 31);
    EXPECT_EQ(Color565::quantize5(0), 0);
    EXPECT_EQ(Color565::quantize6(255), 63);
    EXPECT_EQ(Color565::quantize6(0), 0);
}

// 全输入范围上，量化后再扩展回来的误差不得超过半个量化步长。
// 5 位通道步长约 8.2，半个步长取整上界为 4；6 位通道为 2。
TEST(PixelFormat, Color565AllFiveBitValuesWithinHalfStep)
{
    int maxError = 0;
    for (int v = 0; v < 256; ++v)
    {
        const int expanded = int(Color565::expand5(Color565::quantize5(uint8_t(v))));
        maxError = std::max(maxError, std::abs(expanded - v));
    }
    EXPECT_LE(maxError, 4) << "5 位量化最大误差不得大于半个步长；实测 " << maxError;
}

TEST(PixelFormat, Color565AllSixBitValuesWithinHalfStep)
{
    int maxError = 0;
    for (int v = 0; v < 256; ++v)
    {
        const int expanded = int(Color565::expand6(Color565::quantize6(uint8_t(v))));
        maxError = std::max(maxError, std::abs(expanded - v));
    }
    EXPECT_LE(maxError, 2) << "6 位量化最大误差不得大于半个步长；实测 " << maxError;
}

TEST(PixelFormat, Color565BlackAndWhiteAreExact)
{
    const Color565 black = Color565::fromColor(Color(0, 0, 0));
    const Color565 white = Color565::fromColor(Color(255, 255, 255));
    EXPECT_EQ(black.v, 0u);
    EXPECT_EQ(white.v, 0xFFFFu);
    EXPECT_EQ(black.toColor(), Color(0, 0, 0));
    EXPECT_EQ(white.toColor(), Color(255, 255, 255));
}

// 穷举全部 65536 个取值：扩展再用位复制压缩必须回到原值。
// 这条不变量保证 convertFramebuffer 在 565→8888→565 的链路上不丢信息。
TEST(PixelFormat, Color565ExpandThenRequantizeIsIdentity)
{
    for (uint32_t raw = 0; raw < 65536u; ++raw)
    {
        // 用花括号避免 most vexing parse：圆括号会被解析成函数声明。
        const Color565 original{uint16_t(raw)};
        const Color565 roundTrip = Color565::fromColor(original.toColor());
        ASSERT_EQ(roundTrip.v, original.v) << "raw = " << raw;
    }
}

TEST(PixelFormat, Color565FromFloatClampsOutOfRange)
{
    EXPECT_EQ(Color565::fromFloat(-1.0f, -5.0f, -0.01f).v, 0u);
    EXPECT_EQ(Color565::fromFloat(2.0f, 3.0f, 9.0f).v, 0xFFFFu);
}

// 着色器输出 NaN 属常见事故（退化法线、非有限光照参数），
// Color::fromFloat 把它夹到 0，565 必须继承同一道闸门而不是自己实现钳制。
TEST(PixelFormat, Color565FromFloatNaNBecomesBlack)
{
    const float nan = std::nanf("");
    EXPECT_EQ(Color565::fromFloat(nan, nan, nan).v, 0u);
}

// 有意记录的能力边界：565 没有 alpha 通道，alpha 完全不参与存储。
// 因此 blend.h 的五种混合模式只能在 RGBA8888 上工作（ADR-005）。
TEST(PixelFormat, Color565IgnoresAlphaByDesign)
{
    EXPECT_EQ(Color565::fromColor(Color(200, 100, 50, 0)), Color565::fromColor(Color(200, 100, 50, 255)));
}

// ============================================================
// Gray8
// ============================================================

TEST(PixelFormat, Gray8BlackAndWhiteAreExact)
{
    EXPECT_EQ(Gray8::fromColor(Color(0, 0, 0)).v, 0u);
    EXPECT_EQ(Gray8::fromColor(Color(255, 255, 255)).v, 255u);
    EXPECT_EQ(Gray8(0).toColor(), Color(0, 0, 0));
    EXPECT_EQ(Gray8(128).toColor(), Color(128, 128, 128));
}

// 权重 76/150/29 中绿色最大：同等强度下绿色对结果影响最强。
// 若把三者顺序写反，纯红与纯蓝的结果会互换，这条会立刻失败。
TEST(PixelFormat, Gray8WeightsFavourGreen)
{
    const int red = Gray8::fromColor(Color(255, 0, 0)).v;    // 76  = 0.299 x 255
    const int green = Gray8::fromColor(Color(0, 255, 0)).v;  // 150 = 0.587 x 255
    const int blue = Gray8::fromColor(Color(0, 0, 255)).v;   // 29  = 0.114 x 255
    EXPECT_EQ(red, 76);
    EXPECT_EQ(green, 150);
    EXPECT_EQ(blue, 29);
    EXPECT_GT(green, red);
    EXPECT_GT(red, blue);
    // 权重和为 255，纯色通道必须精确映射（曾经用 77/150/29 + >>8 得 149）。
    EXPECT_EQ(Gray8::fromColor(Color(255, 255, 255)).v, 255u);
}

TEST(PixelFormat, Gray8IsMonotonicInLuminance)
{
    int previous = -1;
    for (int v = 0; v < 256; ++v)
    {
        const int gray = Gray8::fromColor(Color(uint8_t(v), uint8_t(v), uint8_t(v))).v;
        EXPECT_GE(gray, previous) << "灰阶 v = " << v;
        previous = gray;
    }
}

// ============================================================
// BasicFramebuffer<PixelT>
// ============================================================

TEST(FramebufferFormat, RgbaAliasKeepsFourBytesPerPixel)
{
    Framebuffer fb(8, 4);
    EXPECT_EQ(Framebuffer::bytesPerPixel(), 4u);
    EXPECT_EQ(fb.byteSize(), size_t(8) * 4 * 4);
}

TEST(FramebufferFormat, Framebuffer565ByteSizeIsHalfOfRgba)
{
    BasicFramebuffer<Color565> fb(16, 8);
    Framebuffer rgba(16, 8);
    EXPECT_EQ(fb.byteSize(), rgba.byteSize() / 2);
    EXPECT_EQ(fb.width(), 16);
    EXPECT_EQ(fb.height(), 8);
}

TEST(FramebufferFormat, Framebuffer565SetPixelMatchesQuantization)
{
    BasicFramebuffer<Color565> fb(4, 4);
    fb.setPixel(1, 2, Color(200, 100, 50));
    EXPECT_EQ(fb.data()[size_t(2) * 4 + 1], Color565::fromColor(Color(200, 100, 50)));
    // 回读统一还原成 RGBA8888，且落在半个量化步长内。
    const Color back = fb.getPixel(1, 2);
    EXPECT_LE(std::abs(int(back.r) - 200), 4);
    EXPECT_LE(std::abs(int(back.g) - 100), 2);
    EXPECT_LE(std::abs(int(back.b) - 50), 4);
}

TEST(FramebufferFormat, Framebuffer565ClearUsesFillColor)
{
    BasicFramebuffer<Color565> fb(3, 3, Color(255, 0, 0));
    const Color565 expected = Color565::fromColor(Color(255, 0, 0));
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x)
            EXPECT_EQ(fb.getPixel(x, y), expected.toColor());
}

TEST(FramebufferFormat, Framebuffer565RowWritesMatchSetPixel)
{
    BasicFramebuffer<Color565> fb(4, 2);
    const Color values[4] = {Color(10, 20, 30), Color(200, 90, 5), Color(0, 255, 128), Color(255, 255, 0)};
    for (int x = 0; x < 4; ++x)
        fb.setPixel(x, 1, values[x]);
    const Color565* row = fb.row(1);
    for (int x = 0; x < 4; ++x)
        EXPECT_EQ(row[x], Color565::fromColor(values[x])) << "x = " << x;
}

TEST(FramebufferFormat, Framebuffer565OutOfBoundsIsIgnored)
{
    BasicFramebuffer<Color565> fb(2, 2, Color(0, 0, 0));
    fb.setPixel(-1, 0, Color(255, 255, 255));
    fb.setPixel(0, -1, Color(255, 255, 255));
    fb.setPixel(2, 0, Color(255, 255, 255));
    fb.setPixel(0, 2, Color(255, 255, 255));
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
            EXPECT_EQ(fb.getPixel(x, y), Color(0, 0, 0));
}

// ============================================================
// convertFramebuffer
// ============================================================

TEST(FramebufferConvert, RgbaTo565MatchesPerPixelQuantization)
{
    Framebuffer src(5, 3);
    BasicFramebuffer<Color565> dst(5, 3);
    // 每个像素给不同值，避免"全同色掩盖实现错误"。
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 5; ++x)
            src.setPixel(x, y, Color(uint8_t(20 + x * 40), uint8_t(200 - y * 60), uint8_t(10 + x * 30 + y)));

    convertFramebuffer(src, dst);

    for (int y = 0; y < 3; ++y)
    {
        for (int x = 0; x < 5; ++x)
        {
            const Color565 expected = Color565::fromColor(src.getPixel(x, y));
            EXPECT_EQ(dst.data()[size_t(y) * 5 + x], expected) << "(" << x << "," << y << ")";
        }
    }
}

TEST(FramebufferConvert, RoundTripThroughRgbaPreservesAll565Values)
{
    BasicFramebuffer<Color565> src(4, 2);
    Framebuffer middle(4, 2);
    BasicFramebuffer<Color565> back(4, 2);
    for (int i = 0; i < 8; ++i)
        src.data()[size_t(i)] = Color565(uint16_t(0x1000u * unsigned(i) + 0x0F0Fu));

    convertFramebuffer(src, middle);
    convertFramebuffer(middle, back);

    for (int i = 0; i < 8; ++i)
        EXPECT_EQ(back.data()[size_t(i)], src.data()[size_t(i)]) << "index " << i;
}

TEST(FramebufferConvert, ConvertUsesSmallerOverlap)
{
    Framebuffer src(8, 8, Color(255, 255, 255));
    BasicFramebuffer<Color565> dst(3, 2, Color(0, 0, 0));
    convertFramebuffer(src, dst);
    // 重叠区域被覆盖，尺寸由较小者决定，不越界。
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 3; ++x)
            EXPECT_EQ(dst.getPixel(x, y), Color(255, 255, 255));
}

TEST(FramebufferConvert, ConvertToGray8UsesLuminance)
{
    Framebuffer src(2, 1);
    src.setPixel(0, 0, Color(255, 0, 0));
    src.setPixel(1, 0, Color(0, 255, 0));
    BasicFramebuffer<Gray8> dst(2, 1);
    convertFramebuffer(src, dst);
    EXPECT_EQ(dst.data()[0].v, 76u);
    EXPECT_EQ(dst.data()[1].v, 150u);
}

// ============================================================
// NearestUpscaler
// ============================================================

TEST(NearestUpscaler, IntegerScaleDuplicatesSourcePixels)
{
    Framebuffer src(2, 2);
    src.setPixel(0, 0, Color(10, 20, 30));
    src.setPixel(1, 0, Color(200, 100, 50));
    src.setPixel(0, 1, Color(1, 2, 3));
    src.setPixel(1, 1, Color(250, 240, 230));

    BasicFramebuffer<Color565> dst(4, 4);
    NearestUpscaler up;
    up.resize(2, 2, 4, 4);
    ASSERT_TRUE(up.valid());
    up.present(src, dst);

    // 2× 最近邻：每个源像素必须正好铺满 2×2。
    for (int y = 0; y < 4; ++y)
    {
        for (int x = 0; x < 4; ++x)
        {
            const Color expectedSrc = src.getPixel(x / 2, y / 2);
            EXPECT_EQ(dst.getPixel(x, y), Color565::fromColor(expectedSrc).toColor())
                << "(x,y) = (" << x << "," << y << ")";
        }
    }
}

TEST(NearestUpscaler, ThreeTimesScaleMapsCorrectly)
{
    Framebuffer src(3, 1);
    src.setPixel(0, 0, Color(10, 0, 0));
    src.setPixel(1, 0, Color(20, 0, 0));
    src.setPixel(2, 0, Color(30, 0, 0));

    Framebuffer dst(9, 1);
    NearestUpscaler up;
    up.resize(3, 1, 9, 1);
    up.present(src, dst);

    for (int x = 0; x < 9; ++x)
    {
        const Color expected = src.getPixel(x / 3, 0);
        EXPECT_EQ(dst.getPixel(x, 0), expected.toColor()) << "x = " << x;
    }
}

TEST(NearestUpscaler, DownscalePicksNearest)
{
    Framebuffer src(4, 4);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            src.setPixel(x, y, Color(uint8_t(x * 60), uint8_t(y * 60), 0));

    Framebuffer dst(2, 2);
    NearestUpscaler up;
    up.resize(4, 4, 2, 2);
    up.present(src, dst);

    // 4→2 的映射为 {0,2}：取每两个源像素的第一个。
    EXPECT_EQ(dst.getPixel(0, 0), src.getPixel(0, 0));
    EXPECT_EQ(dst.getPixel(1, 0), src.getPixel(2, 0));
    EXPECT_EQ(dst.getPixel(0, 1), src.getPixel(0, 2));
    EXPECT_EQ(dst.getPixel(1, 1), src.getPixel(2, 2));
}

TEST(NearestUpscaler, InvalidSizeIsNoOp)
{
    NearestUpscaler up;
    up.resize(0, 4, 8, 8);
    EXPECT_FALSE(up.valid());

    Framebuffer src(4, 4, Color(255, 255, 255));
    Framebuffer dst(8, 8, Color(0, 0, 0));
    up.present(src, dst);
    EXPECT_EQ(dst.getPixel(0, 0), Color(0, 0, 0)) << "无效尺寸下不得写入任何像素";
}

TEST(NearestUpscaler, PresentRowOutOfRangeIsNoOp)
{
    Framebuffer src(2, 2, Color(255, 0, 0));
    Framebuffer dst(4, 4, Color(0, 0, 0));
    NearestUpscaler up;
    up.resize(2, 2, 4, 4);

    up.presentRow(src, -1, dst);
    up.presentRow(src, 4, dst);
    for (int y = 0; y < 4; ++y)
        EXPECT_EQ(dst.getPixel(0, y), Color(0, 0, 0));
}

TEST(NearestUpscaler, PresentEqualsRowByRow)
{
    Framebuffer src(3, 3);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x)
            src.setPixel(x, y, Color(uint8_t(40 * x), uint8_t(70 * y), uint8_t(10 * (x + y))));

    Framebuffer whole(6, 6);
    Framebuffer byRows(6, 6);
    NearestUpscaler up;
    up.resize(3, 3, 6, 6);
    up.present(src, whole);
    for (int y = 0; y < 6; ++y)
        up.presentRow(src, y, byRows);

    for (int y = 0; y < 6; ++y)
        for (int x = 0; x < 6; ++x)
            EXPECT_EQ(byRows.getPixel(x, y), whole.getPixel(x, y)) << "(" << x << "," << y << ")";
}

// 源尺寸与 resize 时声明的不一致属契约破坏：Release 下必须静默返回而不是越界读。
// 与 core_buffer_test 的越界哨兵同一写法：Debug 下 assert 会先中止进程，只能跳过。
TEST(NearestUpscaler, MismatchedSourceSizeIsNoOp)
{
#ifndef NDEBUG
    GTEST_SKIP() << "契约破坏哨兵仅在 Release（NDEBUG）下可测：Debug 下 assert 会先中止进程";
#else
    NearestUpscaler up;
    up.resize(4, 4, 8, 8);

    Framebuffer wrongSize(2, 2, Color(255, 255, 255));
    Framebuffer dst(8, 8, Color(0, 0, 0));
    up.present(wrongSize, dst);
    EXPECT_EQ(dst.getPixel(0, 0), Color(0, 0, 0));
#endif
}

// ============================================================
// 光栅化：同一场景在不同格式下只应差一个量化误差
// ============================================================

namespace
{

// 借用 normal 字段承载顶点颜色，避免为测试给 Vertex 增加属性
// （Vertex 是 resource/render 的公共交汇类型，不应为测试改动）。
struct NormalAsColorShader : Shader
{
    Vec4f vertex(const Vertex& in, Varyings& out) override
    {
        out.color = in.normal;
        return Vec4f{in.position.x, in.position.y, in.position.z, 1.0f};
    }

    bool fragment(const Varyings& v, Vec3f& color) override
    {
        color = v.color;
        return true;
    }
};

// 三顶点位置与颜色都互不相等：同色或共面退化会掩盖权重排列与量化错误。
//
// 【为什么不用 Vertex{{pos},{color}} 聚合初始化】Vertex 有四个成员，
// 只写两个会触发 GCC/Clang 的 -Wmissing-field-initializers，而 -Wextra
// 配 -Werror 会把它升级成错误；MSVC 只警告，所以本机全绿照样挂 CI。
Vertex makeVertex(Vec3f position, Vec3f color)
{
    Vertex v;
    v.position = position;
    v.normal = color;  // 借用 normal 字段承载顶点色，避免为测试改动 Vertex
    return v;
}

Mesh makeGradientTriangle()
{
    Mesh mesh;
    mesh.vertices.push_back(makeVertex(Vec3f{-0.8f, -0.7f, 0.0f}, Vec3f{0.9f, 0.2f, 0.1f}));
    mesh.vertices.push_back(makeVertex(Vec3f{0.75f, -0.55f, 0.0f}, Vec3f{0.1f, 0.85f, 0.35f}));
    mesh.vertices.push_back(makeVertex(Vec3f{-0.05f, 0.8f, 0.0f}, Vec3f{0.2f, 0.3f, 0.95f}));
    mesh.indices = {0, 1, 2};
    return mesh;
}

// 任意两个格式的同一像素，逐通道差值不得超过该通道的半个量化步长。
void expectWithinQuantization(const Color& actual, const Color& reference, int tolerance, const char* what)
{
    EXPECT_LE(std::abs(int(actual.r) - int(reference.r)), tolerance) << what << " R";
    EXPECT_LE(std::abs(int(actual.g) - int(reference.g)), tolerance) << what << " G";
    EXPECT_LE(std::abs(int(actual.b) - int(reference.b)), tolerance) << what << " B";
}

}  // namespace

TEST(FormatRasterization, DrawMesh565Matches8888WithinQuantization)
{
    const int w = 48, h = 48;
    Framebuffer fb8888(w, h);
    BasicFramebuffer<Color565> fb565(w, h);

    Renderer renderer(w, h);
    NormalAsColorShader shader;
    const Mesh mesh = makeGradientTriangle();

    renderer.clear(fb8888, Color(0, 0, 0));
    renderer.drawMesh(fb8888, mesh, shader);

    Renderer renderer565(w, h);  // 独立深度缓冲，保证两次运行起点一致
    renderer565.clear(fb565, Color(0, 0, 0));
    renderer565.drawMesh(fb565, mesh, shader);

    int shaded = 0;
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const Color a = fb8888.getPixel(x, y);
            const Color b = fb565.getPixel(x, y);
            if (a != Color(0, 0, 0))
                ++shaded;
            expectWithinQuantization(b, a, 4, "drawMesh");
        }
    }
    // 防止"两边都是全黑所以一致"的假通过：必须有实际着色面积，
    // 且三角形内部颜色互不相同（证明确实走了插值而不是常量填充）。
    EXPECT_GT(shaded, w * h / 4) << "三角形覆盖面积异常，比较无意义";
    EXPECT_NE(fb8888.getPixel(w / 2, h / 2), fb8888.getPixel(w / 2 + 6, h / 2 + 6))
        << "内部颜色应当随位置变化";
}

TEST(FormatRasterization, FillTriangle565Matches8888WithinQuantization)
{
    const int w = 32, h = 32;
    Framebuffer fb8888(w, h);
    BasicFramebuffer<Color565> fb565(w, h);
    const Vec2f v0{2.0f, 3.0f}, v1{29.0f, 5.0f}, v2{11.0f, 28.0f};
    const Color color(203, 117, 61);

    sr::fillTriangle(fb8888, v0, v1, v2, color);
    sr::fillTriangle(fb565, v0, v1, v2, color);

    int covered = 0;
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const Color a = fb8888.getPixel(x, y);
            const Color b = fb565.getPixel(x, y);
            if (a != Color(0, 0, 0))
                ++covered;
            expectWithinQuantization(b, a, 4, "fillTriangle");
        }
    }
    EXPECT_GT(covered, 0) << "纯色三角形必须覆盖到像素";
}

TEST(FormatRasterization, FillTriangleGradient565Matches8888WithinQuantization)
{
    const int w = 32, h = 32;
    Framebuffer fb8888(w, h);
    BasicFramebuffer<Color565> fb565(w, h);
    const Vec2f v0{1.0f, 2.0f}, v1{30.0f, 4.0f}, v2{8.0f, 30.0f};
    const Color c0(240, 20, 20), c1(20, 230, 40), c2(30, 40, 240);

    sr::fillTriangleGradient(fb8888, v0, v1, v2, c0, c1, c2);
    sr::fillTriangleGradient(fb565, v0, v1, v2, c0, c1, c2);

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
            expectWithinQuantization(fb565.getPixel(x, y), fb8888.getPixel(x, y), 4, "gradient");
    }
    // 三顶点色互异 ⇒ 三角形内部必须出现多种颜色。
    EXPECT_NE(fb8888.getPixel(8, 8), fb8888.getPixel(20, 10));
}

TEST(FormatRasterization, RasterizerFast565Matches8888WithinQuantization)
{
    const int w = 32, h = 32;
    Framebuffer fb8888(w, h);
    BasicFramebuffer<Color565> fb565(w, h);
    DepthBuffer zb8888(w, h), zb565(w, h);
    zb8888.clear();
    zb565.clear();
    const Vec2f v0{2.0f, 2.0f}, v1{28.0f, 6.0f}, v2{10.0f, 27.0f};

    sr::raster::fillTriangleFast(fb8888, zb8888, v0, v1, v2, 0.25f, 0.5f, 0.75f, Color(90, 180, 250));
    sr::raster::fillTriangleFast(fb565, zb565, v0, v1, v2, 0.25f, 0.5f, 0.75f, Color(90, 180, 250));

    int covered = 0;
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const Color a = fb8888.getPixel(x, y);
            if (a != Color(0, 0, 0))
                ++covered;
            expectWithinQuantization(fb565.getPixel(x, y), a, 4, "rasterizerFast");
        }
    }
    EXPECT_GT(covered, 0);
    // 深度写回必须在两种格式下一致：深度与像素格式无关。
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            EXPECT_FLOAT_EQ(zb565.get(x, y), zb8888.get(x, y));
}

// ============================================================
// 2D 子系统：图元可以画进任意存储格式
//
// 【最强的断言是"覆盖范围与格式无关"】光栅化规则（Bresenham、扫描线、
// scissor）不应知道目标像素占几个字节。因此下面不仅比颜色容差，
// 还逐像素比对"是否被写过"，任何格式分支写错都会在这里暴露。
//
// 【容差为什么不同】不透明图元只经历一次量化 → 半个量化步长（4）。
// 带混合的图元要把 dst 读回 8888 再量化一次，误差上界翻倍；
// 其中 Multiply 因含 dst×dst，dst 的 ±4 误差被放大到约 ±8，
// 故那一个用例给 12 的上界。
// ============================================================

namespace
{
constexpr int kOpaqueTolerance = 4;
constexpr int kBlendTolerance = 8;
constexpr int kMultiplyTolerance = 12;

int channelDiff(const Color& x, const Color& y)
{
    return std::max({std::abs(int(x.r) - int(y.r)), std::abs(int(x.g) - int(y.g)), std::abs(int(x.b) - int(y.b))});
}
}  // namespace

TEST(Draw2DFormat, OpaquePrimitivesCoverIdenticalPixels)
{
    const int w = 96, h = 96;
    Framebuffer a(w, h);
    BasicFramebuffer<Color565> b(w, h);
    const Color bg = colors::black;

    // 混合多种图元：矩形/圆/线/粗线/椭圆/多边形，覆盖各自的扫描线实现
    auto paint = [](auto& fb)
    {
        draw2d::fillRect(fb, 6, 10, 40, 24, Color(220, 30, 40));
        draw2d::fillCircle(fb, 64.0f, 32.0f, 18.0f, Color(20, 200, 90));
        draw2d::drawLine(fb, 4, 60, 92, 88, Color(30, 60, 240));
        draw2d::drawLineThick(fb, 10, 90, 60, 62, Color(250, 200, 20), 3);
        draw2d::fillEllipse(fb, 70.0f, 70.0f, 20.0f, 12.0f, Color(150, 60, 200));
        std::vector<Vec2f> poly = {{5.0f, 20.0f}, {30.0f, 8.0f}, {52.0f, 26.0f}, {20.0f, 40.0f}};
        draw2d::fillPolygon(fb, poly, Color(60, 180, 220));
    };
    paint(a);
    paint(b);

    int coverageMismatch = 0;
    int litPixels = 0;
    int maxDiff = 0;
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const Color ca = a.getPixel(x, y);
            const Color cb = b.getPixel(x, y);
            const bool litA = !(ca == bg);
            const bool litB = !(cb == bg);
            if (litA != litB)
                ++coverageMismatch;
            if (litA)
            {
                ++litPixels;
                maxDiff = std::max(maxDiff, channelDiff(ca, cb));
            }
        }
    }
    EXPECT_EQ(coverageMismatch, 0) << "2D 覆盖范围必须与像素格式无关";
    EXPECT_GT(litPixels, w * h / 10) << "必须有实际绘制面积，否则比较无意义";
    EXPECT_GT(maxDiff, 0) << "565 应产生可测量的量化差异；为 0 说明根本没走量化";
    EXPECT_LE(maxDiff, kOpaqueTolerance);
}

TEST(Draw2DFormat, AlphaBlendMatchesOnOpaqueBackdrop)
{
    const int w = 64, h = 64;
    Framebuffer a(w, h);
    BasicFramebuffer<Color565> b(w, h);
    const Color bg(30, 40, 50);  // 不透明背景：565 上混合能成立的前提
    a.clear(bg);
    b.clear(bg);

    auto paint = [](auto& fb)
    {
        draw2d::fillRect(fb, 4, 4, 30, 30, Color(200, 100, 50, 128), BlendMode::AlphaBlend);
        draw2d::fillRect(fb, 20, 20, 30, 30, Color(60, 220, 120, 200), BlendMode::Additive);
    };
    paint(a);
    paint(b);

    int maxDiff = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            maxDiff = std::max(maxDiff, channelDiff(a.getPixel(x, y), b.getPixel(x, y)));
    EXPECT_LE(maxDiff, kBlendTolerance);
}

// Multiply 含 dst×dst，dst 的量化误差被放大一倍，故单独给更宽的上界。
// 这条仍能抓住"通道顺序写反"或"没做量化"这类粗错。
TEST(Draw2DFormat, MultiplyBlendMatchesOnOpaqueBackdrop)
{
    const int w = 48, h = 48;
    Framebuffer a(w, h);
    BasicFramebuffer<Color565> b(w, h);
    const Color bg(210, 180, 120);
    a.clear(bg);
    b.clear(bg);

    auto paint = [](auto& fb)
    { draw2d::fillRect(fb, 6, 6, 36, 36, Color(120, 200, 90, 140), BlendMode::Multiply); };
    paint(a);
    paint(b);

    int maxDiff = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            maxDiff = std::max(maxDiff, channelDiff(a.getPixel(x, y), b.getPixel(x, y)));
    EXPECT_LE(maxDiff, kMultiplyTolerance);
}

TEST(Draw2DFormat, ScissorClipIsFormatIndependent)
{
    const int w = 40, h = 40;
    Framebuffer a(w, h);
    BasicFramebuffer<Color565> b(w, h);
    a.clear(colors::black);
    b.clear(colors::black);

    const draw2d::ClipRect clip(0, 0, 20, 40);  // 只允许左半屏
    auto paint = [&clip](auto& fb)
    {
        draw2d::fillRect(fb, 0, 0, 40, 40, Color(240, 120, 60), BlendMode::Opaque, &clip);
    };
    paint(a);
    paint(b);

    // scissor 右侧必须两边都保持黑色，左侧颜色差在量化范围内
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const Color ca = a.getPixel(x, y);
            const Color cb = b.getPixel(x, y);
            if (x >= 20)
            {
                EXPECT_EQ(ca, colors::black) << "scissor 外不应被写: (" << x << "," << y << ")";
                EXPECT_EQ(cb, colors::black) << "scissor 外不应被写: (" << x << "," << y << ")";
            }
            else
            {
                EXPECT_LE(channelDiff(ca, cb), kOpaqueTolerance);
            }
        }
    }
}

TEST(Draw2DFormat, BlitImageOn565MatchesQuantization)
{
    sr::Image img;
    img.width = 8;
    img.height = 8;
    img.pixels.resize(64);
    // 每像素互不相同的颜色：同色位图无法暴露通道顺序与索引错误
    for (int i = 0; i < 64; ++i)
        img.pixels[size_t(i)] = Color(uint8_t(i * 4), uint8_t(255 - i * 4), uint8_t(i * 3));

    Framebuffer a(16, 16);
    BasicFramebuffer<Color565> b(16, 16);
    a.clear(colors::black);
    b.clear(colors::black);
    draw2d::blitImage(a, img, 2, 3);
    draw2d::blitImage(b, img, 2, 3);

    int maxDiff = 0;
    int litPixels = 0;
    for (int y = 0; y < 16; ++y)
    {
        for (int x = 0; x < 16; ++x)
        {
            const Color ca = a.getPixel(x, y);
            const Color cb = b.getPixel(x, y);
            if (!(ca == colors::black))
                ++litPixels;
            maxDiff = std::max(maxDiff, channelDiff(ca, cb));
        }
    }
    EXPECT_EQ(litPixels, 64) << "8x8 位图必须完整贴上";
    EXPECT_LE(maxDiff, kOpaqueTolerance);
}

// 灰度目标：同一图元画进去后必须等于"逐像素按亮度公式量化"的结果。
TEST(Draw2DFormat, PrimitivesRenderToGray8WithLuminance)
{
    const int w = 32, h = 32;
    Framebuffer ref(w, h);
    BasicFramebuffer<Gray8> gray(w, h);
    ref.clear(colors::black);
    gray.clear(colors::black);

    draw2d::fillRect(ref, 4, 4, 20, 20, Color(200, 80, 40));
    draw2d::fillRect(gray, 4, 4, 20, 20, Color(200, 80, 40));

    for (int y = 0; y < 32; ++y)
    {
        for (int x = 0; x < 32; ++x)
        {
            const uint8_t expected = Gray8::fromColor(ref.getPixel(x, y)).v;
            EXPECT_EQ(gray.getPixel(x, y).r, expected) << "(" << x << "," << y << ")";
        }
    }
}

// 灰度路径：同一场景转成灰度后必须等于"逐像素按亮度公式量化"的结果。
// 若 convertFramebuffer 里误用了 565 的权重，这里会失败。
TEST(FormatRasterization, Gray8ConversionMatchesPerPixelLuminance)
{
    const int w = 16, h = 16;
    Framebuffer src(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            src.setPixel(x, y, Color(uint8_t(x * 15), uint8_t(y * 15), uint8_t(255 - x * 15)));

    BasicFramebuffer<Gray8> gray(w, h);
    convertFramebuffer(src, gray);

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const uint8_t expected = Gray8::fromColor(src.getPixel(x, y)).v;
            EXPECT_EQ(gray.data()[size_t(y) * w + x].v, expected) << "(" << x << "," << y << ")";
        }
    }
}

// ============================================================
// 同格式快速路径（convertFramebuffer / NearestUpscaler）
//
// 源与目标像素类型相同时走整行拷贝或直接列映射，不再做
// "展开成 RGBA8888 再量化回来"的往返。虽然该往返对 565 是无损的
// （有 65536 值穷举测试锁定），但每像素白付一次解包 + 两次乘除。
// 下面用**原始位模式**（而非经 Color 往返）比对，确保快速路径逐位一致。
// ============================================================

TEST(FramebufferConvert, SameFormatCopyIsBitExact)
{
    BasicFramebuffer<Color565> src(8, 4);
    // 覆盖全 0 / 全 1 / 无符号位翻转 / 各通道边界，避免只测少数取值
    const uint16_t raw[8] = {0x0000, 0xFFFF, 0x0821, 0x7BEF, 0x8410, 0xF81F, 0x07E0, 0x1234};
    for (int i = 0; i < 32; ++i)
        src.data()[size_t(i)] = Color565(raw[i % 8]);

    BasicFramebuffer<Color565> dst(8, 4);
    convertFramebuffer(src, dst);

    for (int i = 0; i < 32; ++i)
        EXPECT_EQ(dst.data()[size_t(i)].v, src.data()[size_t(i)].v) << "index " << i;
}

TEST(NearestUpscaler, SameFormatUpscaleIsBitExact)
{
    BasicFramebuffer<Color565> src(2, 2);
    src.data()[0] = Color565(uint16_t(0x0821));
    src.data()[1] = Color565(uint16_t(0xF81F));
    src.data()[2] = Color565(uint16_t(0x07E0));
    src.data()[3] = Color565(uint16_t(0x7BEF));

    BasicFramebuffer<Color565> dst(4, 4);
    NearestUpscaler up;
    up.resize(2, 2, 4, 4);
    up.present(src, dst);

    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            EXPECT_EQ(dst.data()[size_t(y) * 4 + x].v, src.data()[size_t(y / 2) * 2 + (x / 2)].v)
                << "(" << x << "," << y << ")";
}

// 灰度同格式同样走快速路径：权重公式不参与，必须逐位相等。
TEST(FramebufferConvert, SameFormatGrayCopyIsBitExact)
{
    BasicFramebuffer<Gray8> src(4, 2);
    for (int i = 0; i < 8; ++i)
        src.data()[size_t(i)] = Gray8(uint8_t(i * 33));

    BasicFramebuffer<Gray8> dst(4, 2);
    convertFramebuffer(src, dst);

    for (int i = 0; i < 8; ++i)
        EXPECT_EQ(dst.data()[size_t(i)].v, src.data()[size_t(i)].v) << "index " << i;
}
