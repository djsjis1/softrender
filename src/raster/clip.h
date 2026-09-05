#pragma once
#include "shader/shader.h"

// ============================================================
// 齐次裁剪空间的近平面裁剪（Sutherland-Hodgman 单平面版）
//
// 【为什么必须裁剪】透视除法要 ÷w。当顶点在相机后方或正好在
// 相机平面上时，w<=0，除法会得到无穷大 / 翻转坐标 / 除零崩溃。
// 前几阶段用"任一顶点 w<=0 就丢弃整个三角形"来兜底，代价是
// 靠近相机的三角形会整块消失（地面出现空洞）。
//
// 正解：在透视除法【之前】的裁剪空间里，把三角形沿近平面切开，
// 只保留可见部分。切开后三角形可能变成四边形，用三角扇重新拆分。
//
// 【近平面方程】OpenGL 裁剪空间中，近平面是 z = -w，即：
//   可见（内侧）  <=>  z + w >= 0
// 我们用有符号距离 d = z + w 判断顶点在平面内外，交点按 d 线性插值。
//
// 【为何这里用线性插值而非透视校正】裁剪发生在透视除法之前，
// 裁剪空间坐标与顶点属性此时都还是线性的，直接线性插值即正确；
// 透视校正是在除法之后的屏幕空间才需要（见阶段 5）。
// ============================================================

// 裁剪用顶点：裁剪空间位置 + 待插值的 Varyings
struct ClipVertex
{
    Vec4f pos;    // 裁剪空间（clip space）齐次坐标
    Varyings var; // 顶点属性
};

// 两个裁剪顶点间线性插值（位置和所有属性）
inline ClipVertex lerpClipVertex(const ClipVertex &a, const ClipVertex &b, float t)
{
    ClipVertex r;
    r.pos = a.pos + (b.pos - a.pos) * t;
    r.var.worldPos = a.var.worldPos + (b.var.worldPos - a.var.worldPos) * t;
    r.var.normal = a.var.normal + (b.var.normal - a.var.normal) * t;
    r.var.uv = a.var.uv + (b.var.uv - a.var.uv) * t;
    r.var.color = a.var.color + (b.var.color - a.var.color) * t;
    r.var.tangent = a.var.tangent + (b.var.tangent - a.var.tangent) * t;
    return r;
}

// 把三角形 in[3] 沿近平面裁剪，结果写入 out（最多 4 个顶点）。
// 返回输出顶点数：0（全部在外，丢弃）或 3~4（保留，需三角扇拆分）。
inline int clipTriangleNearPlane(const ClipVertex in[3], ClipVertex out[4])
{
    // 近平面内侧判据：d = z + w >= 0
    auto dist = [](const ClipVertex &v)
    { return v.pos.z + v.pos.w; };

    int count = 0;
    for (int i = 0; i < 3; ++i)
    {
        const ClipVertex &cur = in[i];
        const ClipVertex &nxt = in[(i + 1) % 3];
        float dCur = dist(cur);
        float dNxt = dist(nxt);
        bool inCur = dCur >= 0.0f;
        bool inNxt = dNxt >= 0.0f;

        if (inCur)
            out[count++] = cur;
        if (inCur != inNxt)
        {
            float t = dCur / (dCur - dNxt);
            out[count++] = lerpClipVertex(cur, nxt, t);
        }
    }
    return count;
}

// ============================================================
// 完整 6 面视锥裁剪
//
// 裁剪空间的 6 个平面方程（内侧判据 >= 0）：
//   Near:   w + z    Far:    w - z
//   Left:   w + x    Right:  w - x
//   Bottom: w + y    Top:    w - y
//
// 用 Sutherland-Hodgman 逐平面迭代：每裁一个平面，多边形
// 顶点数可能 +1，三角形最多变 3+6=9 顶点（极端情况）。
// 实际中很少超过 6~7 个。
// ============================================================

static constexpr int kMaxClipVerts = 12; // 安全上界

// 6 个平面的距离函数
enum ClipPlane
{
    kNear = 0,
    kFar,
    kLeft,
    kRight,
    kBottom,
    kTop,
    kClipPlaneCount
};

inline float clipPlaneDist(const ClipVertex &v, ClipPlane plane)
{
    switch (plane)
    {
    case kNear:
        return v.pos.w + v.pos.z;
    case kFar:
        return v.pos.w - v.pos.z;
    case kLeft:
        return v.pos.w + v.pos.x;
    case kRight:
        return v.pos.w - v.pos.x;
    case kBottom:
        return v.pos.w + v.pos.y;
    case kTop:
        return v.pos.w - v.pos.y;
    }
    return 0;
}

// 对多边形 in[0..n-1] 沿一个平面裁剪，结果写入 out，返回新顶点数
inline int clipPolygonOnePlane(const ClipVertex *in, int n, ClipVertex *out, ClipPlane plane)
{
    if (n == 0)
        return 0;
    int count = 0;
    for (int i = 0; i < n; ++i)
    {
        const ClipVertex &cur = in[i];
        const ClipVertex &nxt = in[(i + 1) % n];
        float dCur = clipPlaneDist(cur, plane);
        float dNxt = clipPlaneDist(nxt, plane);
        bool inCur = dCur >= 0.0f;
        bool inNxt = dNxt >= 0.0f;
        if (inCur && count < kMaxClipVerts)
            out[count++] = cur;
        if (inCur != inNxt && count < kMaxClipVerts)
        {
            float t = dCur / (dCur - dNxt);
            out[count++] = lerpClipVertex(cur, nxt, t);
        }
    }
    return count;
}

// 完整 6 面裁剪：输入三角形 3 顶点，输出凸多边形 n 顶点（0 = 全裁掉）
inline int clipTriangleFullFrustum(const ClipVertex in[3], ClipVertex out[kMaxClipVerts])
{
    ClipVertex bufA[kMaxClipVerts], bufB[kMaxClipVerts];
    // 初始拷贝
    for (int i = 0; i < 3; ++i)
        bufA[i] = in[i];
    int n = 3;
    ClipVertex *src = bufA;
    ClipVertex *dst = bufB;
    for (int p = 0; p < kClipPlaneCount; ++p)
    {
        n = clipPolygonOnePlane(src, n, dst, ClipPlane(p));
        if (n == 0)
            return 0;
        std::swap(src, dst);
    }
    // 拷贝到 out
    for (int i = 0; i < n; ++i)
        out[i] = src[i];
    return n;
}
