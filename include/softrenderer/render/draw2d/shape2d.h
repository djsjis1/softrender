#pragma once
#include <cstdint>
#include <cstddef>
#include <initializer_list>
#include <utility>
#include <algorithm>
#include <cmath>
#include <vector>

#include "softrenderer/core/blend.h"
#include "softrenderer/core/framebuffer.h"
#include "softrenderer/math/vec.h"

// ============================================================
// 2D 图元库（shape2d）—— 立即模式 2D 绘制底座
//
// 与 3D 管线（renderer.h）共用同一个 Framebuffer，但职责不同：
//   3D 管线 → 三角形 + 深度 + 着色器，处理透视/光照/遮挡
//   本层    → 矩形/圆/多边形/UI，处理屏幕定位/透明叠加/裁剪区
//
// 坐标：x 向右、y 向上、(0,0) 左下角（与 Framebuffer 一致）。
//       填充图元以像素中心 (x+0.5, y+0.5) 为采样点，边缘更稳定。
//
// 所有函数统一接受两个可选参数：
//   BlendMode mode        — 混合方式，默认 Opaque（直接覆盖）
//   const ClipRect* clip  — 额外裁剪矩形，默认 nullptr（只裁到屏幕边界）
// ============================================================

namespace sr::draw2d
{

// 2D 图元的浮点参数统一做有限性检查。
//
// 坐标、半径、宽高都是调用方给的浮点，一旦是 NaN/Inf，后面的
// `int(std::floor(...))` / `int(std::ceil(...))` 就是未定义行为；
// 而 `r <= 0` 这类判据对 NaN 恒为假，拦不住。
inline bool allFinite(std::initializer_list<float> values)
{
    for (float v : values)
        if (!std::isfinite(v))
            return false;
    return true;
}

// ── 裁剪矩形 ──────────────────────────────────────────────
// 半开区间 [x0,x1) × [y0,y1)，用于 scissor 裁剪。
// 用途：UI 滚动时只画可见部分、局部刷新、限制绘制区域。
struct ClipRect
{
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;

    ClipRect() = default;
    ClipRect(int x0_, int y0_, int x1_, int y1_) : x0(x0_), y0(y0_), x1(x1_), y1(y1_) {}

    bool contains(int x, int y) const { return x >= x0 && x < x1 && y >= y0 && y < y1; }
    bool empty() const { return x0 >= x1 || y0 >= y1; }
};


// ── 唯一像素写入出口 ──────────────────────────────────────
// 所有 2D 图元（矩形、圆、多边形……）最终都经过此函数写像素。
// 职责：边界检查 → scissor 裁剪 → Opaque 快路径 → 混合写入。
// 绕过它直接调 setPixel 会让 scissor 和 BlendMode 静默失效。
template <class FB>
void plotBlend(FB& fb, int x, int y, Color c,
                      BlendMode mode = BlendMode::Opaque,
                      const ClipRect* clip = nullptr)
{
    if (x < 0 || x >= fb.width() || y < 0 || y >= fb.height())
        return;
    if (clip && !clip->contains(x, y))
        return;
    // 快路径：Opaque 恒覆盖；AlphaBlend/Premultiplied 在 α=255 时等价覆盖。
    // Additive/Multiply 即使 α=255 仍需与 dst 运算，不能短路。
    if (mode == BlendMode::Opaque ||
        (c.a == 255 && (mode == BlendMode::AlphaBlend || mode == BlendMode::Premultiplied)))
    {
        fb.setPixel(x, y, c);
        return;
    }
    fb.setPixel(x, y, blendPixels(c, fb.getPixel(x, y), mode));
}

// ── 线段族 ────────────────────────────────────────────────
// 全部线段走 plotBlend，统一支持混合与裁剪。

// 基础 Bresenham 画线：纯色、整数、无浮点。
// 用途：线框调试、UI 分割线、简单连线。
template <class FB>
void drawLine(FB& fb, int x0, int y0, int x1, int y1, Color c,
                     BlendMode mode = BlendMode::Opaque,
                     const ClipRect* clip = nullptr)
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
        steep ? plotBlend(fb, y, x, c, mode, clip) : plotBlend(fb, x, y, c, mode, clip);
        error2 += 2 * dy;
        if (error2 > dx) { y += ystep; error2 -= 2 * dx; }
    }
}

