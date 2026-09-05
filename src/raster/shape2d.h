#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

#include "core/blend.h"
#include "core/framebuffer.h"
#include "math/rect.h"
#include "math/vec.h"

// ============================================================
// 2D 图元库（shape2d）—— 与 3D Renderer 对等的“立即模式”2D 绘制底座
//
// 【为什么单独一层】3D 管线（renderer.h）围绕“三角形 + 深度 + 着色器”
// 组织，适合渲染有透视、有光照的物体；而 2D（HUD、精灵、图标、UI、
// 矢量图形）关心的是：屏幕空间直接定位、alpha 混合叠加、可选裁剪区。
// 二者共用同一个 Framebuffer，但绘制逻辑差别很大，所以拆出本层。
//
// 【坐标约定】与 Framebuffer / line.h / triangle.h 完全一致：
//   x 向右、y 向上，(0,0) 在左下角。整数坐标指像素格；
//   填充类图元以“像素中心 (x+0.5, y+0.5)”为采样点，边缘更稳定。
//
// 【两个可选参数贯穿所有图元】
//   BlendMode mode：默认 Opaque（直接覆盖，等价旧行为）；
//                   传 AlphaBlend / Additive 等即可半透明叠加（见 blend.h）。
//   const ClipRect* clip：默认 nullptr（只裁剪到帧缓冲边界）；
//                   传入后额外裁剪到一个矩形区，UI 局部刷新/滚动很有用。
// ============================================================

namespace sr2d
{

// 裁剪矩形：半开区间 [x0,x1) × [y0,y1)，与常见 scissor 语义一致
struct ClipRect
{
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;

    ClipRect() = default;
    ClipRect(int x0_, int y0_, int x1_, int y1_) : x0(x0_), y0(y0_), x1(x1_), y1(y1_) {}

    bool contains(int x, int y) const { return x >= x0 && x < x1 && y >= y0 && y < y1; }
    bool empty() const { return x0 >= x1 || y0 >= y1; }
};

// Recti（math 层的 x,y,w,h 矩形）→ ClipRect（半开裁剪区）的桥接。
// 依赖方向保持 raster → math，math 层不反向依赖 raster。
inline ClipRect toClipRect(const Recti& r) { return ClipRect(r.x, r.y, r.right(), r.top()); }

// ------------------------------------------------------------
// 公共底座：带边界裁剪 + 混合 + 可选 scissor 的单像素写入。
// 所有 2D 图元最终都落到这里，保证混合/裁剪行为统一。
// ------------------------------------------------------------
inline void plotBlend(Framebuffer& fb, int x, int y, Color c,
                      BlendMode mode = BlendMode::Opaque,
                      const ClipRect* clip = nullptr)
{
    if (x < 0 || x >= fb.width() || y < 0 || y >= fb.height())
        return;
    if (clip && !clip->contains(x, y))
        return;
    // 仅“纯覆盖语义”可短路：Opaque 恒覆盖；AlphaBlend/Premultiplied 在 src 不透明时
    // 等价于覆盖。Additive/Multiply 即使 src 不透明仍需与 dst 运算，不能短路。
    if (mode == BlendMode::Opaque ||
        (c.a == 255 && (mode == BlendMode::AlphaBlend || mode == BlendMode::Premultiplied)))
    {
        fb.setPixel(x, y, c);
        return;
    }
    fb.setPixel(x, y, blendPixels(c, fb.getPixel(x, y), mode));
}

// ------------------------------------------------------------
// 混合版画线：Bresenham 整数误差累积（同 line.h 的 drawLine），
// 但每个像素走 plotBlend，从而支持 alpha 混合与 scissor 裁剪。
// line.h 的 drawLine 只能不透明覆盖，这里补齐“可混合”的线。
// ------------------------------------------------------------
inline void lineBlend(Framebuffer& fb, int x0, int y0, int x1, int y1, Color c,
                      BlendMode mode = BlendMode::Opaque,
                      const ClipRect* clip = nullptr)
{
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
    }
    const int dx = x1 - x0;
    const int dy = std::abs(y1 - y0);
    const int ystep = (y1 > y0) ? 1 : -1;
    int error2 = 0;
    int y = y0;
    for (int x = x0; x <= x1; ++x)
    {
        if (steep)
            plotBlend(fb, y, x, c, mode, clip);
        else
            plotBlend(fb, x, y, c, mode, clip);
        error2 += 2 * dy;
        if (error2 > dx)
        {
            y += ystep;
            error2 -= 2 * dx;
        }
    }
}

// 前向声明：drawEllipse / drawPolygon 的加粗描边需要 drawThickLine，
// 而其定义在文件后部（依赖 fillPolygon），故在此先给出原型。
inline void drawThickLine(Framebuffer& fb, float x0, float y0, float x1, float y1,
                          float thickness, Color c, bool roundCaps,
                          BlendMode mode, const ClipRect* clip);

// ============================================================
// 矩形
// ============================================================

