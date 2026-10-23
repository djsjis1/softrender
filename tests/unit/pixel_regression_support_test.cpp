// ============================================================
// 测试支持代码单元测试：像素回归比对器
//
// 覆盖 tests/support/pixel_regression.h，不参与库的公共安装。
// ============================================================
#include <gtest/gtest.h>

#include <string>

#include "softrenderer/core/color.h"
#include "softrenderer/core/framebuffer.h"
#include "support/pixel_regression.h"

using namespace sr;

TEST(PixelRegression, compare_identicalFramebuffers_passes)
{
    Framebuffer a(64, 64, Color(10, 20, 30));
    Framebuffer b(64, 64, Color(10, 20, 30));
    const regression::CompareResult r = regression::compare(a, b);
    EXPECT_TRUE(r.passed);
    EXPECT_EQ(r.totalPixels, 64 * 64);
    EXPECT_EQ(r.mismatchedPixels, 0);
    EXPECT_FLOAT_EQ(r.maxDelta, 0.0f);
    EXPECT_FLOAT_EQ(r.avgDelta, 0.0f);
}

// 单像素差异小于 pixelThreshold（2.0）：不计入不匹配，也不得拉高平均差到阈值上

TEST(PixelRegression, compare_singlePixelWithinThreshold_passes)
{
    Framebuffer a(64, 64, Color(10, 20, 30));
    Framebuffer b(64, 64, Color(10, 20, 30));
    b.setPixel(32, 32, Color(11, 20, 30));  // 差 1，小于阈值 2.0
    const regression::CompareResult r = regression::compare(a, b);
    EXPECT_TRUE(r.passed);
    EXPECT_EQ(r.mismatchedPixels, 0);
    EXPECT_FLOAT_EQ(r.maxDelta, 1.0f);
}

// 单个「热像素」（差异远超阈值）仍应通过：
// 占比 1/4096 = 0.000244 < 0.001，平均差 200/4096 = 0.049 < 0.5。
// 这是容差设计的核心：抗锯齿/浮点微差不应误报回归。

TEST(PixelRegression, compare_singleHotPixel_passesWithinTolerance)
{
    Framebuffer a(64, 64, Color(10, 20, 30));
    Framebuffer b(64, 64, Color(10, 20, 30));
    b.setPixel(32, 32, Color(210, 20, 30));  // 差 200，远超阈值
    const regression::CompareResult r = regression::compare(a, b);
    EXPECT_TRUE(r.passed);
    EXPECT_EQ(r.mismatchedPixels, 1);
    EXPECT_FLOAT_EQ(r.maxDelta, 200.0f);
    EXPECT_LT(r.mismatchRatio, 0.001f);
}

// 大面积差异必须失败：占比条件触发

TEST(PixelRegression, compare_largeAreaDiff_fails)
{
    Framebuffer a(64, 64, Color(10, 20, 30));
    Framebuffer b(64, 64, Color(10, 20, 30));
    for (int y = 0; y < 32; ++y)  // 上半屏全改
        for (int x = 0; x < 64; ++x)
            b.setPixel(x, y, Color(200, 200, 200));
    const regression::CompareResult r = regression::compare(a, b);
    EXPECT_FALSE(r.passed);
    EXPECT_EQ(r.mismatchedPixels, 32 * 64);
    EXPECT_GT(r.mismatchRatio, 0.4f);
}

// 平均差条件必须能**单独**触发失败：
// 每个像素只差 1（均低于 pixelThreshold 2.0，所以 mismatchedPixels = 0），
// 但 avgDelta = 1.0 > 0.5。若只有占比条件，这种「全图系统性偏移」会被漏掉。

TEST(PixelRegression, compare_uniformSmallDrift_failsOnAvgDelta)
{
    Framebuffer a(64, 64, Color(10, 20, 30));
    Framebuffer b(64, 64, Color(11, 21, 31));  // 每通道均差 1
    const regression::CompareResult r = regression::compare(a, b);
    EXPECT_EQ(r.mismatchedPixels, 0);  // 没有像素超过单像素阈值
    EXPECT_FLOAT_EQ(r.avgDelta, 1.0f); // 但平均差超了 0.5
    EXPECT_FALSE(r.passed);
}

// 尺寸不一致时必须失败，且不得越界读取（旧实现无此保护会直接读越界）

TEST(PixelRegression, compare_sizeMismatch_failsSafely)
{
    Framebuffer a(64, 64, Color(10, 20, 30));
    Framebuffer b(32, 32, Color(10, 20, 30));
    const regression::CompareResult r = regression::compare(a, b);
    EXPECT_FALSE(r.passed);
    EXPECT_EQ(r.totalPixels, 0);
}

// goldenPath 必须用编译期注入的绝对路径，不能依赖工作目录。
// ctest 的工作目录是 build/，若用相对路径会解析成 build/tests/golden/ →
// 找不到基准图 → 旧逻辑自动创建并返回 passed → 测试永远假通过。

TEST(PixelRegression, goldenPath_isAbsoluteAndEndsWithSceneName)
{
    const std::string dir = regression::goldenDir();
    ASSERT_FALSE(dir.empty());
    const std::string path = regression::goldenPath("scene_x");
    EXPECT_EQ(path, dir + "/scene_x.tga");
#ifdef SR_GOLDEN_DIR
    // Windows 盘符形式（C:/...）或 POSIX 根形式（/...）都算绝对路径
    const bool isAbsolute = (path.size() > 1 && path[1] == ':') || path[0] == '/';
    EXPECT_TRUE(isAbsolute) << "SR_GOLDEN_DIR 已定义但路径不是绝对的：" << path;
#else
    // 未注入 SR_GOLDEN_DIR 时这个用例没有价值：它会静默退化成只验证字符串拼接。
    // 用 FAIL 而非静默通过，防止 CMake 侧遗漏定义时无人察觉。
    FAIL() << "unit_tests 未定义 SR_GOLDEN_DIR，无法验证绝对路径。"
           << "请检查 CMakeLists.txt 是否给本目标注入了该定义。";
#endif
}

// ==================== 快速光栅化器 ====================

