// ============================================================
// 阶段 04：三角形光栅化
//
// 【源码阅读】先读这些文件：
//   raster/triangle.h  — 边函数填充 + 重心坐标插值
//   core/framebuffer.h — setPixel / row() 直写
//   core/color.h       — Color::lerp 顶点色插值
//
// 【运行验证】生成 output/stage04_triangle.tga：
//   - 左上：RGB 大三角形（重心坐标的可视化）
//   - 右上：随机三角形扇 + 线框叠加
//   - 下方：三角形拼出"低多边形"风景
// ============================================================
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <random>
#include "core/framebuffer.h"
#include "core/tga.h"
#include "raster/triangle.h"

int main()
{
    const int W = 800, H = 600;
    Framebuffer fb(W, H, Color(24, 24, 32));

    // ---------- 1. 经典 RGB 插值三角形 ----------
    // 三个顶点分别是纯红/纯绿/纯蓝，内部每个像素的颜色
    // 就是它的重心坐标 (α,β,γ) 本身——这张图等于把重心坐标可视化了
    fillTriangleGradient(fb, {80, 250}, {360, 250}, {220, 560},
                         colors::red, colors::green, colors::blue);

    // ---------- 2. 三角形扇 + 线框 ----------
    {
        const float PI = 3.14159265f;
        Vec2f center{600, 400};
        const int N = 8;
        std::mt19937 rng(42);
        std::uniform_int_distribution<int> ch(60, 255);
        for (int i = 0; i < N; ++i)
        {
            float a0 = 2 * PI * i / N, a1 = 2 * PI * (i + 1) / N;
            Vec2f p0{center.x + 150 * std::cos(a0), center.y + 150 * std::sin(a0)};
            Vec2f p1{center.x + 150 * std::cos(a1), center.y + 150 * std::sin(a1)};
            Color c(uint8_t(ch(rng)), uint8_t(ch(rng)), uint8_t(ch(rng)));
            fillTriangle(fb, center, p0, p1, c);
            wireTriangle(fb, center, p0, p1, colors::white);
        }
    }

    // ---------- 3. 低多边形山脉（三角形是构图的原子）----------
    {
        Color dark(40, 60, 90), mid(60, 90, 130), light(90, 130, 170);
        // 后排山
        fillTriangle(fb, {0, 60}, {150, 60}, {60, 200}, mid);
        fillTriangle(fb, {90, 60}, {330, 60}, {210, 230}, dark);
        fillTriangle(fb, {260, 60}, {480, 60}, {380, 190}, mid);
        fillTriangle(fb, {430, 60}, {650, 60}, {540, 220}, dark);
        fillTriangle(fb, {580, 60}, {800, 60}, {700, 180}, mid);
        // 前排亮色山
        fillTriangle(fb, {40, 60}, {260, 60}, {150, 160}, light);
        fillTriangle(fb, {340, 60}, {560, 60}, {450, 150}, light);
        // 太阳（三角形扇拼圆）
        const float PI = 3.14159265f;
        Vec2f sc{700, 120};
        for (int i = 0; i < 16; ++i)
        {
            float a0 = 2 * PI * i / 16, a1 = 2 * PI * (i + 1) / 16;
            fillTriangle(fb, sc, {sc.x + 36 * std::cos(a0), sc.y + 36 * std::sin(a0)},
                         {sc.x + 36 * std::cos(a1), sc.y + 36 * std::sin(a1)},
                         Color(255, 200, 80));
        }
    }

    std::filesystem::create_directories("output");
    const char *path = "output/stage04_triangle.tga";
    if (!tga::write(path, fb))
    {
        std::printf("写文件失败: %s\n", path);
        return 1;
    }
    std::printf("已生成 %s (%dx%d)\n", path, W, H);
    return 0;
}