// 渐变线段：从 c0 线性插值到 c1。
// 用途：地平线渐变、光线、装饰线。
template <class FB>
void drawLineGradient(FB& fb, int x0, int y0, int x1, int y1,
                             Color c0, Color c1,
                             BlendMode mode = BlendMode::Opaque,
                             const ClipRect* clip = nullptr)
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
        steep ? plotBlend(fb, y, x, c, mode, clip) : plotBlend(fb, x, y, c, mode, clip);
        error2 += 2 * dy;
        if (error2 > dx) { y += ystep; error2 -= 2 * dx; }
    }
}

// 整数粗线：在每个 Bresenham 像素处画 thickness×thickness 方块。
// 比 drawThickLine（浮点法线扩展）更轻量，适合像素风粗线。
template <class FB>
void drawLineThick(FB& fb, int x0, int y0, int x1, int y1,
                          Color c, int thickness,
                          BlendMode mode = BlendMode::Opaque,
                          const ClipRect* clip = nullptr)
{
    if (thickness <= 1) { drawLine(fb, x0, y0, x1, y1, c, mode, clip); return; }
    // 两根约束：
    // 1. 用 -half..half 铺平行线时，偶数厚度会多画一条（thickness=4 画出 5 像素宽），
    //    与「thickness×thickness」的注释不符。换成 lo..hi 后总行数恰为 thickness。
    // 2. 本函数是 O(thickness²) 条 Bresenham，超过画布尺寸只会白烧时间。
    thickness = std::min(thickness, std::max(fb.width(), fb.height()));
    const int lo = -(thickness - 1) / 2;
    const int hi = thickness / 2;
    for (int oy = lo; oy <= hi; ++oy)
        for (int ox = lo; ox <= hi; ++ox)
            drawLine(fb, x0 + ox, y0 + oy, x1 + ox, y1 + oy, c, mode, clip);
}

// 虚线：dash 画多长，gap 空多长（像素步数）。
// 用途：辅助线、选中框、边界标记。
template <class FB>
void drawLineDashed(FB& fb, int x0, int y0, int x1, int y1,
                           Color c, int dash = 5, int gap = 3,
                           BlendMode mode = BlendMode::Opaque,
                           const ClipRect* clip = nullptr)
{
    bool steep = std::abs(y1 - y0) > std::abs(x1 - x0);
    if (steep) { std::swap(x0, y0); std::swap(x1, y1); }
    if (x0 > x1) { std::swap(x0, x1); std::swap(y0, y1); }
    const int dx = x1 - x0;
    const int dy = std::abs(y1 - y0);
    const int ystep = (y1 > y0) ? 1 : -1;
    // dash/gap 是调用方可直接传的整数：period 为 0 时 `step % period` 是整数除零，
    // 负 gap 又会让周期变成负数。两者都是未定义行为，编译器可对结果做任意优化
    // （实测传入 (0,0) 时一个像素都不画）。dash <= 0 退化为实线，gap < 0 视为 0。
    if (dash <= 0)
        dash = 1;
    const int period = dash + std::max(0, gap);
    int error2 = 0;
    int y = y0;
    int step = 0;
    for (int x = x0; x <= x1; ++x)
    {
        if ((step % period) < dash)
            steep ? plotBlend(fb, y, x, c, mode, clip) : plotBlend(fb, x, y, c, mode, clip);
        ++step;
        error2 += 2 * dy;
        if (error2 > dx) { y += ystep; error2 -= 2 * dx; }
    }
}

