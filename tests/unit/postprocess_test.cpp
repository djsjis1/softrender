// ============================================================
// 后处理单元测试：色调映射、gamma、亮部提取、高斯模糊与加法合成
//
// 这些效果在 2.0.0 从 `FloatFramebuffer` 的成员方法迁出为
// `sr::postprocess` 的自由函数。本文件用**独立算出的期望值**锁定数值公式，
// 而不是拿两个实现互相验证。
// ============================================================
#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "softrenderer/core/float_framebuffer.h"
#include "softrenderer/core/framebuffer.h"
#include "softrenderer/math/transform.h"
#include "softrenderer/render/postprocess/post_processing.h"

using namespace sr;

namespace
{
// ACES 近似公式（与实现同源，但期望值在这里独立代入常数，避免"实现复制到测试"）
float acesRef(float v)
{
    const float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    return std::clamp((v * (a * v + b)) / (v * (c * v + d) + e), 0.0f, 1.0f);
}
} // namespace

TEST(PostProcess, AcesToneMapKnownValues)
{
    const Vec3f black = postprocess::acesToneMap(Vec3f{0.0f, 0.0f, 0.0f});
    EXPECT_FLOAT_EQ(black.x, 0.0f);
    EXPECT_FLOAT_EQ(black.y, 0.0f);
    EXPECT_FLOAT_EQ(black.z, 0.0f);

    // 0.5 → 0.6425 / 1.0425，1.0 → 2.54 / 3.16
    EXPECT_NEAR(postprocess::acesToneMap(Vec3f{0.5f, 0.5f, 0.5f}).x, 0.616307f, 1e-5f);
    EXPECT_NEAR(postprocess::acesToneMap(Vec3f{1.0f, 1.0f, 1.0f}).x, 0.803797f, 1e-5f);
    // 4.0 已进入肩部：单调递增但被压到 1 以下
    const float bright = postprocess::acesToneMap(Vec3f{4.0f, 4.0f, 4.0f}).x;
    EXPECT_GT(bright, 0.803797f);
    EXPECT_LT(bright, 1.0f);
}

TEST(PostProcess, GammaCorrectUsesInverse22)
{
    // 0.25^(1/2.2) = 0.5325205；负值按 0 截断
    EXPECT_NEAR(postprocess::gammaCorrect(Vec3f{0.25f, 0.25f, 0.25f}).x, 0.5325205f, 1e-6f);
    EXPECT_NEAR(postprocess::gammaCorrect(Vec3f{1.0f, 1.0f, 1.0f}).x, 1.0f, 1e-6f);
    EXPECT_FLOAT_EQ(postprocess::gammaCorrect(Vec3f{-1.0f, -1.0f, -1.0f}).x, 0.0f);
}

