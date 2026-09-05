#pragma once
#include <cmath>
#include "math/mat4.h"
#include "math/vec.h"

// ============================================================
// Mat3：3x3 矩阵，行主序存储（m[行][列]），用于 2D 仿射变换。
//
// 【为什么 2D 也要 3x3】和 Mat4 的道理一模一样：2D 的旋转/缩放
// 用 2x2 就够，但“平移”不是线性变换，塞不进 2x2。于是把 2D 点
// (x,y) 升维成齐次坐标 (x,y,1)，平移就落到第三列，所有 2D 变换
// （平移/旋转/缩放/错切）统一成 3x3 矩阵乘法，可以预先合成一个。
//
//   [x']   [a c e] [x]        x' = a*x + c*y + e
//   [y'] = [b d f] [y]   →    y' = b*x + d*y + f
//   [1 ]   [0 0 1] [1]
//
// 约定与 Mat4 完全一致：列向量、右手系、A*B 表示“先做 B 再做 A”。
// y 轴向上，rotate 正角为逆时针（与 Mat4::rotateZ 相同）。
// ============================================================
struct Mat3
{
    float m[3][3] = {}; // 默认全零

    static Mat3 identity()
    {
        Mat3 r;
        for (int i = 0; i < 3; ++i)
            r.m[i][i] = 1.0f;
        return r;
    }

    // 矩阵乘法：A*B = 先 B 后 A（列向量约定）
    Mat3 operator*(const Mat3& o) const
    {
        Mat3 r;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                for (int k = 0; k < 3; ++k)
                    r.m[i][j] += m[i][k] * o.m[k][j];
        return r;
    }

    Mat3& operator*=(const Mat3& o)
    {
        *this = (*this) * o;
        return *this;
    }

    bool operator==(const Mat3& o) const
    {
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                if (m[i][j] != o.m[i][j])
                    return false;
        return true;
    }

    // 变换“点”（齐次 w=1）：仿射矩阵第三行恒为 (0,0,1)，w 保持 1，
    // 这里仍做除法以兼容投影视角矩阵（第三行非平凡时）。
    Vec2f transformPoint(const Vec2f& p) const
    {
        float x = m[0][0] * p.x + m[0][1] * p.y + m[0][2];
        float y = m[1][0] * p.x + m[1][1] * p.y + m[1][2];
        float w = m[2][0] * p.x + m[2][1] * p.y + m[2][2];
        if (std::abs(w) > 1e-20f && std::abs(w - 1.0f) > 1e-6f)
        {
            x /= w;
            y /= w;
        }
        return {x, y};
    }

    // 变换“方向/向量”（w=0）：平移不生效，只保留线性部分
    Vec2f transformDir(const Vec2f& d) const
    {
        return {m[0][0] * d.x + m[0][1] * d.y,
                m[1][0] * d.x + m[1][1] * d.y};
    }

    Mat3 transposed() const
    {
        Mat3 r;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                r.m[i][j] = m[j][i];
        return r;
    }

    // 行列式：2D 线性部分的“有向面积缩放因子”。
    // =0 表示退化（把平面压扁成线/点），不可逆。
    float determinant() const
    {
        return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
               m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
               m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    }

    // 求逆：伴随矩阵 / 行列式。2D 渲染里常用来把“屏幕坐标 → 世界坐标”
    // （如鼠标拾取、纹理反向映射、blit 时的逆变换采样）。
    Mat3 inversed() const
    {
        float det = determinant();
        if (std::abs(det) < 1e-12f)
            return identity(); // 退化，兜底返回单位阵
        float inv = 1.0f / det;
        Mat3 r;
        r.m[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) * inv;
        r.m[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * inv;
        r.m[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * inv;
        r.m[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) * inv;
        r.m[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * inv;
        r.m[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * inv;
        r.m[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) * inv;
        r.m[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * inv;
        r.m[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * inv;
        return r;
    }

    // X 轴基向量变换后的长度（水平方向的缩放倍数，含旋转不影响长度）
    float scaleX() const { return Vec2f{m[0][0], m[1][0]}.length(); }
    // Y 轴基向量变换后的长度（垂直方向的缩放倍数）
    float scaleY() const { return Vec2f{m[0][1], m[1][1]}.length(); }

    // ---------- 基础变换矩阵工厂 ----------

    static Mat3 translate(float tx, float ty)
    {
        Mat3 r = identity();
        r.m[0][2] = tx; // 平移量藏在第三列——齐次坐标的意义
        r.m[1][2] = ty;
        return r;
    }
    static Mat3 translate(const Vec2f& t) { return translate(t.x, t.y); }

    static Mat3 scale(float sx, float sy)
    {
        Mat3 r = identity();
        r.m[0][0] = sx;
        r.m[1][1] = sy;
        return r;
    }
    static Mat3 scale(float s) { return scale(s, s); }
    static Mat3 scale(const Vec2f& s) { return scale(s.x, s.y); }

    // 绕原点逆时针旋转 rad 弧度（y 向上时的正方向）
    static Mat3 rotate(float rad)
    {
        Mat3 r = identity();
        float c = std::cos(rad), s = std::sin(rad);
        r.m[0][0] = c;
        r.m[0][1] = -s;
        r.m[1][0] = s;
        r.m[1][1] = c;
        return r;
    }

    // 绕任意点 pivot 旋转：先平移到原点、旋转、再平移回去
    static Mat3 rotateAround(const Vec2f& pivot, float rad)
    {
        return translate(pivot) * rotate(rad) * translate(-pivot.x, -pivot.y);
    }

    // 错切（skew）：常用于 2D 斜体/伪透视效果
    static Mat3 shear(float shx, float shy)
    {
        Mat3 r = identity();
        r.m[0][1] = shx;
        r.m[1][0] = shy;
        return r;
    }

    // 嵌入 4x4：把 2D 仿射放进 XY 平面（z 不变），方便与 3D 管线混用，
    // 例如把一张 2D 图层当作 z=0 的四边形交给 3D Renderer 绘制。
    Mat4 toMat4() const
    {
        Mat4 r = Mat4::identity();
        r.m[0][0] = m[0][0];
        r.m[0][1] = m[0][1];
        r.m[0][3] = m[0][2]; // Mat3 第三列(平移) → Mat4 第四列
        r.m[1][0] = m[1][0];
        r.m[1][1] = m[1][1];
        r.m[1][3] = m[1][2];
        // 第 3 行保持 (0,0,1,0)：z 直通；第 4 行 (0,0,0,1)
        return r;
    }
};

// ============================================================
// 2D 专用向量小工具（与 Mat3 配套的常用几何运算）
// ============================================================

// 逆时针垂直向量：(x,y) → (-y,x)。求线段法线、多边形边法向常用。
inline Vec2f perp(const Vec2f& v) { return {-v.y, v.x}; }

// 把 2D 向量绕原点旋转 rad 弧度（等价 Mat3::rotate 作用于向量）
inline Vec2f rotate2D(const Vec2f& v, float rad)
{
    float c = std::cos(rad), s = std::sin(rad);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}

// 2D 向量夹角（弧度，[-π, π]），符号由叉积决定：正=从 a 到 b 逆时针
inline float angleBetween(const Vec2f& a, const Vec2f& b)
{
    return std::atan2(cross2(a, b), dot(a, b));
}