// 抗锯齿线段（简化 Wu 算法）：主线像素 + 相邻像素按误差比例混合。
// 用途：高质量线条、矢量图形描边。
template <class FB>
void drawLineAA(FB& fb, int x0, int y0, int x1, int y1, Color c,
                       BlendMode mode = BlendMode::AlphaBlend,
                       const ClipRect* clip = nullptr)
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
        // 主线像素
        steep ? plotBlend(fb, y, x, c, mode, clip) : plotBlend(fb, x, y, c, mode, clip);
        // 抗锯齿：误差比例决定相邻像素的透明度
        int nextY = y + ystep;
        float intensity = dx == 0 ? 0.0f : float(error2) / float(2 * dx);
        if (intensity > 0.01f && intensity < 0.99f)
        {
            // 边缘透明度要乘源颜色的 alpha：否则完全透明的线仍会画出边缘。
            const int edgeAlpha = int(intensity * float(c.a) + 0.5f);
            Color edge = Color(c.r, c.g, c.b, static_cast<uint8_t>(std::clamp(edgeAlpha, 0, 255)));
            steep ? plotBlend(fb, nextY, x, edge, BlendMode::AlphaBlend, clip)
                  : plotBlend(fb, x, nextY, edge, BlendMode::AlphaBlend, clip);
        }
        error2 += 2 * dy;
        if (error2 > dx) { y += ystep; error2 -= 2 * dx; }
    }
}

// 二次贝塞尔曲线：P0→P1→P2，segments 为细分段数。
// 用途：平滑曲线、图标路径、UI 装饰。
template <class FB>
void drawBezier(FB& fb, int x0, int y0, int x1, int y1, int x2, int y2,
                       Color c, int segments = 50,
                       BlendMode mode = BlendMode::Opaque,
                       const ClipRect* clip = nullptr)
{
    int px = x0, py = y0;
    for (int i = 1; i <= segments; ++i)
    {
        float t = float(i) / float(segments);
        float u = 1.0f - t;
        int x = int(u * u * x0 + 2 * u * t * x1 + t * t * x2);
        int y = int(u * u * y0 + 2 * u * t * y1 + t * t * y2);
        drawLine(fb, px, py, x, y, c, mode, clip);
        px = x; py = y;
    }
}

// 三次贝塞尔曲线：P0→P1→P2→P3。
// 用途：字体轮廓、复杂曲线、动画路径可视化。
template <class FB>
void drawBezierCubic(FB& fb, int x0, int y0, int x1, int y1,
                            int x2, int y2, int x3, int y3,
                            Color c, int segments = 50,
                            BlendMode mode = BlendMode::Opaque,
                            const ClipRect* clip = nullptr)
{
    int px = x0, py = y0;
    for (int i = 1; i <= segments; ++i)
    {
        float t = float(i) / float(segments);
        float u = 1.0f - t;
        int x = int(u*u*u*x0 + 3*u*u*t*x1 + 3*u*t*t*x2 + t*t*t*x3);
        int y = int(u*u*u*y0 + 3*u*u*t*y1 + 3*u*t*t*y2 + t*t*t*y3);
        drawLine(fb, px, py, x, y, c, mode, clip);
        px = x; py = y;
    }
}

// 前向声明：drawEllipse / drawPolygon 的加粗描边需要 drawThickLine，
// 而 drawThickLine 定义在文件后部（依赖 fillPolygon），故先给原型。
//
// 【默认实参必须写在原型上】非模板函数允许在后续声明里追加默认实参，
// 但函数模板的重复声明不允许（[dcl.fct.default]）——把默认值留在下面的
// 定义处会让 Clang/GCC 直接报 "default arguments cannot be added to a
// function template that has already been declared"，而 MSVC 放过。
template <class FB>
void drawThickLine(FB& fb, float x0, float y0, float x1, float y1,
                          float thickness, Color c, bool roundCaps = true,
                          BlendMode mode = BlendMode::Opaque,
                          const ClipRect* clip = nullptr);