// resolveToLdr 用的是查表版 gamma：必须与 pow 版在整个 [0,1] 上一致到 1e-4
// （8 位下 0.03 LSB），否则“性能优化”会悄然改变所有 HDR 输出的颜色。
TEST(PostProcess, GammaCorrectFastMatchesPowWithinLsbTolerance)
{
    float maxError = 0.0f;
    for (int i = 0; i <= 100000; ++i)
    {
        const float v = float(i) / 100000.0f;
        const float exact = std::pow(v, 1.0f / 2.2f);
        const float fast = postprocess::gammaCorrectFast(v);
        maxError = std::max(maxError, std::abs(fast - exact));
    }
    EXPECT_LT(maxError, 1e-4f);

    // 边界与 NaN 传播必须与 pow 版一致
    EXPECT_FLOAT_EQ(postprocess::gammaCorrectFast(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(postprocess::gammaCorrectFast(-5.0f), 0.0f);
    EXPECT_FLOAT_EQ(postprocess::gammaCorrectFast(2.0f), 1.0f);
    EXPECT_TRUE(std::isnan(postprocess::gammaCorrectFast(std::nanf(""))));
}

TEST(PostProcess, BrightPassKeepsOnlyAboveThreshold)
{
    FloatFramebuffer hdr(2, 2);
    hdr.setPixel(0, 0, Vec3f{0.2f, 0.2f, 0.2f}); // 亮度 0.2
    hdr.setPixel(1, 0, Vec3f{1.0f, 2.0f, 3.0f}); // 亮度 1.8596
    hdr.setPixel(0, 1, Vec3f{0.5f, 0.5f, 0.5f}); // 亮度 0.5
    hdr.setPixel(1, 1, Vec3f{4.0f, 0.0f, 0.0f}); // 亮度 0.8504

    FloatFramebuffer dst(2, 2);
    postprocess::brightPass(hdr, dst, 0.6f);

    // 低于阈值的像素必须严格归零（不是保留原值、也不是缩放）
    EXPECT_FLOAT_EQ(dst.getPixel(0, 0).x, 0.0f);
    EXPECT_FLOAT_EQ(dst.getPixel(0, 1).x, 0.0f);
    // 高于阈值的像素原样保留（阈值只做筛选，不做衰减）
    EXPECT_FLOAT_EQ(dst.getPixel(1, 0).y, 2.0f);
    EXPECT_FLOAT_EQ(dst.getPixel(1, 0).z, 3.0f);
    EXPECT_FLOAT_EQ(dst.getPixel(1, 1).x, 4.0f);

    // 提取不得修改源缓冲
    EXPECT_FLOAT_EQ(hdr.getPixel(0, 0).x, 0.2f);
    EXPECT_FLOAT_EQ(hdr.getPixel(1, 0).z, 3.0f);
}

TEST(PostProcess, GaussianBlurPreservesConstantField)
{
    // 常数场在归一化卷积核下必须逐像素不变（含边界 clamp 采样）
    FloatFramebuffer src(5, 4);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 5; ++x)
            src.setPixel(x, y, Vec3f{0.3f, 0.6f, 0.9f});

    FloatFramebuffer dst(5, 4);
    postprocess::gaussianBlur(src, dst, true, 2);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 5; ++x)
        {
            EXPECT_NEAR(dst.getPixel(x, y).x, 0.3f, 1e-6f) << "x=" << x << " y=" << y;
            EXPECT_NEAR(dst.getPixel(x, y).z, 0.9f, 1e-6f) << "x=" << x << " y=" << y;
        }
}

TEST(PostProcess, GaussianBlurSpreadsImpulse)
{
    // 单点脉冲经水平模糊后，中心降低、邻域升高，且总量守恒
    FloatFramebuffer src(7, 1);
    src.setPixel(3, 0, Vec3f{1.0f, 1.0f, 1.0f});

    FloatFramebuffer dst(7, 1);
    postprocess::gaussianBlur(src, dst, true, 2);

    EXPECT_LT(dst.getPixel(3, 0).x, 1.0f);
    EXPECT_GT(dst.getPixel(2, 0).x, 0.0f);
    EXPECT_GT(dst.getPixel(4, 0).x, 0.0f);
    // 对称核 → 左右邻域相等
    EXPECT_NEAR(dst.getPixel(2, 0).x, dst.getPixel(4, 0).x, 1e-6f);

    float total = 0.0f;
    for (int x = 0; x < 7; ++x)
        total += dst.getPixel(x, 0).x;
    EXPECT_NEAR(total, 1.0f, 1e-5f);
}

TEST(PostProcess, AddBlendAccumulatesIntensity)
{
    FloatFramebuffer dst(2, 1);
    dst.setPixel(0, 0, Vec3f{1.0f, 1.0f, 1.0f});
    dst.setPixel(1, 0, Vec3f{0.0f, 0.5f, 1.0f});

    FloatFramebuffer src(2, 1);
    src.setPixel(0, 0, Vec3f{0.5f, 0.25f, 0.0f});
    src.setPixel(1, 0, Vec3f{1.0f, 1.0f, 1.0f});

    postprocess::addBlend(dst, src, 2.0f);

    EXPECT_FLOAT_EQ(dst.getPixel(0, 0).x, 2.0f); // 1.0 + 0.5 * 2
    EXPECT_FLOAT_EQ(dst.getPixel(0, 0).y, 1.5f); // 1.0 + 0.25 * 2
    EXPECT_FLOAT_EQ(dst.getPixel(0, 0).z, 1.0f); // 1.0 + 0.0 * 2
    EXPECT_FLOAT_EQ(dst.getPixel(1, 0).x, 2.0f); // 0.0 + 1.0 * 2
    EXPECT_FLOAT_EQ(dst.getPixel(1, 0).y, 2.5f);
}

