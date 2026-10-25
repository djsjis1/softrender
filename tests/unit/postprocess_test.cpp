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
            const Vec3f expected = sum * (1.0f / float(count));
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