// ============================================================
// 矩形
// 用途：UI 面板、按钮背景、血条、进度条、天空渐变、遮罩
// ============================================================

// 填充矩形：(x,y) 为左下角，w/h 为像素数。
// 先做包围盒裁剪再逐像素画，避免内层循环做越界判断。
template <class FB>
void fillRect(FB& fb, int x, int y, int w, int h, Color c,
                     BlendMode mode = BlendMode::Opaque,
                     const ClipRect* clip = nullptr)
{
    if (w <= 0 || h <= 0)
        return;
    // 裁剪到帧缓冲边界
    int x0 = std::max(0, x), y0 = std::max(0, y);
    int x1 = std::min(fb.width(), x + w), y1 = std::min(fb.height(), y + h);
    // 再裁剪到 scissor 区（如果有的话）
    if (clip)
    {
        x0 = std::max(x0, clip->x0);
        y0 = std::max(y0, clip->y0);
        x1 = std::min(x1, clip->x1);
        y1 = std::min(y1, clip->y1);
    }
    for (int py = y0; py < y1; ++py)
        for (int px = x0; px < x1; ++px)
            plotBlend(fb, px, py, c, mode, nullptr); // 已裁剪，clip 传空免重复判断
}

// 矩形描边：四条 fillRect 拼成边框，thickness 向内扩展
template <class FB>
void drawRect(FB& fb, int x, int y, int w, int h, Color c, int thickness = 1,
                     BlendMode mode = BlendMode::Opaque,
                     const ClipRect* clip = nullptr)
{
    if (w <= 0 || h <= 0 || thickness <= 0)
        return;
    thickness = std::min(thickness, std::min(w, h));
    fillRect(fb, x, y, w, thickness, c, mode, clip);                     // 下边
    fillRect(fb, x, y + h - thickness, w, thickness, c, mode, clip);     // 上边
    fillRect(fb, x, y, thickness, h, c, mode, clip);                     // 左边
    fillRect(fb, x + w - thickness, y, thickness, h, c, mode, clip);     // 右边
}

// 矩形渐变填充：沿 y（下→上 c0→c1）或沿 x（左→右 c0→c1）。
// 用途：天空、进度条、光泽面板、UI 渐变背景。
// t 用像素中心归一化，避免首尾像素只露半截色带。
template <class FB>
void fillRectGradient(FB& fb, int x, int y, int w, int h,
                             Color c0, Color c1, bool horizontal = false,
                             BlendMode mode = BlendMode::Opaque,
                             const ClipRect* clip = nullptr)
{
    if (w <= 0 || h <= 0)
        return;
    for (int j = 0; j < h; ++j)
    {
        for (int i = 0; i < w; ++i)
        {
            int span = horizontal ? w : h;
            int idx = horizontal ? i : j;
            float t = span > 1 ? float(idx) / float(span - 1) : 0.0f;
            plotBlend(fb, x + i, y + j, Color::lerp(c0, c1, t), mode, clip);
        }
    }
}

// ============================================================
// 圆 / 椭圆
// 用途：粒子、光斑、角色碰撞圈、仪表盘、椭圆轨道
// ============================================================