TEST(PostProcess, ResolveToLdrIsTonemapThenGamma)
{
    FloatFramebuffer hdr(2, 1);
    hdr.setPixel(0, 0, Vec3f{0.0f, 0.0f, 0.0f});
    hdr.setPixel(1, 0, Vec3f{1.0f, 1.0f, 1.0f});

    Framebuffer ldr(2, 1, Color(0, 0, 0));
    postprocess::resolveToLdr(hdr, ldr);

    EXPECT_EQ(ldr.getPixel(0, 0).r, 0);

    // 1.0 → ACES 0.803797 → gamma 0.905... → 8 位四舍五入 231
    const float mapped = std::pow(acesRef(1.0f), 1.0f / 2.2f);
    const int expected = int(mapped * 255.0f + 0.5f);
    EXPECT_EQ(ldr.getPixel(1, 0).r, expected);
    EXPECT_EQ(ldr.getPixel(1, 0).g, expected);
    EXPECT_EQ(ldr.getPixel(1, 0).b, expected);
    EXPECT_EQ(ldr.getPixel(1, 0).a, 255);
}

// ==================== FXAA / SSAO ====================

// 常色图必须原样输出：无边缘时不应引入任何颜色。
TEST(PostProcess, FxaaLeavesFlatImageUnchanged)
{
    const Framebuffer src(8, 8, Color(40, 80, 120));
    Framebuffer dst(8, 8, Color(0, 0, 0));
    postprocess::fxaa(src, dst);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
        {
            EXPECT_EQ(dst.getPixel(x, y).r, 40);
            EXPECT_EQ(dst.getPixel(x, y).g, 80);
            EXPECT_EQ(dst.getPixel(x, y).b, 120);
        }
}

// 黑白斜边必须产生中间色。旧实现在算完偏移后又取整，最终仍读回原像素，
// 因此输出与输入完全相同（本用例会失败）。
TEST(PostProcess, FxaaSmoothsDiagonalEdge)
{
    const int W = 16, H = 16;
    Framebuffer src(W, H, Color(0, 0, 0));
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            src.setPixel(x, y, (x + y) % 4 < 2 ? Color(255, 255, 255) : Color(0, 0, 0));

    Framebuffer dst(W, H, Color(0, 0, 0));
    postprocess::fxaa(src, dst);

    int intermediate = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            const int v = dst.getPixel(x, y).r;
            if (v > 10 && v < 245)
                ++intermediate;
        }
    EXPECT_GT(intermediate, 0) << "FXAA 未产生任何中间色，偏移被取整吃掉了";
}

// configure() 修改 noiseSize 后必须重建噪声数组，否则 compute() 会按
// 8x8 索引旧的 4x4 数组（越界读）。
TEST(PostProcess, SsaoReconfigureNoiseSizeIsSafe)
{
    postprocess::SSAO ssao;
    postprocess::SSAOConfig cfg;
    cfg.noiseSize = 8;
    ssao.configure(cfg);

    const int W = 4, H = 4;
    std::vector<Vec3f> pos(size_t(W) * H, Vec3f{0, 0, -2});
    std::vector<Vec3f> nrm(size_t(W) * H, Vec3f{0, 0, 1});
    std::vector<float> depth(size_t(W) * H, 0.5f);
    std::vector<float> ao;

    ssao.compute(pos, nrm, depth, W, H, Mat4::identity(), ao);
    ASSERT_EQ(ao.size(), size_t(W) * H);
    for (float v : ao)
    {
        EXPECT_GE(v, 0.0f);
        EXPECT_LE(v, 1.0f);
    }
}

// sampleCount=0 / noiseSize=0 / radius=0 必须被收敛到合法值，而不是除零。
TEST(PostProcess, SsaoDegenerateConfigIsSanitized)
{
    postprocess::SSAO ssao;
    postprocess::SSAOConfig cfg;
    cfg.sampleCount = 0;
    cfg.noiseSize = 0;
    cfg.radius = 0.0f;
    ssao.configure(cfg);

    const int W = 2, H = 2;
    std::vector<Vec3f> pos(size_t(W) * H, Vec3f{0, 0, -1});
    std::vector<Vec3f> nrm(size_t(W) * H, Vec3f{0, 0, 1});
    std::vector<float> depth(size_t(W) * H, 0.5f);
    std::vector<float> ao;

    ssao.compute(pos, nrm, depth, W, H, Mat4::identity(), ao);
    ASSERT_EQ(ao.size(), size_t(W) * H);
    for (float v : ao)
        EXPECT_TRUE(std::isfinite(v));
}

