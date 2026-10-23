// ============================================================
// core 模块单元测试：颜色混合、深度/颜色缓冲、G-Buffer 与内存分配器
//
// core 不含文件、日志、线程与第三方依赖，故本文件不引入 io/runtime。
// ============================================================
#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "softrenderer/core/arena.h"
#include "softrenderer/core/blend.h"
#include "softrenderer/core/color.h"
#include "softrenderer/core/depthbuffer.h"
#include "softrenderer/core/framebuffer.h"
#include "softrenderer/core/gbuffer.h"

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

// ==================== Cubemap ====================