// 填充圆：包围盒内逐像素做勾股距离测试 (dx²+dy² ≤ r²)。
// 圆心支持浮点坐标，实现亚像素级平滑定位。
// 先裁剪包围盒到屏幕/裁剪区，避免对屏外像素做无谓的距离测试。
template <class FB>
void fillCircle(FB& fb, float cx, float cy, float r, Color c,   
                       BlendMode mode = BlendMode::Opaque,
                       const ClipRect* clip = nullptr)
{
    // 坐标是调用方给的浮点：NaN/Inf 会让下面的 int(std::floor(...)) 成为未定义行为，
    // 而 `r <= 0` 这类判断对 NaN 恒为假，拦不住它。
    if (!allFinite({cx, cy, r}) || r <= 0.0f)
        return;
    int minX = std::max(0, (int)std::floor(cx - r));
    int maxX = std::min(fb.width() - 1, (int)std::ceil(cx + r));
    int minY = std::max(0, (int)std::floor(cy - r));
    int maxY = std::min(fb.height() - 1, (int)std::ceil(cy + r));
    if (clip)
    {
        minX = std::max(minX, clip->x0);
        maxX = std::min(maxX, clip->x1 - 1);
        minY = std::max(minY, clip->y0);
        maxY = std::min(maxY, clip->y1 - 1);
    }
    if (minX > maxX || minY > maxY)
        return;
    float r2 = r * r;
    for (int py = minY; py <= maxY; ++py)
    {
        float dy = py + 0.5f - cy; // 像素中心到圆心的 y 距离
        for (int px = minX; px <= maxX; ++px)
        {
            float dx = px + 0.5f - cx; // 像素中心到圆心的 x 距离
            if (dx * dx + dy * dy <= r2)
                plotBlend(fb, px, py, c, mode, clip);
        }
    }
}

// 圆描边：中点圆算法（Bresenham 圆），只算 1/8 弧再八对称镜像。
// 纯整数增量运算，无浮点无三角函数。thickness>1 时画多圈同心圆近似加粗。
template <class FB>
void drawCircle(FB& fb, int cx, int cy, int r, Color c, int thickness = 1,
                       BlendMode mode = BlendMode::Opaque,
                       const ClipRect* clip = nullptr)
{
    for (int t = 0; t < thickness; ++t)
    {
        int rr = r - t;
        if (rr < 0)
            break;
        int x = rr, y = 0;
        int err = 1 - rr; // 决策变量：正=圆外，负=圆内
        while (x >= y)
        {
            // 八对称：一次迭代画 8 个点
            plotBlend(fb, cx + x, cy + y, c, mode, clip);
            plotBlend(fb, cx + y, cy + x, c, mode, clip);
            plotBlend(fb, cx - y, cy + x, c, mode, clip);
            plotBlend(fb, cx - x, cy + y, c, mode, clip);
            plotBlend(fb, cx - x, cy - y, c, mode, clip);
            plotBlend(fb, cx - y, cy - x, c, mode, clip);
            plotBlend(fb, cx + y, cy - x, c, mode, clip);
            plotBlend(fb, cx + x, cy - y, c, mode, clip);
            ++y;
            if (err < 0)
                err += 2 * y + 1; // y 方向前进，决策变量更新
            else
            {
                --x;
                err += 2 * (y - x) + 1; // x 也收缩，决策变量更新
            }
        }
    }
}

// 填充椭圆：归一化距离测试 (dx/rx)²+(dy/ry)² ≤ 1
template <class FB>
void fillEllipse(FB& fb, float cx, float cy, float rx, float ry, Color c,
                        BlendMode mode = BlendMode::Opaque,
                        const ClipRect* clip = nullptr)
{
    if (!allFinite({cx, cy, rx, ry}) || rx <= 0.0f || ry <= 0.0f)
        return;
    int minX = std::max(0, (int)std::floor(cx - rx));
    int maxX = std::min(fb.width() - 1, (int)std::ceil(cx + rx));
    int minY = std::max(0, (int)std::floor(cy - ry));
    int maxY = std::min(fb.height() - 1, (int)std::ceil(cy + ry));
    if (clip)
    {
        minX = std::max(minX, clip->x0);
        maxX = std::min(maxX, clip->x1 - 1);
        minY = std::max(minY, clip->y0);
        maxY = std::min(maxY, clip->y1 - 1);
    }
    if (minX > maxX || minY > maxY)
        return;
    // 倒数预计算：内层原本每像素做两次浮点除法（/rx、/ry），换算成乘法。
    // rx/ry 已在入口校验为正，1/rx 不会产生 Inf。
    // 与除法相比乘倒数在最后一位上有舍入差异，边界像素可能翻转 ——
    // 由 draw2d_test 与像素回归守护；若出现差异就退回除法，不拿确定性换这点性能。
    const float invRx = 1.0f / rx;
    const float invRy = 1.0f / ry;
    for (int py = minY; py <= maxY; ++py)
    {
        const float ny = (py + 0.5f - cy) * invRy; // 归一化 y 坐标
        for (int px = minX; px <= maxX; ++px)
        {
            const float nx = (px + 0.5f - cx) * invRx; // 归一化 x 坐标
            if (nx * nx + ny * ny <= 1.0f)
                plotBlend(fb, px, py, c, mode, clip);
        }
    }
}

