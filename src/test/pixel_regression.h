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
// 用法：
//   1. 首次运行：生成基准图 saveGolden("test_scene", fb);
//   2. 后续运行：compareWithGolden("test_scene", fb, threshold);
//      返回 true = 通过，false = 回归
//
// 基准图存放：tests/golden/<name>.tga
// ============================================================
namespace regression
{

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
        std::string path = "tests/golden/" + name + ".tga";
        return tga::write(path, fb);
    }

    // 加载基准图并比较
    inline CompareResult compareWithGolden(const std::string &name, const Framebuffer &fb,
                                           float pixelThreshold = 2.0f)
    {
        CompareResult r{};
        std::string path = "tests/golden/" + name + ".tga";
        tga::Image img;
        if (!tga::read(path, img))
        {
            // 基准图不存在，自动保存为基准（首次运行）
            saveGolden(name, fb);
            r.passed = true;
            r.totalPixels = fb.width() * fb.height();
            r.mismatchedPixels = 0;
            r.maxDelta = 0;
            r.avgDelta = 0;
            r.mismatchRatio = 0;
            std::printf("[REGRESSION] Golden created: %s\n", path.c_str());
            return r;
        }
        // 构建 Framebuffer 从 img
        Framebuffer golden(img.width, img.height);
        for (int y = 0; y < img.height; ++y)
            for (int x = 0; x < img.width; ++x)
                golden.setPixel(x, y, img.at(x, y));
        return compare(golden, fb, pixelThreshold);
    }

} // namespace regression
