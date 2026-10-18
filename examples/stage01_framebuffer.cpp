// ============================================================
// 阶段 01：Framebuffer + Bresenham 画线
//
// 【源码阅读】先读这些文件，再来看本示例：
//   core/framebuffer.h  — 帧缓冲：像素数组 + 坐标约定（y 向上，左下角原点）
//   core/color.h        — 颜色：RGBA8 + lerp 线性插值
//   raster/shape2d.h    — Bresenham 画线：整数误差累积，陡峭线转置
//   core/tga.h          — TGA 图像写出
//
// 【运行验证】生成 output/stage01_framebuffer.tga：
//   - 32 方向放射线：验证所有斜率/方向下不断线
//   - 渐变五角星：验证颜色插值
//   - 底部色带 + 白色边框：验证坐标系
// ============================================================
#include <cmath>
#include <cstdio>
#include <filesystem>
#include "core/framebuffer.h"
#include "core/tga.h"
#include "raster/shape2d.h"

int main()
{
    const int W = 800, H = 600;
    Framebuffer fb(W, H, Color(24, 24, 32)); // 深灰蓝底色

    // ---------- 1. 米字型放射线：从中心向 32 个方向画线 ----------
    // 覆盖所有八个卦限的斜率（水平、垂直、陡峭、平缓、正负方向），
    // 这是画线算法最经典的自测图案
    {
        const int cx = 200, cy = 380, r = 160;
        const float PI = 3.14159265f;
        for (int i = 0; i < 32; ++i)
        {
            float angle = 2.0f * PI * i / 32.0f;
            int ex = cx + int(r * std::cos(angle));
            int ey = cy + int(r * std::sin(angle));
            // 按角度着色，方便看出每根线的方向
            Color c = Color::fromFloat(0.5f + 0.5f * std::cos(angle),
                                       0.5f + 0.5f * std::sin(angle), 0.9f);
            drawLine(fb, cx, cy, ex, ey, c);
        }
    }

    // ---------- 2. 渐变线框五角星 ----------
    {
        const int cx = 560, cy = 380, r = 170;
        const float PI = 3.14159265f;
        int px[5], py[5];
        for (int i = 0; i < 5; ++i)
        {
            // 顶点从正上方开始，逆时针分布
            float angle = PI / 2 + 2.0f * PI * i / 5.0f;
            px[i] = cx + int(r * std::cos(angle));
            py[i] = cy + int(r * std::sin(angle));
        }
        // 隔一个顶点连线（0-2-4-1-3-0）构成五角星
        Color palette[5] = {colors::red, colors::yellow, colors::green,
                            colors::cyan, colors::magenta};
        for (int i = 0; i < 5; ++i)
        {
            int j = (i * 2) % 5, k = ((i + 1) * 2) % 5;
            draw2d::drawLineGradient(fb, px[j], py[j], px[k], py[k], palette[i],
                             palette[(i + 1) % 5]);
        }
    }

    // ---------- 3. 底部渐变色带：直观感受线性插值 ----------
    for (int x = 40; x < W - 40; ++x)
    {
        float t = float(x - 40) / float(W - 80);
        Color c = Color::lerp(colors::blue, colors::red, t);
        drawLine(fb, x, 40, x, 120, c);
    }

    // ---------- 4. 白色边框：验证四角坐标 ----------
    drawLine(fb, 0, 0, W - 1, 0, colors::white);
    drawLine(fb, W - 1, 0, W - 1, H - 1, colors::white);
    drawLine(fb, W - 1, H - 1, 0, H - 1, colors::white);
    drawLine(fb, 0, H - 1, 0, 0, colors::white);

    std::filesystem::create_directories("output");
    const char *path = "output/stage01_framebuffer.tga";
    if (!tga::write(path, fb))
    {
        std::printf("写文件失败: %s\n", path);
        return 1;
    }
    std::printf("已生成 %s (%dx%d)\n", path, W, H);
    return 0;
}