// 椭圆描边：参数方程步进 + drawThickLine 连接相邻点。
// 步数随半径增大（周长 ≈ 2πr），避免大圆出现折线感。
template <class FB>
void drawEllipse(FB& fb, float cx, float cy, float rx, float ry, Color c,
                        int thickness = 1,
                        BlendMode mode = BlendMode::Opaque,
                        const ClipRect* clip = nullptr)
{
    if (!allFinite({cx, cy, rx, ry}) || rx <= 0.0f || ry <= 0.0f)
        return;
    const float PI = 3.14159265358979f;
    const float span = 2.0f * PI * std::max(rx, ry);
    // span 可能是 inf（rx 极大），直接转 int 同样是 UB：先夹住上界。
    int steps = std::max(16, span > 4096.0f ? 4096 : int(span));
    int prevX = (int)std::round(cx + rx), prevY = (int)std::round(cy);
    for (int i = 1; i <= steps; ++i)
    {
        float a = 2.0f * PI * float(i) / float(steps);
        float px = cx + rx * std::cos(a);
        float py = cy + ry * std::sin(a);
        drawThickLine(fb, float(prevX), float(prevY), px, py, float(thickness), c, true, mode, clip);
        prevX = (int)std::round(px);
        prevY = (int)std::round(py);
    }
}

// ============================================================
// 圆角矩形
// 用途：UI 面板、按钮、对话框、弹窗、卡片（现代 UI 的标配形状）
// ============================================================

// 圆角矩形 SDF 判定：把点夹到"内矩形"，算到内矩形的最近距离。
// 距离 ≤ r 则在圆角矩形内。一份公式同时覆盖直边段与四个圆角。
// 这就是 Signed Distance Field 思路——用隐式函数描述形状。
inline bool insideRoundedRect(float px, float py, float x, float y, float w, float h, float r)
{
    float cx = std::clamp(px, x + r, x + w - r); // 夹到内矩形 x 范围
    float cy = std::clamp(py, y + r, y + h - r); // 夹到内矩形 y 范围
    float dx = px - cx, dy = py - cy;            // 到内矩形的最近距离向量
    return dx * dx + dy * dy <= r * r;            // 距离 ≤ 半径 → 在内部
}

// 填充圆角矩形：包围盒内逐像素做 SDF 判定
template <class FB>
void fillRoundedRect(FB& fb, float x, float y, float w, float h, float r, Color c,
                            BlendMode mode = BlendMode::Opaque,
                            const ClipRect* clip = nullptr)
{
    if (!allFinite({x, y, w, h, r}) || w <= 0.0f || h <= 0.0f)
        return;
    r = std::max(0.0f, std::min(r, std::min(w, h) * 0.5f)); // 半径不能超过宽高一半
    int minX = (int)std::floor(x), maxX = (int)std::ceil(x + w);
    int minY = (int)std::floor(y), maxY = (int)std::ceil(y + h);
    for (int py = minY; py <= maxY; ++py)
        for (int px = minX; px <= maxX; ++px)
            if (insideRoundedRect(px + 0.5f, py + 0.5f, x, y, w, h, r))
                plotBlend(fb, px, py, c, mode, clip);
}

