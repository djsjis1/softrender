#pragma once
#include <algorithm>
#include "softrenderer/math/vec.h"

// ============================================================
// Rect2：2D 矩形（轴对齐包围盒 AABB），用 (x, y, w, h) 表示。
//
// 坐标约定与 Framebuffer 一致：y 向上，(x,y) 是左下角。
//   left   = x        right = x + w
//   bottom = y        top   = y + h
//
// 2D 渲染里无处不在：精灵/图层的包围盒、UI 布局与命中测试、
// scissor 裁剪区、纹理图集的子矩形、AABB 碰撞检测。
// 模板化以同时提供整数版 Recti（像素/裁剪）与浮点版 Rectf（布局/世界）。
// ============================================================
namespace sr
{

template <typename T>
struct Rect2
{
    T x = 0, y = 0, w = 0, h = 0;

    Rect2() = default;
    Rect2(T x_, T y_, T w_, T h_) : x(x_), y(y_), w(w_), h(h_) {}
    Rect2(const Vec2<T>& pos, const Vec2<T>& size) : x(pos.x), y(pos.y), w(size.x), h(size.y) {}

    T left() const { return x; }
    T right() const { return x + w; }
    T bottom() const { return y; }
    T top() const { return y + h; }

    Vec2<T> min() const { return {x, y}; }
    Vec2<T> max() const { return {x + w, y + h}; }
    Vec2<T> size() const { return {w, h}; }
    Vec2<T> position() const { return {x, y}; }
    Vec2<T> center() const { return {x + w / T(2), y + h / T(2)}; }

    T area() const { return w * h; }
    bool isEmpty() const { return w <= T(0) || h <= T(0); }

    // 由对角两点构造（自动规整，保证 w/h 非负）
    static Rect2 fromMinMax(T x0, T y0, T x1, T y1)
    {
        T lox = std::min(x0, x1), hix = std::max(x0, x1);
        T loy = std::min(y0, y1), hiy = std::max(y0, y1);
        return Rect2(lox, loy, hix - lox, hiy - loy);
    }
    static Rect2 fromCenterSize(const Vec2<T>& c, const Vec2<T>& s)
    {
        return Rect2(c.x - s.x / T(2), c.y - s.y / T(2), s.x, s.y);
    }

    bool contains(T px, T py) const
    {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
    bool contains(const Vec2<T>& p) const { return contains(p.x, p.y); }
    bool contains(const Rect2& o) const
    {
        return o.x >= x && o.y >= y && o.right() <= right() && o.top() <= top();
    }

    // 相交判定：两轴投影均重叠才相交（分离轴思想）。
    // 空矩形（w 或 h ≤ 0）一律不相交：否则 (5,5,0,0) 这类零面积矩形
    // 会被判为与包含它的矩形相交，命中测试与裁剪会凭空产生结果。
    bool intersects(const Rect2& o) const
    {
        if (isEmpty() || o.isEmpty())
            return false;
        return x < o.right() && o.x < right() && y < o.top() && o.y < top();
    }

    // 交集（可能为空矩形）
    Rect2 intersection(const Rect2& o) const
    {
        T lox = std::max(x, o.x), loy = std::max(y, o.y);
        T hix = std::min(right(), o.right()), hiy = std::min(top(), o.top());
        if (hix <= lox || hiy <= loy)
            return Rect2(0, 0, 0, 0);
        return Rect2(lox, loy, hix - lox, hiy - loy);
    }

    // 并集（能同时包含两者的最小矩形）
    Rect2 merged(const Rect2& o) const
    {
        if (isEmpty())
            return o;
        if (o.isEmpty())
            return *this;
        T lox = std::min(x, o.x), loy = std::min(y, o.y);
        T hix = std::max(right(), o.right()), hiy = std::max(top(), o.top());
        return Rect2(lox, loy, hix - lox, hiy - loy);
    }

    Rect2 offset(T dx, T dy) const { return Rect2(x + dx, y + dy, w, h); }
    Rect2 offset(const Vec2<T>& d) const { return Rect2(x + d.x, y + d.y, w, h); }

    // 四边各向外扩 margin（margin 为负则内缩），用于选区外扩/内缩
    Rect2 expanded(T margin) const
    {
        return Rect2(x - margin, y - margin, w + margin * T(2), h + margin * T(2));
    }

    bool operator==(const Rect2& o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
    bool operator!=(const Rect2& o) const { return !(*this == o); }
};

using Rectf = Rect2<float>;
using Recti = Rect2<int>;

}  // namespace sr
