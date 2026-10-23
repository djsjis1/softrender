// ============================================================
// 后处理单元测试：色调映射、gamma、亮部提取、高斯模糊与加法合成
//
// 这些效果在 2.0.0 从 `FloatFramebuffer` 的成员方法迁出为
// `sr::postprocess` 的自由函数。本文件用**独立算出的期望值**锁定数值公式，
// 而不是拿两个实现互相验证。
// ============================================================
#include <gtest/gtest.h>

#include <cmath>

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