// 背景像素必须每帧重置为 1.0，不能残留上一帧的 AO。
TEST(PostProcess, SsaoResetsSkippedPixels)
{
    postprocess::SSAO ssao;
    const int W = 2, H = 1;
    std::vector<Vec3f> pos(2, Vec3f{0, 0, -1});
    std::vector<Vec3f> nrm(2, Vec3f{0, 0, 1});
    std::vector<float> depth{1.0f, 1.0f};  // 全背景
    std::vector<float> ao{0.0f, 0.0f};     // 上一帧残留

    ssao.compute(pos, nrm, depth, W, H, Mat4::identity(), ao);
    ASSERT_EQ(ao.size(), 2u);
    EXPECT_FLOAT_EQ(ao[0], 1.0f);
    EXPECT_FLOAT_EQ(ao[1], 1.0f);
}

// ==================== 参数与尺寸校验 ====================

// 深度数组短于 W*H 时必须直接返回，而不是按 W/H 读越界。
TEST(PostProcess, DepthOfFieldRejectsShortDepthBuffer)
{
    FloatFramebuffer color(4, 4);
    color.clear();
    std::vector<float> depth(3, 1.0f); // 需要 16 个
    FloatFramebuffer out(4, 4);
    out.clear();

    postprocess::depthOfField(color, depth, 4, 4, 1.0f, 1.0f, 2.0f, out);
    EXPECT_FLOAT_EQ(out.getPixel(3, 3).x, 0.0f); // 未处理，保持原值
}

// samples=1 时旧实现是 0/0，输出会变成 NaN。
TEST(PostProcess, RadialMotionBlurSingleSampleIsFinite)
{
    FloatFramebuffer src(4, 4);
    src.clear();
    src.setPixel(2, 2, Vec3f{1, 1, 1});
    FloatFramebuffer out(4, 4);

    postprocess::radialMotionBlur(src, 4, 4, 0.5f, 1, out);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            EXPECT_TRUE(std::isfinite(out.getPixel(x, y).x));
}

// 同源同目标时不得先 clear 再把输入抹掉。
TEST(PostProcess, GaussianBlurRejectsAliasedBuffers)
{
    FloatFramebuffer fb(4, 4);
    fb.clear();
    fb.setPixel(1, 1, Vec3f{1, 1, 1});

    postprocess::gaussianBlur(fb, fb, true, 1);
    EXPECT_FLOAT_EQ(fb.getPixel(1, 1).x, 1.0f);
}

TEST(PostProcess, BrightPassRejectsAliasedBuffers)
{
    FloatFramebuffer fb(2, 2);
    fb.clear();
    fb.setPixel(0, 0, Vec3f{2, 2, 2});

    postprocess::brightPass(fb, fb, 1.0f);
    EXPECT_FLOAT_EQ(fb.getPixel(0, 0).x, 2.0f);
}

// maxBlur 必须被夹到画面尺寸量级，否则 maxBlur=1e6 会让单帧变成 (2e6)² 次循环。
TEST(PostProcess, DepthOfFieldClampsAbsurdBlurRadius)
{
    const int W = 32, H = 32;
    FloatFramebuffer color(W, H);
    color.clear();
    std::vector<float> depth(size_t(W) * H, 0.5f); // 全部远离焦点
    FloatFramebuffer out(W, H);

    postprocess::depthOfField(color, depth, W, H, 0.0f, 1.0f, 1e6f, out);
    // 只要能返回（不挂死）且输出有限即通过
    for (int y = 0; y < H; y += 7)
        for (int x = 0; x < W; x += 7)
            EXPECT_TRUE(std::isfinite(out.getPixel(x, y).x));
}

