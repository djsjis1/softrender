#pragma once
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "core/framebuffer.h"
#include "core/tga.h"

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
// 基准图存放：tests/golden/<name>.tga（路径由编译期 SR_GOLDEN_DIR 注入）
// ============================================================
namespace regression
{

    // 基准图目录。
    //
    // 【为何不能硬编码相对路径】旧实现写的是 "tests/golden/" + name + ".tga"，
    // 只有从**仓库根目录**运行才正确。而 CI 用 ctest --test-dir build，
    // 工作目录是 build/ → 解析成 build/tests/golden/ → 找不到基准图 →
    // 旧逻辑会走「不存在就自动创建并返回 passed」分支 → **测试永远假通过**。
    // 现在由 CMake 通过 SR_GOLDEN_DIR 注入绝对路径，与工作目录无关。
    inline std::string goldenDir()
    {
#ifdef SR_GOLDEN_DIR
        return SR_GOLDEN_DIR;
#else
        // 回退：未经 CMake 配置、直接从仓库根目录运行测试二进制时可用
        return "tests/golden";
#endif
    }

    // 基准图完整路径
    inline std::string goldenPath(const std::string &name)
    {
        return goldenDir() + "/" + name + ".tga";
    }

    struct CompareResult
    {
        bool passed;
        int totalPixels;
        int mismatchedPixels;
        float maxDelta;      // 单通道最大差值 [0,255]
        float avgDelta;      // 平均差值
        float mismatchRatio; // 不匹配像素占比
    };

    // 比较两帧缓冲，返回统计结果
    inline CompareResult compare(const Framebuffer &a, const Framebuffer &b,
                                 float pixelThreshold = 2.0f)
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

        for (int y = 0; y < a.height(); ++y)
        {
            for (int x = 0; x < a.width(); ++x)
            {
                Color ca = a.getPixel(x, y);
                Color cb = b.getPixel(x, y);
                float dr = std::abs(float(ca.r) - float(cb.r));
                float dg = std::abs(float(ca.g) - float(cb.g));
                float db = std::abs(float(ca.b) - float(cb.b));
                float d = std::max({dr, dg, db});
                r.maxDelta = std::max(r.maxDelta, d);
                sumDelta += d;
                if (d > pixelThreshold)
                    ++r.mismatchedPixels;
            }
        }
        r.avgDelta = sumDelta / float(r.totalPixels);
        r.mismatchRatio = float(r.mismatchedPixels) / float(r.totalPixels);
        // 通过条件：不匹配像素 < 0.1% 且平均差 < 0.5
        r.passed = (r.mismatchRatio < 0.001f && r.avgDelta < 0.5f);
        return r;
    }

    // 保存基准图
    inline bool saveGolden(const std::string &name, const Framebuffer &fb)
    {
        return tga::write(goldenPath(name), fb);
    }

    // 加载基准图并比较
    inline CompareResult compareWithGolden(const std::string &name, const Framebuffer &fb,
                                           float pixelThreshold = 2.0f)
    {
        CompareResult r{};
        const std::string path = goldenPath(name);
        tga::Image img;
        if (!tga::read(path, img))
        {
#ifdef SR_UPDATE_GOLDEN
            // 本地开发：只有显式要求更新基准时才自动创建。
            // 配置时加 -DSR_UPDATE_GOLDEN=ON，提交前记得关掉。
            saveGolden(name, fb);
            r.passed = true;
            r.totalPixels = fb.width() * fb.height();
            r.mismatchedPixels = 0;
            r.maxDelta = 0;
            r.avgDelta = 0;
            r.mismatchRatio = 0;
            std::printf("[REGRESSION] Golden created: %s\n", path.c_str());
            return r;
#else
            // CI / 默认：基准图缺失必须**直接失败**。
            //
            // 旧实现在这里自动创建并返回 passed，是典型的静默失败：
            // 一旦路径算错（见 goldenDir 注释）或基准图忘了提交，
            // 测试会永远绿灯 —— 比没有测试更危险。
            r.passed = false;
            r.totalPixels = fb.width() * fb.height();
            r.mismatchedPixels = r.totalPixels;
            r.maxDelta = 255.0f;
            r.avgDelta = 255.0f;
            r.mismatchRatio = 1.0f;
            std::printf("[REGRESSION] MISSING golden image: %s\n", path.c_str());
            std::printf("[REGRESSION] 若为首次建立基准，请用 -DSR_UPDATE_GOLDEN=ON 重新配置；\n"
                        "[REGRESSION] 并确认 goldenDir() 解析出的路径与实际仓库位置一致。\n");
            return r;
#endif
        }
        // 尺寸不一致时 compare() 会直接判失败（totalPixels=0），这里不重复检查
        Framebuffer golden(img.width, img.height);
        for (int y = 0; y < img.height; ++y)
            for (int x = 0; x < img.width; ++x)
                golden.setPixel(x, y, img.at(x, y));
        return compare(golden, fb, pixelThreshold);
    }

} // namespace regression
