#pragma once
#include <cmath>
#include "softrenderer/math/vec.h"

// ============================================================
// Mat4：4x4 矩阵，行主序存储（m[行][列]），右手坐标系。
//
// 【为什么是 4x4 而不是 3x3】
// 3x3 矩阵能表达旋转/缩放，但表达不了"平移"（平移不是线性变换）。
// 引入齐次坐标：点 (x,y,z) 升维成 (x,y,z,1)，
// 平移就变成了第 4 列的乘法——所有变换统一成矩阵乘法，
// 多个变换可以预先乘成一个矩阵，一次乘法搞定全部。
// 方向向量 w=0：乘矩阵时第 4 列（平移）自动失效，正合语义。
// ============================================================
namespace sr
{

struct Mat4
{
    float m[4][4] = {}; // 默认全零

    // 单位矩阵：乘任何东西都不变，变换的"起点"
    static Mat4 identity()
    {
        Mat4 r;
        for (int i = 0; i < 4; ++i)
            r.m[i][i] = 1.0f;
        return r;
    }

    // 矩阵乘法：注意不满足交换律！A*B 表示"先做 B 再做 A"
    // （列向量约定：p' = A * B * p，靠近 p 的先作用）
    Mat4 operator*(const Mat4 &o) const
    {
        Mat4 r;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                for (int k = 0; k < 4; ++k)
                    r.m[i][j] += m[i][k] * o.m[k][j];
        return r;
    }

    // 矩阵 * 齐次向量
    Vec4f operator*(const Vec4f &v) const
    {
        Vec4f r;
        for (int i = 0; i < 4; ++i)
            r[i] = m[i][0] * v.x + m[i][1] * v.y + m[i][2] * v.z + m[i][3] * v.w;
        return r;
    }

    // 变换"点"（w=1）：透视除法前的便捷函数，只适用于仿射矩阵
    Vec3f transformPoint(const Vec3f &p) const
    {
        Vec4f r = (*this) * Vec4f(p, 1.0f);
        return r.xyz();
    }

    // 变换"方向"（w=0）：平移不生效
    Vec3f transformDir(const Vec3f &d) const
    {
        Vec4f r = (*this) * Vec4f(d, 0.0f);
        return r.xyz();
    }

    // 转置：行列互换。正交矩阵（纯旋转）的逆 = 转置，非常便宜
    Mat4 transposed() const
    {
        Mat4 r;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                r.m[i][j] = m[j][i];
        return r;
    }

    // 通用求逆（高斯-约当消元）。
    // 用途：法线变换需要"逆转置矩阵"（阶段 6 光照时详述）
    Mat4 inversed() const
    {
        // 增广矩阵 [this | I]，行变换把左边化成 I，右边就是逆
        float a[4][8] = {};
        for (int i = 0; i < 4; ++i)
        {
            for (int j = 0; j < 4; ++j)
                a[i][j] = m[i][j];
            a[i][i + 4] = 1.0f;
        }
        for (int col = 0; col < 4; ++col)
        {
            // 选主元（绝对值最大的行），提升数值稳定性
            int pivot = col;
            for (int r = col + 1; r < 4; ++r)
                if (std::abs(a[r][col]) > std::abs(a[pivot][col]))
                    pivot = r;
            if (std::abs(a[pivot][col]) < 1e-10f)
                return identity(); // 不可逆，退化处理
            if (pivot != col)
                for (int j = 0; j < 8; ++j)
                    std::swap(a[col][j], a[pivot][j]);
            // 归一化主元行
            float inv = 1.0f / a[col][col];
            for (int j = 0; j < 8; ++j)
                a[col][j] *= inv;
            // 消去其他行
            for (int r = 0; r < 4; ++r)
            {
                if (r == col)
                    continue;
                float f = a[r][col];
                for (int j = 0; j < 8; ++j)
                    a[r][j] -= f * a[col][j];
            }
        }
        Mat4 result;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                result.m[i][j] = a[i][j + 4];
        return result;
    }

    // ---------- 基础变换矩阵工厂 ----------

    static Mat4 translate(float tx, float ty, float tz)
    {
        Mat4 r = identity();
        r.m[0][3] = tx; // 平移量藏在第 4 列——这就是齐次坐标的意义
        r.m[1][3] = ty;
        r.m[2][3] = tz;
        return r;
    }

    static Mat4 scale(float sx, float sy, float sz)
    {
        Mat4 r = identity();
        r.m[0][0] = sx;
        r.m[1][1] = sy;
        r.m[2][2] = sz;
        return r;
    }

    // 绕 X 轴旋转（右手定则：拇指指向 +X，四指方向为正角度）
    static Mat4 rotateX(float rad)
    {
        Mat4 r = identity();
        float c = std::cos(rad), s = std::sin(rad);
        r.m[1][1] = c;
        r.m[1][2] = -s;
        r.m[2][1] = s;
        r.m[2][2] = c;
        return r;
    }

    static Mat4 rotateY(float rad)
    {
        Mat4 r = identity();
        float c = std::cos(rad), s = std::sin(rad);
        r.m[0][0] = c;
        r.m[0][2] = s;
        r.m[2][0] = -s;
        r.m[2][2] = c;
        return r;
    }

    static Mat4 rotateZ(float rad)
    {
        Mat4 r = identity();
        float c = std::cos(rad), s = std::sin(rad);
        r.m[0][0] = c;
        r.m[0][1] = -s;
        r.m[1][0] = s;
        r.m[1][1] = c;
        return r;
    }
};

}  // namespace sr