// 圆角矩形描边：外层 SDF 内 + 内层（收缩 thickness）SDF 外 = 均匀边框
template <class FB>
void drawRoundedRect(FB& fb, float x, float y, float w, float h, float r,
                            Color c, int thickness = 1,
                            BlendMode mode = BlendMode::Opaque,
                            const ClipRect* clip = nullptr)
{
    if (!allFinite({x, y, w, h, r}) || w <= 0.0f || h <= 0.0f || thickness <= 0)
        return;
    float t = float(thickness);
    int minX = (int)std::floor(x), maxX = (int)std::ceil(x + w);
    int minY = (int)std::floor(y), maxY = (int)std::ceil(y + h);
    for (int py = minY; py <= maxY; ++py)
    {
        for (int px = minX; px <= maxX; ++px)
        {
            float sx = px + 0.5f, sy = py + 0.5f;
            bool outer = insideRoundedRect(sx, sy, x, y, w, h, r);
            if (!outer)
                continue;
            bool inner = insideRoundedRect(sx, sy, x + t, y + t, w - 2 * t, h - 2 * t, std::max(0.0f, r - t));
            if (!inner)
                plotBlend(fb, px, py, c, mode, clip);
        }
    }
}

// ============================================================
// 多边形
// 用途：任意凸/凹形状填充、自定义碰撞区域、矢量图形、不规则遮罩
// ============================================================

// 填充多边形：扫描线 + 奇偶规则（even-odd fill rule）。
// 对每条水平扫描线求与所有边的交点 → 排序 → 成对填充区间。
// 支持凹多边形（凸多边形每行只有 1 对交点，凹的有多对）。
// 半开区间 [a.y, b.y) 避免顶点被两条边重复计数。
template <class FB>
void fillPolygon(FB& fb, const std::vector<Vec2f>& pts, Color c,
                        BlendMode mode = BlendMode::Opaque,
                        const ClipRect* clip = nullptr)
{
    int n = (int)pts.size();
    if (n < 3)
        return;
    // 顶点是非可信浮点：`(int)std::floor(NaN)` 与超出 int 范围的大坐标都是
    // 未定义行为，且 `n < 3` 这类判断对 NaN 恒为假。fillCircle /
    // fillRoundedRect 都做了同样的前置校验，这里补齐以统一口径。
    for (const auto& p : pts)
        if (!allFinite({p.x, p.y}))
            return;
    float minyf = pts[0].y, maxyf = pts[0].y;
    for (const auto& p : pts)
    {
        minyf = std::min(minyf, p.y);
        maxyf = std::max(maxyf, p.y);
    }
    // 扫描线只可能落在屏幕行内：先把行范围夹到 [-1, height] 再做 float→int。
    // 钳到“屏幕范围”而不只是“安全整数范围”：后者虽然能消除 UB，却会把
    // 屏幕外很远的顶点钳成 1e7，产生千万次注定被 plotBlend 丢弃的无效循环
    // （实测让一个 64×64 的小场景跑了 1.2 s）。
    const float rowHi = float(fb.height());
    minyf = std::clamp(minyf, -1.0f, rowHi);
    maxyf = std::clamp(maxyf, -1.0f, rowHi);
    int minY = std::max(0, (int)std::floor(minyf));
    int maxY = std::min(fb.height() - 1, (int)std::ceil(maxyf));
    std::vector<float> xs;
    for (int y = minY; y <= maxY; ++y)
    {
        float py = y + 0.5f;
        xs.clear();
        for (int i = 0; i < n; ++i)
        {
            const Vec2f& a = pts[i];
            const Vec2f& b = pts[(i + 1) % n];
            // 边跨越扫描线（半开区间避免顶点重复计数）
            if ((a.y <= py && b.y > py) || (b.y <= py && a.y > py))
            {
                float t = (py - a.y) / (b.y - a.y);
                xs.push_back(a.x + t * (b.x - a.x));
            }
        }
        std::sort(xs.begin(), xs.end());
        // 成对填充：第 0-1 个交点之间、第 2-3 个之间……
        for (size_t k = 0; k + 1 < xs.size(); k += 2)
        {
            // 交点 x 同样可能极大（顶点在屏幕外很远），夹到屏幕列范围后
            // 由 plotBlend 的边界判断丢弃屏幕外部分。
            const float colHi = float(fb.width());
            const float x0 = std::clamp(xs[k] - 0.5f, -1.0f, colHi);
            const float x1 = std::clamp(xs[k + 1] - 0.5f, -1.0f, colHi);
            int xStart = (int)std::ceil(x0);
            int xEnd = (int)std::ceil(x1);
            for (int x = xStart; x < xEnd; ++x)
                plotBlend(fb, x, y, c, mode, clip);
        }
    }
}

