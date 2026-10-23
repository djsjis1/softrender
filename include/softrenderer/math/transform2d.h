#pragma once
#include "softrenderer/math/mat3.h"
#include "softrenderer/math/rect.h"

// ============================================================
// 2D 投影（返回 Mat3）——与 transform.h 的 3D 投影对称，用于 2D 渲染。
//
// 单独成头的原因：纯 3D 路径（renderer.h / 各 3D 示例）只需 Mat4 投影，
// 不必引入 Mat3/Rect；把 2D 投影拆到这里可保持 transform.h 的依赖面最小。
// 需要 2D 正交/视口投影时包含本头即可（仍在 namespace transform 下）。
//
// 2D 没有透视除法，一切是仿射：世界/NDC 坐标线性映射到设备像素。
// 约定设备像素 y 向上、(0,0) 在左下角（与 Framebuffer 一致）。
// ============================================================
namespace sr::transform
{

    // 2D 视口变换：NDC [-1,1]² → 屏幕像素 [0,w)×[0,h)
    // 是 Mat4 viewport 的 2D 类比（去掉 z 那一行）。
    inline Mat3 viewport2D(int w, int h)
    {
        Mat3 v = Mat3::identity();
        v.m[0][0] = w * 0.5f;
        v.m[0][2] = w * 0.5f;
        v.m[1][1] = h * 0.5f;
        v.m[1][2] = h * 0.5f;
        return v;
    }

    // 2D 正交投影：世界矩形 [l,r]×[b,t] → 设备像素 [0,w)×[0,h)。
    // 每个轴各做一次“缩放 + 平移”，把世界范围线性铺满设备。
    // 这是“分辨率无关 2D 相机”的核心：无论窗口多大，世界坐标不变。
    inline Mat3 ortho2D(float l, float r, float b, float t, int w, int h)
    {
        float sx = (r != l) ? float(w) / (r - l) : 0.0f;
        float sy = (t != b) ? float(h) / (t - b) : 0.0f;
        Mat3 o = Mat3::identity();
        o.m[0][0] = sx;
        o.m[1][1] = sy;
        o.m[0][2] = -l * sx; // 世界 l → 设备 0
        o.m[1][2] = -b * sy; // 世界 b → 设备 0
        return o;
    }

    // 便捷重载：用一个 Rectf 描述世界可见范围
    inline Mat3 ortho2D(const Rectf& world, int w, int h)
    {
        return ortho2D(world.left(), world.right(), world.bottom(), world.top(), w, h);
    }

} // namespace sr::transform
