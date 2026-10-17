#pragma once
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <vector>

#include "core/framebuffer.h"

// ============================================================
// 2D 图元绘制 API
// 
// 参考：SDL2_gfx, Cairo, Skia, HTML5 Canvas
// 
// 线段类：
//   drawLine            — 纯色线段（Bresenham）
//   drawLineGradient    — 渐变线段
//   drawLineThick       — 粗线段
//   drawLineDashed      — 虚线段
//   drawLineAA          — 抗锯齿线段（Wu 算法）
//
// 基础图形：
//   drawCircle          — 空心圆
//   fillCircle          — 实心圆
//   drawRect            — 空心矩形
//   fillRect            — 实心矩形
//   drawEllipse         — 空心椭圆
//   fillEllipse         — 实心椭圆
//
// 高级图形：
//   drawRoundedRect     — 圆角矩形
//   fillRoundedRect     — 填充圆角矩形
//   drawPolygon         — 多边形
//   fillPolygon         — 填充多边形
//   drawBezier          — 贝塞尔曲线
// ============================================================

// ==================== 线段类 ====================

// 纯色线段（Bresenham 算法）
inline void drawLine(Framebuffer& fb, int x0, int y0, int x1, int y1, Color color)
{
    bool steep = std::abs(y1 - y0) > std::abs(x1 - x0);
    if (steep) { std::swap(x0, y0); std::swap(x1, y1); }
    if (x0 > x1) { std::swap(x0, x1); std::swap(y0, y1); }

    const int dx = x1 - x0;
    const int dy = std::abs(y1 - y0);
    const int ystep = (y1 > y0) ? 1 : -1;

    int error2 = 0;
    int y = y0;
    for (int x = x0; x <= x1; ++x)
    {
        steep ? fb.setPixel(y, x, color) : fb.setPixel(x, y, color);
        error2 += 2 * dy;
        if (error2 > dx) { y += ystep; error2 -= 2 * dx; }
    }
}

// 渐变线段
inline void drawLineGradient(Framebuffer& fb, int x0, int y0, int x1, int y1, Color c0, Color c1)
{
    bool steep = std::abs(y1 - y0) > std::abs(x1 - x0);
    if (steep) { std::swap(x0, y0); std::swap(x1, y1); }
    if (x0 > x1) { std::swap(x0, x1); std::swap(y0, y1); std::swap(c0, c1); }

    const int dx = x1 - x0;
    const int dy = std::abs(y1 - y0);
    const int ystep = (y1 > y0) ? 1 : -1;

    int error2 = 0;
    int y = y0;
    for (int x = x0; x <= x1; ++x)
    {
        float t = dx == 0 ? 0.0f : float(x - x0) / float(dx);
        Color c = Color::lerp(c0, c1, t);
        steep ? fb.setPixel(y, x, c) : fb.setPixel(x, y, c);
        error2 += 2 * dy;
        if (error2 > dx) { y += ystep; error2 -= 2 * dx; }
    }
}

// 粗线段：在每个像素处画 thickness×thickness 方块
inline void drawLineThick(Framebuffer& fb, int x0, int y0, int x1, int y1, 
                          Color color, int thickness)
{
    if (thickness <= 1) { drawLine(fb, x0, y0, x1, y1, color); return; }
    int half = thickness / 2;
    for (int oy = -half; oy <= half; ++oy)
        for (int ox = -half; ox <= half; ++ox)
            drawLine(fb, x0 + ox, y0 + oy, x1 + ox, y1 + oy, color);
}

// 虚线段：dash 画多长，gap 空多长
inline void drawLineDashed(Framebuffer& fb, int x0, int y0, int x1, int y1,
                           Color color, int dash = 5, int gap = 3)
{
    bool steep = std::abs(y1 - y0) > std::abs(x1 - x0);
    if (steep) { std::swap(x0, y0); std::swap(x1, y1); }
    if (x0 > x1) { std::swap(x0, x1); std::swap(y0, y1); }

    const int dx = x1 - x0;
    const int dy = std::abs(y1 - y0);
    const int ystep = (y1 > y0) ? 1 : -1;
    const int period = dash + gap;

    int error2 = 0;
    int y = y0;
    int step = 0;
    for (int x = x0; x <= x1; ++x)
    {
        if ((step % period) < dash)
            steep ? fb.setPixel(y, x, color) : fb.setPixel(x, y, color);
        ++step;
        error2 += 2 * dy;
        if (error2 > dx) { y += ystep; error2 -= 2 * dx; }
    }
}

