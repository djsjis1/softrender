// ============================================================
// core 模块单元测试：颜色混合、深度/颜色缓冲、G-Buffer 与内存分配器
//
// core 不含文件、日志、线程与第三方依赖，故本文件不引入 io/runtime。
// ============================================================
#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <vector>

#include "softrenderer/core/arena.h"
#include "softrenderer/core/blend.h"
#include "softrenderer/core/color.h"
#include "softrenderer/core/depthbuffer.h"
#include "softrenderer/core/float_framebuffer.h"
#include "softrenderer/core/framebuffer.h"
#include "softrenderer/core/gbuffer.h"
#include "softrenderer/render/shader/deferred_lighting.h"

using namespace sr;

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
#else
    DepthBuffer db(4, 4);
    // 四个方向都要测：只测一侧会漏掉「x 检查误用了 height_」这类笔误
    EXPECT_FLOAT_EQ(db.get(-1, 0), DepthBuffer::kFar);
    EXPECT_FLOAT_EQ(db.get(4, 0), DepthBuffer::kFar);
    EXPECT_FLOAT_EQ(db.get(0, -1), DepthBuffer::kFar);
    EXPECT_FLOAT_EQ(db.get(0, 4), DepthBuffer::kFar);
    // 合法位置仍返回真实值：确认兜底没有把正常路径也短路掉
    db.testAndSet(1, 1, 0.25f);
    EXPECT_FLOAT_EQ(db.get(1, 1), 0.25f);
#endif
}

// ==================== 帧缓冲边界 ====================

// setPixel 早就有运行时边界检查（越界静默忽略），而 getPixel 只有 assert。
// Release 下 assert 被编译掉，越界读就是 UB —— 这个不对称是本用例要锁住的契约。

TEST(Framebuffer, framebuffer_getPixel_outOfBounds_returnsSentinel)
{
#ifndef NDEBUG
    GTEST_SKIP() << "越界哨兵仅在 Release（NDEBUG）下可测：Debug 下 assert 会先中止进程";
#else
    // 用一个非黑的填充色，这样「读到 sr::colors::black 哨兵」与「读到真实像素」可区分
    Framebuffer fb(4, 4, Color(10, 20, 30, 255));

    EXPECT_EQ(fb.getPixel(-1, 0).r, 0);
    EXPECT_EQ(fb.getPixel(4, 0).r, 0);
    EXPECT_EQ(fb.getPixel(0, -1).r, 0);
    EXPECT_EQ(fb.getPixel(0, 4).r, 0);
    // 哨兵是 sr::colors::black，其 a 通道为默认值 255（而非 0）
    EXPECT_EQ(fb.getPixel(-1, -1).a, 255);

    // 合法位置不受影响
    EXPECT_EQ(fb.getPixel(0, 0).r, 10);
    EXPECT_EQ(fb.getPixel(3, 3).b, 30);

    // setPixel 越界仍是静默忽略，不得抛异常也不得写内存
    fb.setPixel(-1, -1, Color(255, 255, 255));
    fb.setPixel(4, 4, Color(255, 255, 255));
    EXPECT_EQ(fb.getPixel(0, 0).r, 10);
#endif
}

// FloatFramebuffer 的 getPixel 曾完全裸下标访问（既无 assert 也无运行时检查），
// 与同类的 setPixel、BasicFramebuffer::getPixel 口径不一致；这里锁定修好后的契约。
TEST(FloatFramebuffer, getPixel_outOfBounds_returnsBlack)
{
#ifndef NDEBUG
    GTEST_SKIP() << "越界哨兵仅在 Release（NDEBUG）下可测：Debug 下 assert 会先中止进程";
#else
    sr::FloatFramebuffer fb(4, 4);
    fb.setPixel(1, 2, sr::Vec3f{0.25f, 0.5f, 0.75f});

    // 四个方向都必须返回黑色哨兵，而不是读到别的像素或越界内存
    for (int i = 0; i < 4; ++i)
    {
        const sr::Vec3f c = (i == 0) ? fb.getPixel(-1, 2)
                            : (i == 1) ? fb.getPixel(4, 2)
                            : (i == 2) ? fb.getPixel(1, -1)
                                       : fb.getPixel(1, 4);
        EXPECT_EQ(c.x, 0.0f) << "direction " << i;
        EXPECT_EQ(c.y, 0.0f) << "direction " << i;
        EXPECT_EQ(c.z, 0.0f) << "direction " << i;
    }

    // 合法位置不受影响
    EXPECT_EQ(fb.getPixel(1, 2).x, 0.25f);
    EXPECT_EQ(fb.getPixel(1, 2).z, 0.75f);
#endif
}