// 均匀色图无论怎么模糊都必须等于原值：任何权重/除数错误都会在这里暴露成倍数。
TEST(PostProcess, DepthOfFieldPreservesUniformImage)
{
    const int W = 7, H = 5;
    FloatFramebuffer color(W, H);
    color.clear();
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            color.setPixel(x, y, Vec3f{0.5f, 0.25f, 0.125f});
    std::vector<float> depth(size_t(W) * H, 0.0f);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            depth[size_t(y) * W + x] = 0.1f * float((x * 3 + y) % 10);

    FloatFramebuffer out(W, H);
    postprocess::depthOfField(color, depth, W, H, 0.4f, 0.5f, 3.0f, out);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            EXPECT_NEAR(out.getPixel(x, y).x, 0.5f, 1e-4f) << x << "," << y;
            EXPECT_NEAR(out.getPixel(x, y).y, 0.25f, 1e-4f) << x << "," << y;
            EXPECT_NEAR(out.getPixel(x, y).z, 0.125f, 1e-4f) << x << "," << y;
        }
}

// DOF 的语义锁：每个像素必须等于「以它自己的 CoC 半径做的二维盒式平均」。
// 期望值在本用例内用直接的二维窗口独立算出，不复用实现。
// 这同时防止把二维窗口“优化”成可分离两趟——半径逐像素不同时两者并不等价。
TEST(PostProcess, DepthOfFieldMatchesNaiveBoxBlur)
{
    const int W = 7, H = 5;
    FloatFramebuffer color(W, H);
    color.clear();
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            color.setPixel(x, y, Vec3f{float(x) * 0.1f, float(y) * 0.2f, float(x + y) * 0.05f});

    std::vector<float> depth(size_t(W) * H, 0.0f);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            depth[size_t(y) * W + x] = 0.1f * float((x * 3 + y) % 10);

    const float focusDist = 0.4f, focusRange = 0.5f, maxBlur = 3.0f;
    FloatFramebuffer out(W, H);
    postprocess::depthOfField(color, depth, W, H, focusDist, focusRange, maxBlur, out);

    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            const float d = depth[size_t(y) * W + x];
            int radius = 0;
            if (std::isfinite(d) && d < 1e8f)
            {
                const float coc = std::min(std::abs(d - focusDist) / focusRange, 1.0f);
                radius = std::max(0, int(coc * maxBlur));
            }
            Vec3f sum{0, 0, 0};
            int count = 0;
            for (int dy = -radius; dy <= radius; ++dy)
                for (int dx = -radius; dx <= radius; ++dx)
                {
                    sum = sum + color.getPixel(std::clamp(x + dx, 0, W - 1), std::clamp(y + dy, 0, H - 1));
                    ++count;
                }
            // 显式挡住 count==0：循环至少执行一次，但 FloatFramebuffer::getPixel
            // 加了越界分支之后编译器不再能证明这一点，MSVC 会报 C4723（潜在除零），
            // 在 /WX 下直接变成编译错误。
            const float invCount = count > 0 ? 1.0f / float(count) : 0.0f;
            const Vec3f expected = sum * invCount;
            const Vec3f got = out.getPixel(x, y);
            EXPECT_NEAR(got.x, expected.x, 1e-4f) << x << "," << y;
            EXPECT_NEAR(got.y, expected.y, 1e-4f) << x << "," << y;
            EXPECT_NEAR(got.z, expected.z, 1e-4f) << x << "," << y;
        }
}

// ==================== 未集成效果的契约 ====================
// vignette 与 chromaticAberration 此前没有任何调用方（示例/测试/压测都没有），
// 属于从未被执行过的公共 API。这里固定它们的基本行为与尺寸契约。

TEST(PostProcess, VignetteDarkensEdgesAndKeepsCenter)
{
    FloatFramebuffer fb(17, 17);
    fb.clear(Vec3f{1, 1, 1});
    postprocess::vignette(fb, 0.5f);

    const Vec3f center = fb.getPixel(8, 8);
    const Vec3f corner = fb.getPixel(0, 0);
    EXPECT_GT(center.x, 0.99f);    // 中心几乎不变
    EXPECT_LT(corner.x, center.x); // 边角被压暗
    EXPECT_GE(corner.x, 0.0f);
    EXPECT_TRUE(std::isfinite(corner.x));
}