// 填充矩形：(x,y) 是左下角，w/h 是宽高（像素数）。逐行直写，最快的实心块。
inline void fillRect(Framebuffer& fb, int x, int y, int w, int h, Color c,
                     BlendMode mode = BlendMode::Opaque,
                     const ClipRect* clip = nullptr)
{
    if (w <= 0 || h <= 0)
        return;
    // 先把范围夹到帧缓冲内，避免逐像素都做越界判断
    int x0 = std::max(0, x), y0 = std::max(0, y);
    int x1 = std::min(fb.width(), x + w), y1 = std::min(fb.height(), y + h);
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

// 矩形描边：只画四条边，thickness 为线宽（向内扩展）
inline void drawRect(Framebuffer& fb, int x, int y, int w, int h, Color c, int thickness = 1,
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

// 矩形线性渐变填充：horizontal=false 时沿 y（下→上 c0→c1），true 时沿 x（左→右）。
// 天空、进度条、光泽面板都靠它。t 用像素中心归一化，避免首尾色带。
inline void fillRectGradient(Framebuffer& fb, int x, int y, int w, int h,
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
// ============================================================

// 填充圆：包围盒内逐像素做勾股距离测试 (dx²+dy² ≤ r²)。
// 以像素中心采样，圆心 (cx,cy) 可为浮点以支持平滑定位。
inline void fillCircle(Framebuffer& fb, float cx, float cy, float r, Color c,
                       BlendMode mode = BlendMode::Opaque,
                       const ClipRect* clip = nullptr)
{
    if (r <= 0.0f)
        return;
    // 先把包围盒夹到帧缓冲与裁剪区，避免对屏外像素做无谓的距离测试
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
        float dy = py + 0.5f - cy;
        for (int px = minX; px <= maxX; ++px)
        {
            float dx = px + 0.5f - cx;
            if (dx * dx + dy * dy <= r2)
                plotBlend(fb, px, py, c, mode, clip);
        }
    }
}

// 圆描边：中点圆算法（Bresenham 圆），只算 1/8 弧再八对称镜像，纯整数增量。
// thickness>1 时 concentric 画多圈近似加粗。
inline void drawCircle(Framebuffer& fb, int cx, int cy, int r, Color c, int thickness = 1,
                       BlendMode mode = BlendMode::Opaque,
                       const ClipRect* clip = nullptr)
{
    for (int t = 0; t < thickness; ++t)
    {
        int rr = r - t;
        if (rr < 0)
            break;
        int x = rr, y = 0;
        int err = 1 - rr; // 决策变量
        while (x >= y)
        {
            // 八对称：一次算出 8 个点
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
                err += 2 * y + 1; // 仅 y 方向前进
            else
            {
                --x;
                err += 2 * (y - x) + 1; // x 方向也收缩
            }
        }
    }
}

// 填充椭圆：包围盒内做归一化距离测试 (dx/rx)²+(dy/ry)² ≤ 1
inline void fillEllipse(Framebuffer& fb, float cx, float cy, float rx, float ry, Color c,
                        BlendMode mode = BlendMode::Opaque,
                        const ClipRect* clip = nullptr)
{
    if (rx <= 0.0f || ry <= 0.0f)
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
    for (int py = minY; py <= maxY; ++py)
    {
        float ny = (py + 0.5f - cy) / ry;
        for (int px = minX; px <= maxX; ++px)
        {
            float nx = (px + 0.5f - cx) / rx;
            if (nx * nx + ny * ny <= 1.0f)
                plotBlend(fb, px, py, c, mode, clip);
        }
    }
}

// 椭圆描边：参数方程步进 + 混合线连接相邻点。步数随半径增大而增多，避免折线感。
inline void drawEllipse(Framebuffer& fb, float cx, float cy, float rx, float ry, Color c,
                        int thickness = 1,
                        BlendMode mode = BlendMode::Opaque,
                        const ClipRect* clip = nullptr)
{
    if (rx <= 0.0f || ry <= 0.0f)
        return;
    const float PI = 3.14159265358979f;
    int steps = std::max(16, (int)(2.0f * PI * std::max(rx, ry)));
    int prevX = (int)std::round(cx + rx), prevY = (int)std::round(cy);
    for (int i = 1; i <= steps; ++i)
    {
        float a = 2.0f * PI * float(i) / float(steps);
        float px = cx + rx * std::cos(a);
        float py = cy + ry * std::sin(a);
        // 沿法线扩展的粗线（圆帽衔接），轮廓厚度均匀
        drawThickLine(fb, float(prevX), float(prevY), px, py, float(thickness), c, true, mode, clip);
        prevX = (int)std::round(px);
        prevY = (int)std::round(py);
    }
}

// ============================================================
// 圆角矩形（UI 面板、按钮的标配）
// ============================================================

// 圆角矩形内部测试：把点夹到“内矩形”[x+r, x+w-r]×[y+r, y+h-r]，
// 若到内矩形的距离 ≤ r 则在圆角矩形内。这是圆角矩形的隐式（SDF 思路）判定，
// 一份公式同时覆盖直边段与四个圆角。像素中心采样。
inline bool insideRoundedRect(float px, float py, float x, float y, float w, float h, float r)
{
    float cx = std::clamp(px, x + r, x + w - r);
    float cy = std::clamp(py, y + r, y + h - r);
    float dx = px - cx, dy = py - cy;
    return dx * dx + dy * dy <= r * r;
}

inline void fillRoundedRect(Framebuffer& fb, float x, float y, float w, float h, float r, Color c,
                            BlendMode mode = BlendMode::Opaque,
                            const ClipRect* clip = nullptr)
{
    if (w <= 0.0f || h <= 0.0f)
        return;
    r = std::max(0.0f, std::min(r, std::min(w, h) * 0.5f));
    int minX = (int)std::floor(x), maxX = (int)std::ceil(x + w);
    int minY = (int)std::floor(y), maxY = (int)std::ceil(y + h);
    for (int py = minY; py <= maxY; ++py)
        for (int px = minX; px <= maxX; ++px)
            if (insideRoundedRect(px + 0.5f, py + 0.5f, x, y, w, h, r))
                plotBlend(fb, px, py, c, mode, clip);
}

// 圆角矩形描边：外层内、内层（收缩 thickness）外 → 得到厚度均匀的边框
inline void drawRoundedRect(Framebuffer& fb, float x, float y, float w, float h, float r,
                            Color c, int thickness = 1,
                            BlendMode mode = BlendMode::Opaque,
                            const ClipRect* clip = nullptr)
{
    if (w <= 0.0f || h <= 0.0f || thickness <= 0)
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
// 多边形（任意凸/凹多边形：扫描线填充 + 折线描边）
// ============================================================

// 填充多边形：扫描线 + 奇偶规则（even-odd）。
// 对每条水平扫描线求与所有边的交点，排序后成对填充区间。
// 顶点用浮点 Vec2f，支持任意多边形（含凹多边形）。
inline void fillPolygon(Framebuffer& fb, const std::vector<Vec2f>& pts, Color c,
                        BlendMode mode = BlendMode::Opaque,
                        const ClipRect* clip = nullptr)
{
    int n = (int)pts.size();
    if (n < 3)
        return;
    float minyf = pts[0].y, maxyf = pts[0].y;
    for (const auto& p : pts)
    {
        minyf = std::min(minyf, p.y);
        maxyf = std::max(maxyf, p.y);
    }
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
        for (size_t k = 0; k + 1 < xs.size(); k += 2)
        {
            int xStart = (int)std::ceil(xs[k] - 0.5f);
            int xEnd = (int)std::ceil(xs[k + 1] - 0.5f);
            for (int x = xStart; x < xEnd; ++x)
                plotBlend(fb, x, y, c, mode, clip);
        }
    }
}

// 多边形描边：首尾相连画折线；closed=false 时不连回起点（画开放路径）
inline void drawPolygon(Framebuffer& fb, const std::vector<Vec2f>& pts, Color c,
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
        // 沿法线扩展的粗线（圆帽衔接顶点），轮廓厚度均匀
        drawThickLine(fb, a.x, a.y, b.x, b.y, float(thickness), c, true, mode, clip);
    }
}

// 粗线：把线段扩成矩形（沿法线两侧各偏移 thickness/2）再填充，末端加圆帽。
// 比“画多遍 1px 线”更干净，也不会留下缝隙。
inline void drawThickLine(Framebuffer& fb, float x0, float y0, float x1, float y1,
                          float thickness, Color c, bool roundCaps = true,
                          BlendMode mode = BlendMode::Opaque,
                          const ClipRect* clip = nullptr)
{
    Vec2f a{x0, y0}, b{x1, y1};
    Vec2f d = b - a;
    float len = d.length();
    float h = thickness * 0.5f;
    if (len <= 1e-6f)
    {
        if (roundCaps)
            fillCircle(fb, x0, y0, h, c, mode, clip);
        return;
    }
    Vec2f n{-d.y / len, d.x / len}; // 单位法线
    std::vector<Vec2f> quad = {a + n * h, b + n * h, b - n * h, a - n * h};
    fillPolygon(fb, quad, c, mode, clip);
    if (roundCaps)
    {
        fillCircle(fb, x0, y0, h, c, mode, clip);
        fillCircle(fb, x1, y1, h, c, mode, clip);
    }
}

// 单个点（放大到 size×size 的方块，size=1 即单像素）
inline void drawPoint(Framebuffer& fb, int x, int y, Color c, int size = 1,
                      BlendMode mode = BlendMode::Opaque,
                      const ClipRect* clip = nullptr)
{
    if (size <= 1)
        plotBlend(fb, x, y, c, mode, clip);
    else
        fillRect(fb, x - size / 2, y - size / 2, size, size, c, mode, clip);
}

} // namespace sr2d