// ==================== 纹理 ====================

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

// clampU8 必须正确钳制到 [0,255]

TEST(Blend, clampU8_clampsCorrectly)
{
    EXPECT_EQ(clampU8(0), 0);
    EXPECT_EQ(clampU8(128), 128);
    EXPECT_EQ(clampU8(255), 255);
    EXPECT_EQ(clampU8(-1), 0);
    EXPECT_EQ(clampU8(-100), 0);
    EXPECT_EQ(clampU8(256), 255);
    EXPECT_EQ(clampU8(510), 255);
    EXPECT_EQ(clampU8(10000), 255);
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

TEST(GBuffer, WriteAndRead)
{
    sr::GBuffer gbuf;
    gbuf.resize(4, 4);
    EXPECT_TRUE(gbuf.write(1, 1, 0.5f, Vec3f{1, 2, 3}, Vec3f{0, 1, 0}, 0.3f, Vec3f{0.8f, 0.2f, 0.1f}, 0.0f));
    EXPECT_FALSE(gbuf.write(1, 1, 0.8f, Vec3f{0, 0, 0}, Vec3f{0, 1, 0}, 0.5f, Vec3f{1, 1, 1}, 1.0f));  // farther
    EXPECT_FLOAT_EQ(gbuf.depth[1 * 4 + 1], 0.5f);
    EXPECT_FLOAT_EQ(gbuf.worldPos[1 * 4 + 1].x, 1.0f);
}

// NaN 深度必须被拒绝：NaN 与任何值比较都为假，一旦写进去深度测试会整体失效。
TEST(GBuffer, RejectsNonFiniteDepth)
{
    sr::GBuffer g;
    g.resize(2, 1);
    g.clear();
    const Vec3f pos{0, 0, 0};
    const Vec3f nrm{0, 0, 1};
    const Vec3f alb{1, 1, 1};

    EXPECT_FALSE(g.write(0, 0, std::numeric_limits<float>::quiet_NaN(), pos, nrm, 0.5f, alb, 0.0f));
    EXPECT_FALSE(g.write(0, 0, std::numeric_limits<float>::infinity(), pos, nrm, 0.5f, alb, 0.0f));
    EXPECT_TRUE(g.write(0, 0, 0.9f, pos, nrm, 0.5f, alb, 0.0f));
    EXPECT_FALSE(g.write(0, 0, 0.95f, pos, nrm, 0.5f, alb, 0.0f)); // 更远，必须被拒
    EXPECT_TRUE(g.write(0, 0, 0.5f, pos, nrm, 0.5f, alb, 0.0f));  // 更近，应接受
}

// G-Buffer 四路 RT 长度不一致时，光照 Pass 必须直接返回而不是越界读。
TEST(GBuffer, LightingPassRejectsInconsistentBuffers)
{
    sr::GBuffer g;
    g.resize(4, 4);
    g.clear();
    g.depth.resize(3); // 人为破坏一致性

    std::vector<Vec3f> out(1, Vec3f{9, 9, 9});
    sr::lightingPass(g, Vec3f{0, 1, 0}, Vec3f{1, 1, 1}, Vec3f{0, 0, 1}, Vec3f{0, 0, 0}, out);
    EXPECT_TRUE(out.empty());
}

// ==================== 颜色转换的最后一道闸门 ====================

// 着色器输出 NaN/Inf 时，fromFloat 必须把它们夹到 0/255，
// 而不是把 NaN 交给 uint8_t（那是未定义行为）。
TEST(Color, FromFloatClampsNonFinite)
{
    const Color c = Color::fromFloat(std::numeric_limits<float>::quiet_NaN(),
                                     std::numeric_limits<float>::infinity(),
                                     -std::numeric_limits<float>::infinity());
    EXPECT_EQ(c.r, 0);
    EXPECT_EQ(c.g, 255);
    EXPECT_EQ(c.b, 0);
    EXPECT_EQ(c.a, 255);
}

// t 为 NaN 时 lerp 夹到 0（返回 c0），不得产生未定义行为。
TEST(Color, LerpClampsNonFiniteT)
{
    const Color c = Color::lerp(Color(10, 20, 30), Color(200, 210, 220),
                                std::numeric_limits<float>::quiet_NaN());
    EXPECT_EQ(c.r, 10);
    EXPECT_EQ(c.g, 20);
    EXPECT_EQ(c.b, 30);
}

// ==================== 混合的 alpha 合成与整数舍入 ====================

// div255 必须是四舍五入：旧移位实现在 33023 上返回 129（应为 130）。
TEST(Blend, div255RoundsToNearest)
{
    EXPECT_EQ(div255(0), 0);
    EXPECT_EQ(div255(127), 0);
    EXPECT_EQ(div255(128), 1);
    EXPECT_EQ(div255(255), 1);
    EXPECT_EQ(div255(33023), 130);   // 旧公式 (v + (v>>8) + 128) >> 8 = 129
    EXPECT_EQ(div255(65025), 255);
    EXPECT_EQ(div255(130050), 510);  // src*α + dst*(255−α) 的上界
}

// 半透明源盖在不透明目标上：结果必须仍不透明。
// 旧实现把 alpha 当 RGB 再乘一次源 alpha，得到 191。
TEST(Blend, AlphaBlendOverOpaqueKeepsFullAlpha)
{
    const Color src(200, 100, 50, 128);
    const Color dst(10, 20, 30, 255);
    const Color r = blendPixels(src, dst, BlendMode::AlphaBlend);
    EXPECT_EQ(r.a, 255);
}

// 透明目标上的 source-over：颜色应等于源色本身，alpha 等于源 alpha。
TEST(Blend, AlphaBlendOverTransparentKeepsSourceColor)
{
    const Color src(200, 100, 50, 128);
    const Color dst(0, 0, 0, 0);
    const Color r = blendPixels(src, dst, BlendMode::AlphaBlend);
    EXPECT_EQ(r.a, 128);
    EXPECT_EQ(r.r, 200);
    EXPECT_EQ(r.g, 100);
    EXPECT_EQ(r.b, 50);
}

// 连续两层半透明：alpha 按 source-over 递增，而不是 As×As。
TEST(Blend, AlphaBlendStacksAlphaCorrectly)
{
    const Color base(0, 0, 0, 0);
    const Color layer(255, 255, 255, 128);
    const Color once = blendPixels(layer, base, BlendMode::AlphaBlend);
    const Color twice = blendPixels(layer, once, BlendMode::AlphaBlend);
    EXPECT_EQ(once.a, 128);
    // 128 + round(128*127/255) = 128 + 64
    EXPECT_EQ(twice.a, 192);
}

// Premultiplied 也必须保留合成后的 alpha（旧实现无条件写 255）。
TEST(Blend, PremultipliedKeepsCompositeAlpha)
{
    const Color src(64, 32, 16, 128);  // 已预乘：各通道 ≤ alpha
    const Color dst(0, 0, 0, 0);
    const Color r = blendPixels(src, dst, BlendMode::Premultiplied);
    EXPECT_EQ(r.a, 128);
    EXPECT_EQ(r.r, 64);
}

// ==================== Cubemap ====================