// W/H 与 src 不一致时，旧实现用调用方传的 W/H 直接索引 src.getPixel，会越界读；
// 输出也不会按输入尺寸重建，结果只有左上角一块被写。
TEST(PostProcess, ChromaticAberrationRebuildsMismatchedOutput)
{
    FloatFramebuffer src(8, 6);
    src.clear(Vec3f{0.5f, 0.5f, 0.5f});
    FloatFramebuffer out(2, 2);

    postprocess::chromaticAberration(src, 32, 24, 1.0f, out);

    EXPECT_EQ(out.width(), 8);
    EXPECT_EQ(out.height(), 6);
    const Vec3f p = out.getPixel(7, 5);
    EXPECT_TRUE(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
}

// ==================== 分条并行（*Rows）与整帧等价 ====================
//
// 分条 API 的核心契约：任意条带划分的结果必须与整帧单线程结果**逐像素一致**
// （每个算子的逐像素运算不携带跨行状态，条带划分只改变行调度不改变数值）。

namespace
{
// 非平凡测试图：线性渐变 + 高频条纹，保证每个算子都有内容可算
FloatFramebuffer makeHdrPattern(int W, int H)
{
    FloatFramebuffer fb(W, H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            const float u = float(x) / float(std::max(1, W - 1));
            const float v = float(y) / float(std::max(1, H - 1));
            fb.setPixel(x, y, {0.2f + 0.6f * u, 0.1f + 0.8f * v,
                               0.5f + 0.4f * std::sin(u * 12.0f + v * 7.0f)});
        }
    return fb;
}

Framebuffer makeLdrPattern(int W, int H)
{
    Framebuffer fb(W, H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            const float u = float(x) / float(std::max(1, W - 1));
            const float v = float(y) / float(std::max(1, H - 1));
            fb.setPixel(x, y, Color::fromFloat(u, v, 0.5f + 0.5f * std::sin(u * 9.0f + v * 5.0f)));
        }
    return fb;
}

// 用任意条带宽度逐段调用 fn(y0, y1)，模拟多线程调度下的行区间覆盖
template <class Fn>
void forEachStrip(int height, int stripHeight, Fn&& fn)
{
    for (int y = 0; y < height; y += stripHeight)
        fn(y, std::min(y + stripHeight, height));
}

void expectBuffersEqual(const FloatFramebuffer& a, const FloatFramebuffer& b)
{
    ASSERT_EQ(a.width(), b.width());
    ASSERT_EQ(a.height(), b.height());
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x)
        {
            const Vec3f pa = a.getPixel(x, y), pb = b.getPixel(x, y);
            EXPECT_FLOAT_EQ(pa.x, pb.x) << "x=" << x << " y=" << y;
            EXPECT_FLOAT_EQ(pa.y, pb.y) << "x=" << x << " y=" << y;
            EXPECT_FLOAT_EQ(pa.z, pb.z) << "x=" << x << " y=" << y;
        }
}
} // namespace

TEST(PostProcessStrips, stripRowsPartitionsExactly)
{
    for (int strips = 1; strips <= 7; ++strips)
    {
        int covered = 0;
        int prevEnd = 0;
        for (int i = 0; i < strips; ++i)
        {
            const auto [y0, y1] = postprocess::stripRows(17, i, strips);
            EXPECT_EQ(y0, prevEnd) << "条带必须首尾相接 strips=" << strips;
            EXPECT_GT(y1, y0) << "条带不能为空 strips=" << strips;
            covered += y1 - y0;
            prevEnd = y1;
        }
        EXPECT_EQ(covered, 17);
        EXPECT_EQ(prevEnd, 17);
    }
    // 非法入参返回空区间
    EXPECT_EQ(postprocess::stripRows(0, 0, 1).second, 0);
    EXPECT_EQ(postprocess::stripRows(10, 5, 3).second, 0);
    EXPECT_EQ(postprocess::stripRows(10, 0, 0).second, 0);
}

TEST(PostProcessStrips, resolveToLdrRowsMatchWholeFrame)
{
    const FloatFramebuffer src = makeHdrPattern(23, 11);
    Framebuffer whole(23, 11), split(23, 11);
    postprocess::resolveToLdr(src, whole);
    forEachStrip(11, 3, [&](int y0, int y1) { postprocess::resolveToLdrRows(src, split, y0, y1); });
    for (int y = 0; y < 11; ++y)
        for (int x = 0; x < 23; ++x)
            EXPECT_EQ(whole.getPixel(x, y), split.getPixel(x, y)) << "x=" << x << " y=" << y;
}

