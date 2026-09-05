#pragma once
#include <cstdlib>

#include "core/framebuffer.h"

// ============================================================
// Bresenham 画线算法
//
// 问题：给定两个整数端点 (x0,y0)-(x1,y1)，要点亮哪些像素？
//
// 朴素思路是 y = k*x + b 逐 x 求 y，但有两个毛病：
//   1. 浮点运算慢（80 年代的痛点，今天主要是精度和美感问题）
//   2. 斜率 |k|>1 时逐 x 采样会出现断点（y 每次跨越超过 1 像素）
//
// Bresenham 的两个核心技巧：
//   A. 斜线陡峭（|dy|>|dx|）时交换 x/y 轴处理（transpose），
//      保证沿"长轴"逐像素前进，短轴每步最多挪 1 格 → 不断线
//   B. 用整数误差累积代替浮点：维护 error2 = 2*误差*dx，
//      每前进一格长轴，误差累积 2*|dy|；一旦超过 dx，
//      短轴挪一格并扣除 2*dx。整个过程只有整数加减和比较。
//
// 上面的理论看不懂没关系，建议照着 drawLine 函数体里的注释，
// 用纸笔演算一遍 (0,0)->(5,2) 的例子，跑完就通了。
// ============================================================

// 在帧缓冲 fb 上画一条从 (x0,y0) 到 (x1,y1) 的线段，颜色为 color。
// fb.setPixel(x, y, color) 的作用就是把 (x,y) 这个像素涂成 color。
inline void drawLine(Framebuffer& fb, int x0, int y0, int x1, int y1, Color color)
{
    // ---------- 预处理：把"各种方向"的线统一成"从左往右的平缓线" ----------

    // 技巧 A：判断这条线是否"陡峭"。
    // 斜率 = dy/dx，|dy| > |dx| 即斜率绝对值 > 1（接近竖直的线）。
    // 竖直的线如果逐 x 采样，x 每走 1 格 y 要跳好几格，画出来是断的。
    bool steep = std::abs(y1 - y0) > std::abs(x1 - x0);
    if (steep)
    {
        // 把 x 和 y 互换（相当于坐标系转置 90°）：
        // 原来的"竖直陡线"变成"水平平缓线"，之后就能放心地逐 x 画了。
        // 代价是真正画像素时要把 x、y 换回来（见循环体内）。
        std::swap(x0, y0);
        std::swap(x1, y1);
    }
    // 保证 x0 <= x1，即从左往右画。
    // 这样循环只需让 x 从 x0 递增到 x1，省掉"往左画"的负方向分支。
    if (x0 > x1)
    {
        std::swap(x0, x1);
        std::swap(y0, y1);
    }

    // ---------- 准备工作：算出斜率相关的三个量 ----------
    const int dx = x1 - x0;                // 长轴总步数：x 一共要走多少格
    const int dy = std::abs(y1 - y0);      // 短轴总步数：y 最多能挪多少格
    const int ystep = (y1 > y0) ? 1 : -1;  // y 的走向：终点在上方就每步 +1，在下方就 -1

    // ---------- 核心：整数误差累积，决定 y 什么时候"跳"一格 ----------
    // 理想直线上 x 处的 y 是 y0 + (dy/dx)*(x-x0)，但 (dy/dx) 是分数，
    // 像素只能落在整数格上，所以让 y 先在原地，误差攒够了再进位。
    //
    // error2 = 2 * dx * 实际误差，把分数运算整体放大成整数：
    //   每走一格 x，误差理应增加 dy/dx → error2 增加 2*dy；
    //   当误差超过半格（error2 > dx）时，y 挪一格，
    //   同时回吐一整格的误差（error2 减去 2*dx），保持总量平衡。
    //
    // 纸上演算 (0,0)->(5,2)：dx=5, dy=2, ystep=1
    //   x=0: error2=0   画(0,0) → +4=4, 4>5? 否 → y 不动
    //   x=1: error2=4   画(1,0) → +4=8, 8>5? 是 → y=1, error2=8-10=-2
    //   x=2: error2=-2  画(2,1) → +4=2, 2>5? 否 → y 不动
    //   x=3: error2=2   画(3,1) → +4=6, 6>5? 是 → y=2, error2=6-10=-4
    //   x=4: error2=-4  画(4,2) → +4=0, 0>5? 否 → y 不动
    //   x=5: error2=0   画(5,2)
    //   y 的轨迹 0,0,1,1,2,2，正好是最接近斜率 0.4 的像素序列。
    int error2 = 0;
    int y = y0;
    for (int x = x0; x <= x1; ++x)  // 沿长轴逐像素前进
    {
        if (steep)
            fb.setPixel(y, x, color);  // 前面转置过，画的时候把 x、y 换回来
        else
            fb.setPixel(x, y, color);
        error2 += 2 * dy;  // 每走一格 x，误差累积 dy/dx（放大 2*dx 倍）
        if (error2 > dx)   // 误差超过半格，说明 y 该挪一格了
        {
            y += ystep;        // y 沿走向挪一格
            error2 -= 2 * dx;  // 回吐一整格的误差，保证累积量平衡
        }
    }
}

// 渐变色版本：颜色沿线段从 c0 平滑过渡到 c1。
// 思路：给线上的每个点算一个 0~1 的插值参数 t（起点 t=0，终点 t=1），
// 再用 t 在 c0、c1 之间做线性插值（Color::lerp）。
// 这其实是"插值"概念的预习——阶段 2 三角形的重心插值就是它的二维推广。
inline void drawLineGradient(Framebuffer& fb, int x0, int y0, int x1, int y1, Color c0, Color c1)
{
    // 与 drawLine 相同的预处理：陡峭线转置、保证从左往右画
    bool steep = std::abs(y1 - y0) > std::abs(x1 - x0);
    if (steep)
    {
        std::swap(x0, y0);
        std::swap(x1, y1);
    }
    if (x0 > x1)
    {
        std::swap(x0, x1);
        std::swap(y0, y1);
        std::swap(c0, c1);  // 端点交换了，颜色也要跟着换，保证 c0 始终对应起点
    }

    // 和 drawLine 完全相同的 Bresenham 主循环（斜率相关量）
    const int dx = x1 - x0;
    const int dy = std::abs(y1 - y0);
    const int ystep = (y1 > y0) ? 1 : -1;

    int error2 = 0;
    int y = y0;
    for (int x = x0; x <= x1; ++x)
    {
        // t = (x-x0)/dx：当前走到了全程的百分之几，范围 [0,1]
        // dx == 0 时整条线缩成一个点，取 t=0 防止除零
        float t = dx == 0 ? 0.0f : float(x - x0) / float(dx);
        Color c = Color::lerp(c0, c1, t);  // 在 c0→c1 之间按比例取色
        if (steep)
            fb.setPixel(y, x, c);
        else
            fb.setPixel(x, y, c);
        error2 += 2 * dy;
        if (error2 > dx)
        {
            y += ystep;
            error2 -= 2 * dx;
        }
    }
}