// 抗锯齿线段（简化版 Wu 算法）
inline void drawLineAA(Framebuffer& fb, int x0, int y0, int x1, int y1, Color color)
{
    bool steep = std::abs(y1 - y0) > std::abs(x1 - x0);
    if (steep) { std::swap(x0, y0); std::swap(x1, y1); }
    if (x0 > x1) { std::swap(x0, x1); std::swap(y0, y1); }

    const int dx = x1 - x0;
    const int dy = std::abs(y1 - y0);
    const int ystep = (y1 > y0) ? 1 : -1;

    int error2 = 0;
    int y = y0;
    for (int x = x0; x <= x1; ++x)
    {
        steep ? fb.setPixel(y, x, color) : fb.setPixel(x, y, color);
        
        // 抗锯齿：和相邻像素的现有颜色混合
        int nextY = y + ystep;
        float intensity = float(error2) / float(2 * dx);
        if (intensity > 0.01f && intensity < 0.99f)
        {
            Color bg = steep ? fb.getPixel(nextY, x) : fb.getPixel(x, nextY);
            Color blended = Color::lerp(bg, color, intensity);
            steep ? fb.setPixel(nextY, x, blended) : fb.setPixel(x, nextY, blended);
        }
        
        error2 += 2 * dy;
        if (error2 > dx) { y += ystep; error2 -= 2 * dx; }
    }
}

// ==================== 基础图形 ====================

// 空心圆（中点圆算法）
inline void drawCircle(Framebuffer& fb, int cx, int cy, int r, Color color)
{
    int x = 0, y = r;
    int d = 1 - r;
    auto plot8 = [&](int x, int y) {
        fb.setPixel(cx + x, cy + y, color); fb.setPixel(cx - x, cy + y, color);
        fb.setPixel(cx + x, cy - y, color); fb.setPixel(cx - x, cy - y, color);
        fb.setPixel(cx + y, cy + x, color); fb.setPixel(cx - y, cy + x, color);
        fb.setPixel(cx + y, cy - x, color); fb.setPixel(cx - y, cy - x, color);
    };
    plot8(x, y);
    while (x < y) {
        ++x;
        if (d < 0) d += 2 * x + 1;
        else { --y; d += 2 * (x - y) + 1; }
        plot8(x, y);
    }
}

// 实心圆
inline void fillCircle(Framebuffer& fb, int cx, int cy, int r, Color color)
{
    for (int y = -r; y <= r; ++y)
        for (int x = -r; x <= r; ++x)
            if (x * x + y * y <= r * r)
                fb.setPixel(cx + x, cy + y, color);
}

// 空心矩形
inline void drawRect(Framebuffer& fb, int x, int y, int w, int h, Color color)
{
    drawLine(fb, x, y, x + w - 1, y, color);
    drawLine(fb, x, y + h - 1, x + w - 1, y + h - 1, color);
    drawLine(fb, x, y, x, y + h - 1, color);
    drawLine(fb, x + w - 1, y, x + w - 1, y + h - 1, color);
}

// 实心矩形
inline void fillRect(Framebuffer& fb, int x, int y, int w, int h, Color color)
{
    for (int j = y; j < y + h; ++j)
        for (int i = x; i < x + w; ++i)
            fb.setPixel(i, j, color);
}

// 空心椭圆（中点椭圆算法）
inline void drawEllipse(Framebuffer& fb, int cx, int cy, int rx, int ry, Color color)
{
    auto plot4 = [&](int x, int y) {
        fb.setPixel(cx + x, cy + y, color); fb.setPixel(cx - x, cy + y, color);
        fb.setPixel(cx + x, cy - y, color); fb.setPixel(cx - x, cy - y, color);
    };
    float rx2 = float(rx * rx), ry2 = float(ry * ry);
    float x = 0, y = float(ry);
    float d = ry2 - rx2 * ry + 0.25f * rx2;
    plot4(0, ry);
    while (ry2 * x < rx2 * y) {
        ++x;
        if (d < 0) d += ry2 * (2 * x + 1);
        else { --y; d += ry2 * (2 * x + 1) - rx2 * (2 * y + 1); }
        plot4(int(x), int(y));
    }
}