TEST(PostProcessStrips, vignetteRowsMatchWholeFrame)
{
    FloatFramebuffer a = makeHdrPattern(19, 13), b = makeHdrPattern(19, 13);
    postprocess::vignette(a, 0.6f);
    forEachStrip(13, 4, [&](int y0, int y1) { postprocess::vignetteRows(b, 0.6f, y0, y1); });
    expectBuffersEqual(a, b);
}

TEST(PostProcessStrips, radialMotionBlurRowsMatchWholeFrame)
{
    const FloatFramebuffer src = makeHdrPattern(20, 20);
    FloatFramebuffer whole(20, 20), split(20, 20);
    postprocess::radialMotionBlur(src, 20, 20, 0.8f, 6, whole);
    split.resize(20, 20);
    forEachStrip(20, 7, [&](int y0, int y1) {
        postprocess::radialMotionBlurRows(src, 20, 20, 0.8f, 6, split, y0, y1);
    });
    expectBuffersEqual(whole, split);
}

TEST(PostProcessStrips, bloomChainRowsMatchWholeFrame)
{
    const FloatFramebuffer src = makeHdrPattern(21, 15);
    FloatFramebuffer wholeBloom(21, 15), wholeBlur(21, 15), merged(21, 15);
    FloatFramebuffer splitBloom(21, 15), splitBlur(21, 15), splitDst(21, 15);

    // 整帧参考链：brightPass -> blur -> blur -> addBlend
    postprocess::brightPass(src, wholeBloom, 0.55f);
    postprocess::gaussianBlur(wholeBloom, wholeBlur, true, 2);
    postprocess::gaussianBlur(wholeBlur, wholeBloom, false, 2);
    merged.clear();
    postprocess::addBlend(merged, wholeBloom, 0.7f);

    // 分条等价链（dst 的 resize/clear 是全帧操作，编排侧只做一次）
    splitDst.clear();
    forEachStrip(15, 5, [&](int y0, int y1) { postprocess::brightPassRows(src, splitBloom, 0.55f, y0, y1); });
    forEachStrip(15, 5, [&](int y0, int y1) { postprocess::gaussianBlurRows(splitBloom, splitBlur, true, 2, y0, y1); });
    splitBloom.clear();
    forEachStrip(15, 5, [&](int y0, int y1) { postprocess::gaussianBlurRows(splitBlur, splitBloom, false, 2, y0, y1); });
    forEachStrip(15, 5, [&](int y0, int y1) { postprocess::addBlendRows(splitDst, splitBloom, 0.7f, y0, y1); });

    expectBuffersEqual(merged, splitDst);
}

TEST(PostProcessStrips, fxaaRowsMatchWholeFrame)
{
    const Framebuffer src = makeLdrPattern(24, 16);
    Framebuffer whole(24, 16), split(24, 16);
    postprocess::fxaa(src, whole);
    std::vector<float> lumMap;
    postprocess::buildLuminanceMap(src, lumMap);
    forEachStrip(16, 5, [&](int y0, int y1) { postprocess::fxaaRows(src, split, lumMap, y0, y1); });
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 24; ++x)
            EXPECT_EQ(whole.getPixel(x, y), split.getPixel(x, y)) << "x=" << x << " y=" << y;
}

TEST(PostProcessStrips, ssaoComputeRowsMatchWholeFrame)
{
    const int W = 12, H = 9;
    std::vector<Vec3f> viewPos(size_t(W) * H), viewNormal(size_t(W) * H);
    std::vector<float> depth(size_t(W) * H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            const size_t idx = size_t(y) * W + x;
            viewPos[idx] = {float(x) * 0.1f - 0.6f, float(y) * 0.1f - 0.4f, -2.0f};
            viewNormal[idx] = {0.0f, 0.0f, 1.0f};
            // 一块"前景矮墙"制造遮挡关系，背景深度置 1（会被跳过）
            depth[idx] = (x > 2 && x < 9 && y > 2) ? 0.4f + 0.01f * float(x) : 0.999f;
        }
    const Mat4 viewProj = sr::transform::perspective(1.2f, float(W) / float(H), 0.1f, 10.0f);

    postprocess::SSAO ssao;
    std::vector<float> whole, split(size_t(W) * H, 1.0f);
    ssao.compute(viewPos, viewNormal, depth, W, H, viewProj, whole);
    forEachStrip(H, 4, [&](int y0, int y1) {
        ssao.computeRows(viewPos, viewNormal, depth, W, H, viewProj, split, y0, y1);
    });
    ASSERT_EQ(whole.size(), split.size());
    for (size_t i = 0; i < whole.size(); ++i)
        EXPECT_FLOAT_EQ(whole[i], split[i]) << "idx=" << i;
}

