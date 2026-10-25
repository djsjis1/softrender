#pragma once
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "softrenderer/core/framebuffer.h"
#include "softrenderer/io/image_loader.h"
#include "softrenderer/io/image_write.h"

// ============================================================
// 像素级回归测试工具
//
// 企业级渲染器的质量保障：固定场景渲染后，对比输出像素与
// 基准图（golden image）的差异。允许少量像素偏差（抗锯齿/
// 浮点精度导致的 1-2 灰度差异），但大面积偏差 = 回归 bug。
//
// 【为何需要它】单元测试只能断言局部性质（某个像素是否被涂色、
// 某个点积是否为零），**无法锁定完整渲染结果的视觉正确性**。
// rasterizer_fast 的深度插值权重错位能长期潜伏，正是因为缺这张网。
//
// 用法：
//   1. 首次建立基准：配置时加 -DSR_UPDATE_GOLDEN=ON，运行测试即生成基准图
//   2. 后续回归：compareWithGolden("test_scene", fb, threshold)
//      返回 passed=true 即通过
//
// 基准图存放：tests/fixtures/golden/<name>.png（路径由编译期 SR_GOLDEN_DIR 注入）
// 比较包含 alpha：2D 混合场景的颜色可能相同而合成透明度已错。
// ============================================================
namespace regression {
using sr::Color;
using sr::Framebuffer;

// 基准图目录。
//
// 【为何不能硬编码相对路径】旧实现写的是 "tests/fixtures/golden/" + name + ".tga"，
// 只有从**仓库根目录**运行才正确。而 CI 用 ctest --test-dir build，
// 工作目录是 build/ → 解析成 build/tests/fixtures/golden/ → 找不到基准图 →
// 旧逻辑会走「不存在就自动创建并返回 passed」分支 → **测试永远假通过**。
// 现在由 CMake 通过 SR_GOLDEN_DIR 注入绝对路径，与工作目录无关。
inline std::string goldenDir()
{
#ifdef SR_GOLDEN_DIR
    return SR_GOLDEN_DIR;
#else
    // 回退：未经 CMake 配置、直接从仓库根目录运行测试二进制时可用
    return "tests/fixtures/golden";
#endif
}

// 基准图完整路径。
// 用 PNG 而不是 TGA：TGA 写出是 24 位，会丢掉 alpha 通道，
// 而 2D 混合场景的回归必须能比 alpha。
inline std::string goldenPath(const std::string& name)
{
    return goldenDir() + "/" + name + ".png";
}

struct CompareResult
{
    bool passed;
    int totalPixels;
    int mismatchedPixels;
    int mismatchedAlpha;  // alpha 不符的像素数（RGB 可能全对而合成透明度已错）
    float maxDelta;       // 单通道最大差值 [0,255]
    float avgDelta;       // 平均差值
    float mismatchRatio;  // 不匹配像素占比
};

// 比较两帧缓冲，返回统计结果
inline CompareResult compare(const Framebuffer& a, const Framebuffer& b, float pixelThreshold = 2.0f)
{
    CompareResult r{};
    if (a.width() != b.width() || a.height() != b.height())
    {
        r.passed = false;
        r.totalPixels = 0;
        return r;
    }
    r.totalPixels = a.width() * a.height();
    float sumDelta = 0;
    r.maxDelta = 0;
    r.mismatchedPixels = 0;
    r.mismatchedAlpha = 0;

    for (int y = 0; y < a.height(); ++y)
    {
        for (int x = 0; x < a.width(); ++x)
        {
            Color ca = a.getPixel(x, y);
            Color cb = b.getPixel(x, y);
            float dr = std::abs(float(ca.r) - float(cb.r));
            float dg = std::abs(float(ca.g) - float(cb.g));
            float db = std::abs(float(ca.b) - float(cb.b));
            float da = std::abs(float(ca.a) - float(cb.a));
            float d = std::max({dr, dg, db, da});
            r.maxDelta = std::max(r.maxDelta, d);
            sumDelta += d;
            if (d > pixelThreshold)
                ++r.mismatchedPixels;
            if (da > 0.5f)
                ++r.mismatchedAlpha;
        }
    }
    r.avgDelta = sumDelta / float(r.totalPixels);
    r.mismatchRatio = float(r.mismatchedPixels) / float(r.totalPixels);
    // 通过条件：不匹配像素 < 0.1%、平均差 < 0.5，且 alpha 完全一致。
    r.passed = (r.mismatchRatio < 0.001f && r.avgDelta < 0.5f && r.mismatchedAlpha == 0);
    return r;
}

// 保存基准图
inline bool saveGolden(const std::string& name, const Framebuffer& fb)
{
    return sr::io::writeImage(goldenPath(name), fb);
}

// 加载基准图并比较
inline CompareResult compareWithGolden(const std::string& name, const Framebuffer& fb, float pixelThreshold = 2.0f)
{
    CompareResult r{};
    const std::string path = goldenPath(name);

#ifdef SR_UPDATE_GOLDEN
    // 维护模式：无论基准图是否存在都用当前结果覆写。
    //
    // 旧实现只在 readTga 失败时写入，已存在但内容不符的基准图永远无法更新；
    // 且不检查 saveGolden 的返回值，写失败也报通过。
    const bool saved = saveGolden(name, fb);
    (void)pixelThreshold; // 覆写模式不做比较
    r.totalPixels = fb.width() * fb.height();
    r.mismatchedPixels = 0;
    r.mismatchedAlpha = 0;
    r.maxDelta = 0;
    r.avgDelta = 0;
    r.mismatchRatio = 0;
    r.passed = saved;
    std::printf("[REGRESSION] Golden %s: %s\n", saved ? "written" : "WRITE FAILED", path.c_str());
    return r;
#else
    sr::Image img;
    if (!sr::io::loadImage(path, img))
    {
        // CI / 默认：基准图缺失必须**直接失败**。
        //
        // 旧实现在这里自动创建并返回 passed，是典型的静默失败：
        // 一旦路径算错（见 goldenDir 注释）或基准图忘了提交，
        // 测试会永远绿灯 —— 比没有测试更危险。
        r.passed = false;
        r.totalPixels = fb.width() * fb.height();
        r.mismatchedPixels = r.totalPixels;
        r.mismatchedAlpha = r.totalPixels;
        r.maxDelta = 255.0f;
        r.avgDelta = 255.0f;
        r.mismatchRatio = 1.0f;
        std::printf("[REGRESSION] MISSING golden image: %s\n", path.c_str());
        std::printf("[REGRESSION] 若为首次建立基准，请用 -DSR_UPDATE_GOLDEN=ON 重新配置；\n"
                    "[REGRESSION] 并确认 goldenDir() 解析出的路径与实际仓库位置一致。\n");
        return r;
    }
    // 尺寸不一致时 compare() 会直接判失败（totalPixels=0），这里不重复检查
    Framebuffer golden(img.width, img.height);
    for (int y = 0; y < img.height; ++y)
        for (int x = 0; x < img.width; ++x)
            golden.setPixel(x, y, img.at(x, y));
    return compare(golden, fb, pixelThreshold);
#endif
}

}  // namespace regression