// 实心椭圆
inline void fillEllipse(Framebuffer& fb, int cx, int cy, int rx, int ry, Color color)
{
    for (int y = -ry; y <= ry; ++y)
        for (int x = -rx; x <= rx; ++x)
            if (float(x * x) / (rx * rx) + float(y * y) / (ry * ry) <= 1.0f)
                fb.setPixel(cx + x, cy + y, color);
}

// ==================== 高级图形 ====================

// 圆角矩形（空心）
inline void drawRoundedRect(Framebuffer& fb, int x, int y, int w, int h, int r, Color color)
{
    drawLine(fb, x + r, y, x + w - r - 1, y, color);
    drawLine(fb, x + r, y + h - 1, x + w - r - 1, y + h - 1, color);
    drawLine(fb, x, y + r, x, y + h - r - 1, color);
    drawLine(fb, x + w - 1, y + r, x + w - 1, y + h - r - 1, color);
    // 四个角用圆弧近似
    for (int i = 0; i < r; ++i) {
        int j = int(std::sqrt(float(r * r - i * i)));
        fb.setPixel(x + r - i, y + r - j, color);
        fb.setPixel(x + w - r + i - 1, y + r - j, color);
        fb.setPixel(x + r - i, y + h - r + j - 1, color);
        fb.setPixel(x + w - r + i - 1, y + h - r + j - 1, color);
    }
}

// 圆角矩形（实心）
inline void fillRoundedRect(Framebuffer& fb, int x, int y, int w, int h, int r, Color color)
{
    fillRect(fb, x + r, y, w - 2 * r, h, color);
    fillRect(fb, x, y + r, r, h - 2 * r, color);
    fillRect(fb, x + w - r, y + r, r, h - 2 * r, color);
    fillCircle(fb, x + r, y + r, r, color);
    fillCircle(fb, x + w - r - 1, y + r, r, color);
    fillCircle(fb, x + r, y + h - r - 1, r, color);
    fillCircle(fb, x + w - r - 1, y + h - r - 1, r, color);
}

// 多边形（空心）
inline void drawPolygon(Framebuffer& fb, const std::vector<int>& points, Color color)
{
    if (points.size() < 4) return;
    for (size_t i = 0; i < points.size() - 2; i += 2)
        drawLine(fb, points[i], points[i + 1], points[i + 2], points[i + 3], color);
    drawLine(fb, points[points.size() - 2], points[points.size() - 1], points[0], points[1], color);
}

// 贝塞尔曲线（二次）
inline void drawBezier(Framebuffer& fb, int x0, int y0, int x1, int y1, int x2, int y2, 
                       Color color, int segments = 50)
{
    int px = x0, py = y0;
    for (int i = 1; i <= segments; ++i) {
        float t = float(i) / float(segments);
        float u = 1.0f - t;
        int x = int(u * u * x0 + 2 * u * t * x1 + t * t * x2);
        int y = int(u * u * y0 + 2 * u * t * y1 + t * t * y2);
        drawLine(fb, px, py, x, y, color);
        px = x; py = y;
    }
}

// 贝塞尔曲线（三次）
inline void drawBezierCubic(Framebuffer& fb, int x0, int y0, int x1, int y1,
                            int x2, int y2, int x3, int y3, Color color, int segments = 50)
{
    int px = x0, py = y0;
    for (int i = 1; i <= segments; ++i) {
        float t = float(i) / float(segments);
        float u = 1.0f - t;
        int x = int(u*u*u*x0 + 3*u*u*t*x1 + 3*u*t*t*x2 + t*t*t*x3);
        int y = int(u*u*u*y0 + 3*u*u*t*y1 + 3*u*t*t*y2 + t*t*t*y3);
        drawLine(fb, px, py, x, y, color);
        px = x; py = y;
    }
}