// ==================== 半分辨率 Bloom 整链 ====================

TEST(PostProcessBloom, darkImageUnchangedBelowThreshold)
{
    FloatFramebuffer src(32, 32), dst(32, 32);
    src.clear(Vec3f{0.2f, 0.2f, 0.2f}); // 亮度 0.2 < 阈值 1.0
    postprocess::bloom(src, dst);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
        {
            const Vec3f p = dst.getPixel(x, y);
            EXPECT_NEAR(p.x, 0.2f, 1e-5f);
            EXPECT_NEAR(p.y, 0.2f, 1e-5f);
            EXPECT_NEAR(p.z, 0.2f, 1e-5f);
        }
}

TEST(PostProcessBloom, brightImpulseSpreadsAndDecays)
{
    FloatFramebuffer src(64, 48), dst(64, 48);
    src.clear(Vec3f{0, 0, 0});
    src.setPixel(32, 24, Vec3f{6, 6, 6}); // 单个超阈值亮像素

    postprocess::bloom(src, dst, {1.0f, 1.0f, 10});

    // 亮核处应有 bloom 增益，随距离衰减；全图有限
    const Vec3f center = dst.getPixel(32, 24);
    const Vec3f near = dst.getPixel(36, 24);
    const Vec3f far = dst.getPixel(52, 24);
    EXPECT_GT(center.x, 0.0f);
    EXPECT_GT(near.x, 0.0f);
    EXPECT_NEAR(far.x, 0.0f, 1e-4f);
    EXPECT_GT(near.x, far.x);
    for (int y = 0; y < 48; y += 7)
        for (int x = 0; x < 64; x += 5)
        {
            const Vec3f p = dst.getPixel(x, y);
            EXPECT_TRUE(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z));
        }
}

TEST(PostProcessBloom, rejectsAliasedBuffersAndKeepsSrc)
{
    FloatFramebuffer src(16, 16), dst(16, 16);
    src.setPixel(8, 8, Vec3f{4, 4, 4});
    postprocess::bloom(src, src); // 同缓冲：空操作
    EXPECT_FLOAT_EQ(src.getPixel(8, 8).x, 4.0f) << "同缓冲不得清掉输入";

    postprocess::bloom(src, dst);
    EXPECT_FLOAT_EQ(src.getPixel(8, 8).x, 4.0f) << "src 保持不变";
    EXPECT_GT(dst.getPixel(8, 8).x, 3.9f); // 亮部 + bloom 增益
}

// ==================== bloom 分条等价 ====================

TEST(PostProcessBloom, stripedOrchestrationMatchesWholeChain)
{
    const FloatFramebuffer src = makeHdrPattern(40, 30);
    FloatFramebuffer whole(40, 30), split(40, 30);

    postprocess::bloom(src, whole, {0.5f, 0.9f, 10});

    // 手工分条编排：与整链同一段数，条带边界任意划分
    postprocess::BloomScratch scratch;
    scratch.resizeFor(40, 30);
    scratch.half.clear();
    forEachStrip(15, 4, [&](int y0, int y1) { postprocess::bloomPrepareRows(src, scratch, {0.5f, 0.9f, 10}, y0, y1); });

    const int radius = postprocess::bloomHalfRadius({0.5f, 0.9f, 10}, scratch.half.width(), scratch.half.height());
    scratch.blurH.clear();
    forEachStrip(scratch.half.height(), 3,
                 [&](int y0, int y1) { postprocess::gaussianBlurRows(scratch.half, scratch.blurH, true, radius, y0, y1); });
    scratch.half.clear();
    forEachStrip(scratch.half.height(), 3,
                 [&](int y0, int y1) { postprocess::gaussianBlurRows(scratch.blurH, scratch.half, false, radius, y0, y1); });
    forEachStrip(30, 7, [&](int y0, int y1) { postprocess::bloomApplyRows(src, scratch.half, split, {0.5f, 0.9f, 10}, y0, y1); });

    expectBuffersEqual(whole, split);
}