// 多边形描边：首尾相连画折线。closed=false 时不连回起点（开放路径）。
template <class FB>
void drawPolygon(FB& fb, const std::vector<Vec2f>& pts, Color c,
                        bool closed = true, int thickness = 1,
                        BlendMode mode = BlendMode::Opaque,
                        const ClipRect* clip = nullptr)
{
    int n = (int)pts.size();
    if (n < 2)
        return;
    int segs = closed ? n : n - 1;
    for (int i = 0; i < segs; ++i)
    {
        const Vec2f& a = pts[i];
        const Vec2f& b = pts[(i + 1) % n];
        drawThickLine(fb, a.x, a.y, b.x, b.y, float(thickness), c, true, mode, clip);
    }
}

// 粗线：线段沿法线两侧各偏移 thickness/2 扩成矩形，末端加圆帽。
// 比"画多遍 1px 线偏移"更干净，不会留缝隙。
// 用途：粗描边、连接线、矢量画笔、加粗的多边形轮廓。
template <class FB>
void drawThickLine(FB& fb, float x0, float y0, float x1, float y1,
                          float thickness, Color c, bool roundCaps,
                          BlendMode mode, const ClipRect* clip)
{
    Vec2f a{x0, y0}, b{x1, y1};
    Vec2f d = b - a;
    float len = d.length();
    float h = thickness * 0.5f;
    if (len <= 1e-6f)
    {
        // 零长度线段 → 只画一个圆点
        if (roundCaps)
            fillCircle(fb, x0, y0, h, c, mode, clip);
        return;
    }
    Vec2f n{-d.y / len, d.x / len}; // 单位法线（垂直于线段方向）
    std::vector<Vec2f> quad = {a + n * h, b + n * h, b - n * h, a - n * h};
    fillPolygon(fb, quad, c, mode, clip);
    if (roundCaps)
    {
        fillCircle(fb, x0, y0, h, c, mode, clip); // 起点圆帽
        fillCircle(fb, x1, y1, h, c, mode, clip); // 终点圆帽
    }
}

// ── 点 ────────────────────────────────────────────────────
// size=1 画单像素，size>1 画 size×size 方块。
// 用途：粒子、星空、散点图、调试标记。
template <class FB>
void drawPoint(FB& fb, int x, int y, Color c, int size = 1,
                      BlendMode mode = BlendMode::Opaque,
                      const ClipRect* clip = nullptr)
{
    if (size <= 1)
        plotBlend(fb, x, y, c, mode, clip);
    else
        fillRect(fb, x - size / 2, y - size / 2, size, size, c, mode, clip);
}

// 线框三角形：三条边复用 drawLine。属于 2D 绘制能力（需要扫线算法），
// 因此放在这里而不是光栅化模块——基础光栅化不应为了画线框而依赖整个 2D 模块。
template <class FB>
void wireTriangle(FB &fb, Vec2f v0, Vec2f v1, Vec2f v2, Color color)
{
    drawLine(fb, (int)v0.x, (int)v0.y, (int)v1.x, (int)v1.y, color);
    drawLine(fb, (int)v1.x, (int)v1.y, (int)v2.x, (int)v2.y, color);
    drawLine(fb, (int)v2.x, (int)v2.y, (int)v0.x, (int)v0.y, color);
}

} // namespace sr::draw2d
