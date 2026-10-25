// ============================================================
// 数学模块单元测试：向量、矩阵、四元数、矩形、投影变换与重心坐标
//
// 覆盖 include/softrenderer/math/ 全部公共头。
// 插值类断言一律使用互不相等的顶点值与非对称采样点。
// ============================================================
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <type_traits>

#include "softrenderer/math/barycentric.h"
#include "softrenderer/math/mat3.h"
#include "softrenderer/math/mat4.h"
#include "softrenderer/math/quat.h"
#include "softrenderer/math/rect.h"
#include "softrenderer/math/scalar.h"
#include "softrenderer/math/transform.h"
#include "softrenderer/math/transform2d.h"
#include "softrenderer/math/vec.h"

using namespace sr;

TEST(Vec, DotOrthogonal)
{
    Vec3f a{1, 0, 0}, b{0, 1, 0};
    EXPECT_NEAR(dot(a, b), 0.0f, 1e-6f);
}

TEST(Vec, DotParallel)
{
    Vec3f a{0, 3, 0}, b{0, 5, 0};
    EXPECT_NEAR(dot(a, b), 15.0f, 1e-6f);
}

TEST(Vec, CrossPerpendicular)
{
    Vec3f a{1, 0, 0}, b{0, 1, 0};
    Vec3f c = cross(a, b);
    EXPECT_NEAR(c.x, 0.0f, 1e-6f);
    EXPECT_NEAR(c.y, 0.0f, 1e-6f);
    EXPECT_NEAR(c.z, 1.0f, 1e-6f);
}

TEST(Vec, NormalizeLength)
{
    Vec3f v{3, 4, 0};
    Vec3f n = v.normalized();
    EXPECT_NEAR(n.length(), 1.0f, 1e-5f);
}

// normalizedOr：零向量与 NaN 退回 fallback（否则 Debug 断言、Release 产出 NaN），
// 正常输入与 normalized() 逐分量一致。
TEST(Vec, NormalizedOrFallsBackOnDegenerateInput)
{
    const Vec3f fallback{0, 0, 1};
    const Vec3f zero{0, 0, 0};
    EXPECT_FLOAT_EQ(zero.normalizedOr(fallback).z, 1.0f);

    const Vec3f withNan{std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};
    EXPECT_FLOAT_EQ(withNan.normalizedOr(fallback).z, 1.0f);

    const Vec3f withInf{std::numeric_limits<float>::infinity(), 0.0f, 0.0f};
    EXPECT_FLOAT_EQ(withInf.normalizedOr(fallback).z, 1.0f);

    const Vec3f unit{3, 0, 4};
    const Vec3f exact = unit.normalized();
    const Vec3f safe = unit.normalizedOr(fallback);
    EXPECT_FLOAT_EQ(safe.x, exact.x);
    EXPECT_FLOAT_EQ(safe.z, exact.z);
}

