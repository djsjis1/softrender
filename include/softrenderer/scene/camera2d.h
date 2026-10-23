#pragma once
#include <algorithm>
#include <cmath>

#include "softrenderer/math/mat3.h"
#include "softrenderer/math/rect.h"
#include "softrenderer/math/vec.h"

// ============================================================
// Camera2D：2D 正交相机，与 3D Camera 对称。
//
// 用“中心点 + 缩放 + 旋转”描述一块世界如何落到设备像素上：
//   position：屏幕中心对应的世界坐标（相机看向哪里）
//   zoom    ：每世界单位占多少设备像素（>1 放大，<1 缩小）
//   rotation：世界在屏幕上的旋转角（弧度，逆时针）
//   yDown   ：为 true 时翻转 y（适配“左上角原点”的 2D/UI 习惯）
//
// 分解与 3D 一致：world --view--> 相机空间 --proj--> 设备像素。
// viewProj = proj * view，可直接把世界坐标送到 Framebuffer。
// 逆变换 screenToWorld 用于鼠标拾取 / 命中测试。
// ============================================================
namespace sr
{

class Camera2D
{
public:
    Vec2f position{0.0f, 0.0f}; // 屏幕中心对应的世界坐标
    float zoom = 1.0f;          // 像素 / 世界单位
    float rotation = 0.0f;      // 弧度，逆时针
    int viewW = 1, viewH = 1;   // 设备分辨率（像素）
    bool yDown = false;         // true 时 y 向下（UI 习惯）

    Camera2D() = default;
    Camera2D(int w, int h) : viewW(w), viewH(h) {}

    // world → 相机空间：先平移到中心，再反向旋转
    Mat3 viewMatrix() const
    {
        return Mat3::rotate(rotation) * Mat3::translate(-position.x, -position.y);
    }

    // 相机空间 → 设备像素：缩放（含可选 y 翻转）后把原点挪到屏幕中心
    Mat3 projMatrix() const
    {
        return Mat3::translate(viewW * 0.5f, viewH * 0.5f) *
               Mat3::scale(zoom, yDown ? -zoom : zoom);
    }

    Mat3 viewProj() const { return projMatrix() * viewMatrix(); }

    Vec2f worldToScreen(const Vec2f &p) const { return viewProj().transformPoint(p); }
    Vec2f screenToWorld(const Vec2f &p) const { return viewProj().inversed().transformPoint(p); }

    // 当前可见的世界范围（轴对齐包围盒）：把设备四角逆变换回世界再取包围盒。
    // 用于 2D 剔除（只画看得见的东西）与“缩放到适配”。
    Rectf visibleWorldRect() const
    {
        Mat3 inv = viewProj().inversed();
        Vec2f c0 = inv.transformPoint({0.0f, 0.0f});
        Vec2f c1 = inv.transformPoint({float(viewW), 0.0f});
        Vec2f c2 = inv.transformPoint({float(viewW), float(viewH)});
        Vec2f c3 = inv.transformPoint({0.0f, float(viewH)});
        float minx = std::min(std::min(c0.x, c1.x), std::min(c2.x, c3.x));
        float maxx = std::max(std::max(c0.x, c1.x), std::max(c2.x, c3.x));
        float miny = std::min(std::min(c0.y, c1.y), std::min(c2.y, c3.y));
        float maxy = std::max(std::max(c0.y, c1.y), std::max(c2.y, c3.y));
        return Rectf(minx, miny, maxx - minx, maxy - miny);
    }

    // 调整 zoom 使给定世界矩形恰好填满视口（等比，取较小的缩放以完整容纳）
    void zoomToFit(const Rectf &worldBounds)
    {
        if (worldBounds.w <= 0.0f || worldBounds.h <= 0.0f)
            return;
        float sx = float(viewW) / worldBounds.w;
        float sy = float(viewH) / worldBounds.h;
        zoom = std::min(sx, sy);
        position = worldBounds.center();
    }

    // ---- 交互 ----
    void pan(const Vec2f &worldDelta) { position = position - worldDelta; }
    void zoomBy(float factor) { zoom = std::max(1e-4f, zoom * factor); }
};

}  // namespace sr
