#pragma once
#include <cmath>
#include "softrenderer/math/vec.h"
#include "softrenderer/math/mat4.h"

// ============================================================
// 四元数（Quaternion）
//
// 用于骨骼动画的旋转插值（SLERP），避免欧拉角万向节锁。
// 约定：q = (x, y, z, w)，w 为实部。
//
// 注意：这是渲染核心数学，与 Vec3f/Mat4 紧耦合，
// 不适合用外部库替代（GLM 的 quat 类型与我们的 Vec3f 不兼容）。
// ============================================================

namespace sr
{

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;

    Quat() = default;
    Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

    // 单位四元数
    static Quat identity() { return {0, 0, 0, 1}; }

    // 从轴角构造
    static Quat fromAxisAngle(const Vec3f& axis, float angleRad) {
        float half = angleRad * 0.5f;
        float s = std::sin(half);
        Vec3f a = axis.normalized();
        return {a.x * s, a.y * s, a.z * s, std::cos(half)};
    }

    // 从欧拉角构造（YXZ 顺序，与我们的 rotateY*rotateX*rotateZ 一致）
    static Quat fromEuler(float yaw, float pitch, float roll) {
        float cy = std::cos(yaw * 0.5f), sy = std::sin(yaw * 0.5f);
        float cp = std::cos(pitch * 0.5f), sp = std::sin(pitch * 0.5f);
        float cr = std::cos(roll * 0.5f), sr = std::sin(roll * 0.5f);
        return {
            cy * sp * cr + sy * cp * sr,
            sy * cp * cr - cy * sp * sr,
            cy * cp * sr - sy * sp * cr,
            cy * cp * cr + sy * sp * sr
        };
    }

    // 长度
    float length() const { return std::sqrt(x*x + y*y + z*z + w*w); }

    // 归一化
    Quat normalized() const {
        float len = length();
        if (len < 1e-8f) return identity();
        float inv = 1.0f / len;
        return {x * inv, y * inv, z * inv, w * inv};
    }

    // 共轭（逆，对单位四元数）
    Quat conjugate() const { return {-x, -y, -z, w}; }

    // 四元数乘法
    Quat operator*(const Quat& q) const {
        return {
            w*q.x + x*q.w + y*q.z - z*q.y,
            w*q.y - x*q.z + y*q.w + z*q.x,
            w*q.z + x*q.y - y*q.x + z*q.w,
            w*q.w - x*q.x - y*q.y - z*q.z
        };
    }

    // 旋转向量
    Vec3f rotate(const Vec3f& v) const {
        Quat p{v.x, v.y, v.z, 0};
        Quat r = (*this) * p * conjugate();
        return {r.x, r.y, r.z};
    }

    // 转 Mat4
    Mat4 toMat4() const {
        Mat4 m = Mat4::identity();
        float xx = x*x, yy = y*y, zz = z*z;
        float xy = x*y, xz = x*z, yz = y*z;
        float wx = w*x, wy = w*y, wz = w*z;
        m.m[0][0] = 1 - 2*(yy + zz); m.m[0][1] = 2*(xy - wz);     m.m[0][2] = 2*(xz + wy);
        m.m[1][0] = 2*(xy + wz);     m.m[1][1] = 1 - 2*(xx + zz); m.m[1][2] = 2*(yz - wx);
        m.m[2][0] = 2*(xz - wy);     m.m[2][1] = 2*(yz + wx);     m.m[2][2] = 1 - 2*(xx + yy);
        return m;
    }

    // 点积
    float dot(const Quat& q) const { return x*q.x + y*q.y + z*q.z + w*q.w; }
};

// SLERP：球面线性插值
inline Quat slerp(const Quat& a, const Quat& b, float t) {
    float d = a.dot(b);
    Quat b2 = b;
    if (d < 0) { d = -d; b2 = {-b.x, -b.y, -b.z, -b.w}; } // 取短弧
    if (d > 0.9995f) {
        // 非常接近，用线性插值 + 归一化
        Quat r{a.x + t*(b2.x-a.x), a.y + t*(b2.y-a.y), a.z + t*(b2.z-a.z), a.w + t*(b2.w-a.w)};
        return r.normalized();
    }
    float theta = std::acos(d);
    float sinTheta = std::sin(theta);
    float wa = std::sin((1-t)*theta) / sinTheta;
    float wb = std::sin(t*theta) / sinTheta;
    return {wa*a.x + wb*b2.x, wa*a.y + wb*b2.y, wa*a.z + wb*b2.z, wa*a.w + wb*b2.w};
}

// NLERP：归一化线性插值（比 SLERP 快，匀速但不恒速）
inline Quat nlerp(const Quat& a, const Quat& b, float t) {
    float d = a.dot(b);
    float sign = d < 0 ? -1.0f : 1.0f;
    Quat r{a.x + t*(sign*b.x-a.x), a.y + t*(sign*b.y-a.y),
           a.z + t*(sign*b.z-a.z), a.w + t*(sign*b.w-a.w)};
    return r.normalized();
}

}  // namespace sr