// pow5 必须与 std::pow(x, 5) 数值一致（着色器的菲涅尔项用它替代库调用）。
TEST(ScalarMath, Pow5MatchesStdPow)
{
    float maxError = 0.0f;
    for (int i = 0; i <= 10000; ++i)
    {
        const float x = float(i) / 10000.0f;
        maxError = std::max(maxError, std::abs(pow5(x) - std::pow(x, 5.0f)));
    }
    EXPECT_LT(maxError, 1e-6f);
    EXPECT_FLOAT_EQ(pow5(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(pow5(1.0f), 1.0f);
    // 奇次幂保留符号（调用方已 clamp，但函数本身不应改变符号语义）
    EXPECT_LT(pow5(-0.5f), 0.0f);
}

TEST(Vec, LerpMidpoint)
{
    Vec3f a{0, 0, 0}, b{2, 4, 6};
    Vec3f m = lerp(a, b, 0.5f);
    EXPECT_NEAR(m.x, 1.0f, 1e-6f);
    EXPECT_NEAR(m.y, 2.0f, 1e-6f);
    EXPECT_NEAR(m.z, 3.0f, 1e-6f);
}

TEST(Vec, ReflectMirror)
{
    // 入射 (1,-1,0) 打在法线 (0,1,0) 上，反射应为 (1,1,0)
    Vec3f I = normalize(Vec3f{1, -1, 0});
    Vec3f N{0, 1, 0};
    Vec3f R = reflect(I, N);
    EXPECT_NEAR(R.x, I.x, 1e-5f);
    EXPECT_NEAR(R.y, -I.y, 1e-5f);
    EXPECT_NEAR(R.z, 0.0f, 1e-5f);
}

TEST(Vec, Distance)
{
    Vec3f a{0, 0, 0}, b{3, 4, 0};
    EXPECT_NEAR(distance(a, b), 5.0f, 1e-5f);
    EXPECT_NEAR(distanceSq(a, b), 25.0f, 1e-5f);
}

TEST(Vec, ClampMinMax)
{
    Vec3f v{-1, 0.5f, 2};
    Vec3f c = clamp(v, 0.0f, 1.0f);
    EXPECT_NEAR(c.x, 0.0f, 1e-6f);
    EXPECT_NEAR(c.y, 0.5f, 1e-6f);
    EXPECT_NEAR(c.z, 1.0f, 1e-6f);
    Vec3f lo = min(Vec3f{1, 5, 3}, Vec3f{2, 4, 3});
    EXPECT_NEAR(lo.y, 4.0f, 1e-6f);
    Vec3f hi = max(Vec3f{1, 5, 3}, Vec3f{2, 4, 3});
    EXPECT_NEAR(hi.x, 2.0f, 1e-6f);
}

TEST(Vec, ScalarLeftMultiply)
{
    Vec3f v{1, 2, 3};
    Vec3f r = 2.0f * v;  // 左乘
    EXPECT_NEAR(r.x, 2.0f, 1e-6f);
    EXPECT_NEAR(r.z, 6.0f, 1e-6f);
}

TEST(Vec, CompoundAssignment)
{
    Vec3f v{1, 1, 1};
    v += Vec3f{1, 2, 3};
    EXPECT_NEAR(v.y, 3.0f, 1e-6f);
    v *= 2.0f;
    EXPECT_NEAR(v.z, 8.0f, 1e-6f);
}

TEST(Vec, Equality)
{
    Vec3f a{1, 2, 3}, b{1, 2, 3}, c{1, 2, 4};
    EXPECT_TRUE(a == b);
    EXPECT_TRUE(a != c);
}

TEST(Vec, Vec2Normalized)
{
    Vec2f v{3, 4};
    Vec2f n = v.normalized();
    EXPECT_NEAR(n.length(), 1.0f, 1e-5f);
    EXPECT_NEAR(dot(v, v), 25.0f, 1e-5f);  // 2D 点积
}

TEST(Vec, GenericDot4D)
{
    // 通用 dot 无需重写即可支持 4D（利用编译期 size + operator[]）
    Vec4f a{1, 2, 3, 4}, b{5, 6, 7, 8};
    EXPECT_NEAR(dot(a, b), 70.0f, 1e-5f);
    // 通用 lerp / distance 同样支持 4D
    Vec4f m = lerp(a, b, 0.5f);
    EXPECT_NEAR(m.x, 3.0f, 1e-5f);
    EXPECT_NEAR(m.w, 6.0f, 1e-5f);
    EXPECT_NEAR(distance(a, b), 8.0f, 1e-5f);
}

TEST(Vec, Cross2Sign)
{
    Vec2f a{1, 0}, b{0, 1};
    EXPECT_GT(cross2(a, b), 0.0f);  // 逆时针 = 正
    EXPECT_LT(cross2(b, a), 0.0f);  // 顺时针 = 负
}

TEST(Vec, AddSubtract)
{
    Vec3f a{1, 2, 3}, b{4, 5, 6};
    Vec3f s = a + b;
    EXPECT_FLOAT_EQ(s.x, 5.0f);
    EXPECT_FLOAT_EQ(s.y, 7.0f);
    EXPECT_FLOAT_EQ(s.z, 9.0f);
    Vec3f d = b - a;
    EXPECT_FLOAT_EQ(d.x, 3.0f);
}

TEST(Vec, ScalarMultiply)
{
    Vec3f v{1, 2, 3};
    Vec3f r = v * 2.0f;
    EXPECT_FLOAT_EQ(r.x, 2.0f);
    EXPECT_FLOAT_EQ(r.y, 4.0f);
    EXPECT_FLOAT_EQ(r.z, 6.0f);
}

// ==================== 矩阵 ====================

TEST(Mat4, IdentityMultiply)
{
    Mat4 I = Mat4::identity();
    Vec4f v{1, 2, 3, 1};
    Vec4f r = I * v;
    EXPECT_NEAR(r.x, 1.0f, 1e-6f);
    EXPECT_NEAR(r.y, 2.0f, 1e-6f);
    EXPECT_NEAR(r.z, 3.0f, 1e-6f);
    EXPECT_NEAR(r.w, 1.0f, 1e-6f);
}

TEST(Mat4, TranslatePoint)
{
    Mat4 T = Mat4::translate(1, 2, 3);
    Vec3f p = T.transformPoint({0, 0, 0});
    EXPECT_NEAR(p.x, 1.0f, 1e-6f);
    EXPECT_NEAR(p.y, 2.0f, 1e-6f);
    EXPECT_NEAR(p.z, 3.0f, 1e-6f);
}

TEST(Mat4, TranslateDirectionUnchanged)
{
    Mat4 T = Mat4::translate(10, 20, 30);
    Vec3f d = T.transformDir({1, 0, 0});
    EXPECT_NEAR(d.x, 1.0f, 1e-6f);
    EXPECT_NEAR(d.y, 0.0f, 1e-6f);
    EXPECT_NEAR(d.z, 0.0f, 1e-6f);
}

TEST(Mat4, InverseIdentity)
{
    Mat4 I = Mat4::identity();
    Mat4 inv = I.inversed();
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            EXPECT_NEAR(inv.m[i][j], I.m[i][j], 1e-5f);
}

TEST(Mat4, InverseRoundtrip)
{
    Mat4 M = Mat4::translate(1, 2, 3) * Mat4::rotateY(0.5f) * Mat4::scale(2, 2, 2);
    Mat4 inv = M.inversed();
    Mat4 prod = M * inv;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            EXPECT_NEAR(prod.m[i][j], (i == j ? 1.0f : 0.0f), 1e-4f);
}

TEST(Mat4, RotateY90)
{
    Mat4 R = Mat4::rotateY(3.14159265f / 2.0f);
    Vec3f p = R.transformPoint({1, 0, 0});
    EXPECT_NEAR(p.x, 0.0f, 1e-4f);
    EXPECT_NEAR(p.z, -1.0f, 1e-4f);
}

// ==================== 变换 ====================

TEST(Transform, PerspectiveWEqualsNegZ)
{
    Mat4 P = sr::transform::perspective(1.0f, 1.0f, 0.1f, 100.0f);
    Vec4f v{0, 0, -5, 1};
    Vec4f clip = P * v;
    EXPECT_NEAR(clip.w, 5.0f, 1e-5f);
}

TEST(Transform, LookAtEyeAtOrigin)
{
    Mat4 V = sr::transform::lookAt({0, 0, 0}, {0, 0, -1}, {0, 1, 0});
    Vec3f r = V.transformPoint({0, 0, 0});
    EXPECT_NEAR(r.x, 0.0f, 1e-5f);
    EXPECT_NEAR(r.y, 0.0f, 1e-5f);
    EXPECT_NEAR(r.z, 0.0f, 1e-5f);
}

TEST(Transform, OrthoCenterMapsToZero)
{
    Mat4 O = sr::transform::ortho(-5, 5, -5, 5, 0.1f, 100.0f);
    Vec4f center = O * Vec4f{0, 0, -50.05f, 1};  // z 中心
    EXPECT_NEAR(center.x, 0.0f, 1e-4f);
    EXPECT_NEAR(center.y, 0.0f, 1e-4f);
}

// ==================== 重心坐标 ====================

TEST(Barycentric, VertexReturnsUnit)
{
    Vec2f v0{0, 0}, v1{1, 0}, v2{0, 1};
    Vec3f bc = sr::barycentric(v0, v1, v2, v0);
    EXPECT_NEAR(bc.x, 1.0f, 1e-5f);
    EXPECT_NEAR(bc.y, 0.0f, 1e-5f);
    EXPECT_NEAR(bc.z, 0.0f, 1e-5f);
}

TEST(Barycentric, Centroid)
{
    Vec2f v0{0, 0}, v1{3, 0}, v2{0, 3};
    Vec3f bc = sr::barycentric(v0, v1, v2, {1, 1});
    EXPECT_NEAR(bc.x, 1.0f / 3.0f, 1e-4f);
    EXPECT_NEAR(bc.y, 1.0f / 3.0f, 1e-4f);
    EXPECT_NEAR(bc.z, 1.0f / 3.0f, 1e-4f);
}

TEST(Barycentric, OutsideNegative)
{
    Vec2f v0{0, 0}, v1{1, 0}, v2{0, 1};
    Vec3f bc = sr::barycentric(v0, v1, v2, {2, 2});
    EXPECT_TRUE(bc.x < 0 || bc.y < 0 || bc.z < 0);
}

TEST(Barycentric, SumToOne)
{
    Vec2f v0{0, 0}, v1{4, 0}, v2{1, 3};
    Vec3f bc = sr::barycentric(v0, v1, v2, {1.5f, 1.0f});
    EXPECT_NEAR(bc.x + bc.y + bc.z, 1.0f, 1e-5f);
}

// ==================== 裁剪 ====================

TEST(Quat, IdentityRotation)
{
    Quat q = Quat::identity();
    Vec3f v{1, 2, 3};
    Vec3f r = q.rotate(v);
    EXPECT_NEAR(r.x, 1.0f, 1e-5f);
    EXPECT_NEAR(r.y, 2.0f, 1e-5f);
    EXPECT_NEAR(r.z, 3.0f, 1e-5f);
}

TEST(Quat, Rotate90Y)
{
    Quat q = Quat::fromAxisAngle(Vec3f{0, 1, 0}, 3.14159265f / 2.0f);
    Vec3f r = q.rotate(Vec3f{1, 0, 0});
    EXPECT_NEAR(r.x, 0.0f, 1e-4f);
    EXPECT_NEAR(r.z, -1.0f, 1e-4f);
}

TEST(Quat, SlerpEndpoints)
{
    Quat a = Quat::identity();
    Quat b = Quat::fromAxisAngle(Vec3f{0, 1, 0}, 1.0f);
    Quat s0 = slerp(a, b, 0.0f);
    Quat s1 = slerp(a, b, 1.0f);
    EXPECT_NEAR(s0.w, a.w, 1e-4f);
    EXPECT_NEAR(s1.w, b.w, 1e-4f);
}

TEST(Quat, ToMat4Consistency)
{
    Quat q = Quat::fromAxisAngle(Vec3f{0, 0, 1}, 0.7f);
    Mat4 m = q.toMat4();
    Vec3f v{1, 0, 0};
    Vec3f r1 = q.rotate(v);
    Vec3f r2 = m.transformDir(v);
    EXPECT_NEAR(r1.x, r2.x, 1e-4f);
    EXPECT_NEAR(r1.y, r2.y, 1e-4f);
    EXPECT_NEAR(r1.z, r2.z, 1e-4f);
}

// 零/非有限轴没有方向可言：Vec3::normalized() 只有 assert 守卫，
// Release 下是除零，会得到 NaN 四元数并污染整条蒙皮链。
// 退化输入应返回单位四元数（无旋转），与 Quat::normalized() 对零长度的兜底同口径。
TEST(Quat, FromAxisAngleWithDegenerateAxisStaysFinite)
{
    const Quat zeroAxis = Quat::fromAxisAngle(Vec3f{0, 0, 0}, 1.0f);
    EXPECT_TRUE(std::isfinite(zeroAxis.x) && std::isfinite(zeroAxis.y));
    EXPECT_TRUE(std::isfinite(zeroAxis.z) && std::isfinite(zeroAxis.w));
    EXPECT_FLOAT_EQ(zeroAxis.w, 1.0f);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const Quat nanAxis = Quat::fromAxisAngle(Vec3f{nan, 0, 0}, 1.0f);
    EXPECT_TRUE(std::isfinite(nanAxis.w));
}

// ==================== 粒子系统 ====================

TEST(Mat3, IdentityKeepsPoint)
{
    Mat3 m = Mat3::identity();
    Vec2f p = m.transformPoint({3.0f, -2.0f});
    EXPECT_NEAR(p.x, 3.0f, 1e-5f);
    EXPECT_NEAR(p.y, -2.0f, 1e-5f);
}

TEST(Mat3, Translate)
{
    Mat3 m = Mat3::translate(5.0f, -3.0f);
    Vec2f p = m.transformPoint({1.0f, 1.0f});
    EXPECT_NEAR(p.x, 6.0f, 1e-5f);
    EXPECT_NEAR(p.y, -2.0f, 1e-5f);
}

TEST(Mat3, Rotate90CCW)
{
    Mat3 m = Mat3::rotate(3.14159265f * 0.5f);
    Vec2f p = m.transformPoint({1.0f, 0.0f});
    EXPECT_NEAR(p.x, 0.0f, 1e-5f);
    EXPECT_NEAR(p.y, 1.0f, 1e-5f);
}

TEST(Mat3, InverseRoundTrip)
{
    Mat3 m = Mat3::translate(4.0f, -2.0f) * Mat3::rotate(0.7f) * Mat3::scale(2.0f, 3.0f);
    Vec2f p{1.5f, -0.5f};
    Vec2f q = m.inversed().transformPoint(m.transformPoint(p));
    EXPECT_NEAR(q.x, p.x, 1e-4f);
    EXPECT_NEAR(q.y, p.y, 1e-4f);
}

TEST(Mat3, ToMat4AgreesOnXY)
{
    Mat3 m = Mat3::translate(3.0f, 5.0f) * Mat3::rotate(0.4f);
    Mat4 m4 = m.toMat4();
    Vec2f p2 = m.transformPoint({2.0f, 1.0f});
    Vec3f p3 = m4.transformPoint({2.0f, 1.0f, 0.0f});
    EXPECT_NEAR(p2.x, p3.x, 1e-4f);
    EXPECT_NEAR(p2.y, p3.y, 1e-4f);
    EXPECT_NEAR(p3.z, 0.0f, 1e-4f);
}

// ==================== Rect ====================

TEST(Rect, ContainsAndIntersect)
{
    Recti a(0, 0, 10, 10);
    EXPECT_TRUE(a.contains(5, 5));
    EXPECT_FALSE(a.contains(10, 5)); // 半开：右边界不含
    Recti b(5, 5, 10, 10);
    EXPECT_TRUE(a.intersects(b));
    Recti c(20, 20, 5, 5);
    EXPECT_FALSE(a.intersects(c));
}

// 空矩形不得与任何矩形相交（旧实现只看边界，零面积矩形会判为相交）。
TEST(Rect, EmptyRectNeverIntersects)
{
    const Recti a(0, 0, 10, 10);
    EXPECT_FALSE(a.intersects(Recti(5, 5, 0, 0)));
    EXPECT_FALSE(a.intersects(Recti(5, 5, 0, 4)));
    EXPECT_FALSE(Recti(5, 5, -2, 4).intersects(a));
}

TEST(Rect, IntersectionAndMerge)
{
    Recti a(0, 0, 10, 10), b(5, 5, 10, 10);
    Recti inter = a.intersection(b);
    EXPECT_EQ(inter.x, 5);
    EXPECT_EQ(inter.w, 5);
    Recti uni = a.merged(b);
    EXPECT_EQ(uni.x, 0);
    EXPECT_EQ(uni.right(), 15);
}

TEST(Rect, Center)
{
    Rectf r(0.0f, 0.0f, 4.0f, 6.0f);
    Vec2f c = r.center();
    EXPECT_NEAR(c.x, 2.0f, 1e-5f);
    EXPECT_NEAR(c.y, 3.0f, 1e-5f);
}

// ==================== 2D 投影与相机 ====================

TEST(Transform2D, Ortho2DMapsWorldToDevice)
{
    Mat3 o = sr::transform::ortho2D(0.0f, 100.0f, 0.0f, 50.0f, 200, 100);
    Vec2f p = o.transformPoint({100.0f, 50.0f}); // 世界右上角
    EXPECT_NEAR(p.x, 200.0f, 1e-3f);
    EXPECT_NEAR(p.y, 100.0f, 1e-3f);
}

