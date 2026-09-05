#pragma once
#include <cassert>
#include <cmath>
#include <concepts>  // std::same_as（Vec2 模板约束）
#include <type_traits>

// ============================================================
// 向量库：Vec2 / Vec3 / Vec4（模板，常用 float 与 int 实例）
//
// 渲染器中向量的三种身份，务必区分：
//   - 点(position)：空间中的位置，可被平移
//   - 向量(direction)：方向+长度，平移不变（法线、光线方向）
//   - 齐次坐标下二者用 w 区分：点 w=1，向量 w=0（阶段 3 详述）
// ============================================================

template <typename T>
 requires (std::same_as<T, int> || std::same_as<T, float>)
struct Vec2
{
    T x = 0, y = 0;
    Vec2() = default;
    Vec2(T x_, T y_) : x(x_), y(y_) {}

    Vec2 operator+(const Vec2& v) const { return {x + v.x, y + v.y}; }
    Vec2 operator-(const Vec2& v) const { return {x - v.x, y - v.y}; }
    Vec2 operator*(T s) const { return {x * s, y * s}; }
    Vec2 operator/(T s) const { return {x / s, y / s}; }
    Vec2 operator-() const { return {-x, -y}; }
    // 逐分量乘
    Vec2 operator*(const Vec2& v) const { return {x * v.x, y * v.y}; }
    // 复合赋值
    Vec2& operator+=(const Vec2& v)
    {
        x += v.x;
        y += v.y;
        return *this;
    }
    Vec2& operator-=(const Vec2& v)
    {
        x -= v.x;
        y -= v.y;
        return *this;
    }
    Vec2& operator*=(T s)
    {
        x *= s;
        y *= s;
        return *this;
    }
    bool operator==(const Vec2& v) const { return x == v.x && y == v.y; }
    bool operator!=(const Vec2& v) const { return !(*this == v); }
    T& operator[](int i) { return i == 0 ? x : y; }
    T operator[](int i) const { return i == 0 ? x : y; }

    T lengthSq() const { return x * x + y * y; }
    T length() const { return std::sqrt(lengthSq()); }
    Vec2 normalized() const
    {
        T len = length();
        assert(len > 0);
        return {x / len, y / len};
    }

    // 编译期维度：供通用模板函数（dot/lerp/distance 等）推导循环次数
    static constexpr size_t size = 2;
};

template <typename T>
struct Vec3
{
    T x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(T x_, T y_, T z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3& v) const { return {x + v.x, y + v.y, z + v.z}; }
    Vec3 operator-(const Vec3& v) const { return {x - v.x, y - v.y, z - v.z}; }
    Vec3 operator*(T s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(T s) const { return {x / s, y / s, z / s}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    // 逐分量乘（调制颜色时常用，如 光照强度 * 材质颜色）
    Vec3 operator*(const Vec3& v) const { return {x * v.x, y * v.y, z * v.z}; }
    // 逐分量除
    Vec3 operator/(const Vec3& v) const { return {x / v.x, y / v.y, z / v.z}; }
    // 复合赋值
    Vec3& operator+=(const Vec3& v)
    {
        x += v.x;
        y += v.y;
        z += v.z;
        return *this;
    }
    Vec3& operator-=(const Vec3& v)
    {
        x -= v.x;
        y -= v.y;
        z -= v.z;
        return *this;
    }
    Vec3& operator*=(T s)
    {
        x *= s;
        y *= s;
        z *= s;
        return *this;
    }
    Vec3& operator/=(T s)
    {
        x /= s;
        y /= s;
        z /= s;
        return *this;
    }
    bool operator==(const Vec3& v) const { return x == v.x && y == v.y && z == v.z; }
    bool operator!=(const Vec3& v) const { return !(*this == v); }
    T& operator[](int i) { return i == 0 ? x : (i == 1 ? y : z); }
    T operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }

    T lengthSq() const { return x * x + y * y + z * z; }
    T length() const { return std::sqrt(lengthSq()); }

    // 单位化：方向不变、长度变 1。光照计算前必须对法线/光线方向做这一步，
    // 否则点积结果会被长度放大，亮度就错了
    Vec3 normalized() const
    {
        T len = length();
        assert(len > 0);
        return {x / len, y / len, z / len};
    }

    // 编译期维度：供通用模板函数推导循环次数
    static constexpr size_t size = 3;
};

template <typename T>
struct Vec4
{
    T x = 0, y = 0, z = 0, w = 0;
    Vec4() = default;
    Vec4(T x_, T y_, T z_, T w_) : x(x_), y(y_), z(z_), w(w_) {}
    Vec4(const Vec3<T>& v, T w_) : x(v.x), y(v.y), z(v.z), w(w_) {}

    Vec4 operator+(const Vec4& v) const { return {x + v.x, y + v.y, z + v.z, w + v.w}; }
    Vec4 operator-(const Vec4& v) const { return {x - v.x, y - v.y, z - v.z, w - v.w}; }
    Vec4 operator*(T s) const { return {x * s, y * s, z * s, w * s}; }
    Vec4& operator+=(const Vec4& v)
    {
        x += v.x;
        y += v.y;
        z += v.z;
        w += v.w;
        return *this;
    }
    Vec4& operator*=(T s)
    {
        x *= s;
        y *= s;
        z *= s;
        w *= s;
        return *this;
    }
    bool operator==(const Vec4& v) const { return x == v.x && y == v.y && z == v.z && w == v.w; }
    bool operator!=(const Vec4& v) const { return !(*this == v); }
    T& operator[](int i) { return i == 0 ? x : (i == 1 ? y : (i == 2 ? z : w)); }
    T operator[](int i) const { return i == 0 ? x : (i == 1 ? y : (i == 2 ? z : w)); }

    T lengthSq() const { return x * x + y * y + z * z + w * w; }
    T length() const { return std::sqrt(lengthSq()); }
    Vec4 normalized() const
    {
        T len = length();
        assert(len > 0);
        return {x / len, y / len, z / len, w / len};
    }

    // 编译期维度：供通用模板函数推导循环次数
    static constexpr size_t size = 4;

    Vec3<T> xyz() const { return {x, y, z}; }
};

// ---------- 常用运算 ----------

// ============================================================
// 点积（dot product）——通用版，一次覆盖 2D/3D/4D
//
//   a·b = Σ a[i]·b[i] = |a|·|b|·cosθ
//
// 几何意义：b 在 a 方向上的投影长度 × |a|（反之亦然）。
// 当 a、b 都是单位向量时，a·b 直接等于 cosθ：
//   同向 → +1    垂直 → 0    反向 → -1
//
// 【实现技巧】不写 N 个重载，而是利用每个 Vec 类型自带的
// 编译期常量 size 和 operator[]：循环边界是编译期常量，
// 优化器会完全展开，性能和手写逐分量乘法完全一致。
//
// 【渲染用途】漫反射光照：光强 = max(0, N·L)，即法线朝向
// 光源的程度。这是整个光照模型的核心运算。
//
// 【注意】若两个向量未单位化，结果会混入长度，亮度就错了。
// 光照计算前必须 normalize。
// ============================================================
template <typename A, typename B>
auto dot(const A& a, const B& b)
{
    static_assert(A::size == B::size, "dot: 两个向量的维度必须一致");
    using T = decltype(a.x);
    T sum = T(0);
    for (int i = 0; i < int(A::size); ++i)
        sum += a[i] * b[i];
    return sum;
}

// ============================================================
// 叉积（cross product）
//
//   a×b = (a.y·b.z − a.z·b.y,
//          a.z·b.x − a.x·b.z,
//          a.x·b.y − a.y·b.x)
//
// 两个关键性质：
//   1. 结果垂直于 a 和 b（右手定则：四指从 a 弯向 b，拇指指向结果）
//   2. 结果长度 = |a|·|b|·sinθ = a、b 张成的平行四边形面积
//
// 【渲染用途】
//   - 求三角形法线：n = cross(v1−v0, v2−v0)
//   - 判断顶点绕序/背面朝向（与背面剔除配套）
//   - 求面积、判断退化三角形（|cross| ≈ 0）
//
// 【注意】结果不单位化！做光照前需 .normalized()。
// ============================================================
template <typename T>
Vec3<T> cross(const Vec3<T>& a, const Vec3<T>& b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// ============================================================
// 2D 叉积（返回标量，也叫有向面积）
//
//   a×b = a.x·b.y − a.y·b.x = |a|·|b|·sinθ
//
// 几何意义：a 转到 b 的有向面积的 2 倍。符号含义：
//   正 → b 在 a 的左侧（逆时针）   负 → b 在 a 的右侧（顺时针）
//
// 【渲染用途】三角形光栅化的灵魂——边函数（edge function）：
//   E(p) = cross2(b − a, p − a)
//   对三条边各算一次，同号 ⇒ p 在三角形内部。
//   同时它也是背面剔除（面积 ≤ 0 即背面）与增量式边函数的基石。
// ============================================================
template <typename T>
T cross2(const Vec2<T>& a, const Vec2<T>& b)
{
    return a.x * b.y - a.y * b.x;
}

// ============================================================
// 标量左乘（s·v）：让 v*2 和 2*v 两种写法都可用。
// 因为成员版只定义了 v*s，补上对称操作避免歧义。
// ============================================================
template <typename T>
Vec2<T> operator*(T s, const Vec2<T>& v)
{
    return v * s;
}
template <typename T>
Vec3<T> operator*(T s, const Vec3<T>& v)
{
    return v * s;
}
template <typename T>
Vec4<T> operator*(T s, const Vec4<T>& v)
{
    return v * s;
}

// ============================================================
// 自由函数版 length / normalize（通用，任意维度）
// 提供与成员版等价但 GLM 风格的调用方式：
//   normalize(v)  等价于  v.normalized()
//   length(v)     等价于  v.length()
// 便于将来写泛型代码（模板中不依赖成员函数存在）。
// ============================================================
template <typename V>
auto length(const V& v)
{
    return v.length();
}
template <typename V>
auto normalize(const V& v)
{
    return v.normalized();
}

// ============================================================
// 距离（通用版，任意维度）
//
//   distance(a, b)   = |b − a| = √(Σ (b[i]−a[i])²)
//   distanceSq(a, b) = |b − a|²
//
// 【渲染用途】
//   - 粒子衰减、点光源强度 = 1/d²（distanceSq 免开方更高效）
//   - 相机与物体的远近排序（渲染队列，用 distanceSq 避免开方）
//   - 碰撞/裁剪范围测试
//
// 【注意】需要比较远近时优先用 distanceSq——开方是单调函数，
// 不影响大小关系，但省掉昂贵的 √。
// ============================================================
template <typename A, typename B>
auto distanceSq(const A& a, const B& b)
{
    return (a - b).lengthSq();
}
template <typename A, typename B>
auto distance(const A& a, const B& b)
{
    return (a - b).length();
}

// ============================================================
// 线性插值 lerp / mix（通用版，任意维度）
//
//   lerp(a, b, t) = a + (b − a)·t,   t ∈ [0, 1]
//
// 几何意义：沿 a→b 的直线走 t 比例。t=0 得 a，t=1 得 b，
// t=0.5 得中点。
//
// 【实现技巧】只需依赖 +、−、× 三个运算符，而每个 Vec 类型
// 都有它们，所以一份模板即可覆盖 2D/3D/4D。
//
// 【渲染用途】几乎无处不在：
//   - 颜色渐变（Color::lerp 的向量版）
//   - 顶点属性插值、动画关键帧过渡
//   - 相机平滑跟随（exponential smoothing 的组件）
//   - 后处理混合（bloom 强度、透明过渡）
//
// mix 是 GLSL 中的同名函数，提供别名方便迁移。
// ============================================================
template <typename A, typename B, typename T>
auto lerp(const A& a, const B& b, T t)
{
    return a + (b - a) * t;
}
template <typename A, typename B, typename T>
auto mix(const A& a, const B& b, T t)
{
    return lerp(a, b, t);
}

// ============================================================
// 反射（reflection）
//
//   reflect(I, N) = I − 2·(N·I)·N
//
// 其中：I = 入射方向（指向表面），N = 表面法线（单位向量）。
// 几何意义：入射方向关于法线对称翻转——法线分量取反两倍。
//
// 【渲染用途】
//   - 镜面反射采样：IBL 中 R = reflect(−V, N) 后查环境贴图
//   - 镜面/水面反射向量计算
//
// 【注意】N 应为单位向量；若 N 非单位，结果会被拉长/缩短。
// ============================================================
template <typename T>
Vec3<T> reflect(const Vec3<T>& I, const Vec3<T>& N)
{
    return I - N * (T(2) * dot(N, I));
}

// ============================================================
// 折射（refraction，Snell 定律）
//
//   k = 1 − η²·(1 − cos²θ),   cosθ = N·I
//   折射方向 = η·I − (η·cosθ + √k)·N
//
// 其中：η = 折射率比（入射介质/出射介质，如空气→水 ≈ 1/1.33）。
//
// 【注意】当 k < 0 时发生全反射，返回零向量——
// 调用者应自行回退到 reflect()（玻璃/水的掠射角就是这个效果）。
//
// 【渲染用途】透明材质（玻璃、水）的折射方向，配合光线追踪/
// 环境采样使用。
// ============================================================
template <typename T>
Vec3<T> refract(const Vec3<T>& I, const Vec3<T>& N, T eta)
{
    T cosI = dot(N, I);
    T k = T(1) - eta * eta * (T(1) - cosI * cosI);
    if (k < T(0))
        return {0, 0, 0};  // 全反射
    return I * eta - N * (eta * cosI + std::sqrt(k));
}

// ============================================================
// 逐分量 clamp / min / max（通用版，任意维度）
//
//   clamp(v, lo, hi)[i] = min(max(v[i], lo), hi)
//   min(a, b)[i]       = min(a[i], b[i])
//   max(a, b)[i]       = max(a[i], b[i])
//
// 【实现技巧】与 dot/lerp 相同的策略：利用编译期 size + operator[]，
// 循环边界是常量，优化器完全展开。
//
// 【渲染用途】
//   - clamp：HDR 颜色钳制到 [0,1]、UV 越界处理、参数限幅
//   - min/max：包围盒（AABB）计算、逐通道颜色调制
//
// 【注意】与 std::min/std::max 不同，这里作用于整个向量，
// 按分量独立比较，是 GLSL 的 min/max/clamp 语义。
// 模板用 SFINAE 限定只接受带 size 常量的 Vec 类型，
// 避免误接管标量的 min/max 调用。
// ============================================================
template <typename V, typename T>
std::enable_if_t<(V::size > 0), V> clamp(const V& v, T lo, T hi)
{
    V result;
    for (int i = 0; i < int(V::size); ++i)
    {
        auto val = v[i];
        result[i] = val < lo ? lo : (val > hi ? hi : val);
    }
    return result;
}
template <typename V>
std::enable_if_t<(V::size > 0), V> min(const V& a, const V& b)
{
    V result;
    for (int i = 0; i < int(V::size); ++i)
        result[i] = a[i] < b[i] ? a[i] : b[i];
    return result;
}
template <typename V>
std::enable_if_t<(V::size > 0), V> max(const V& a, const V& b)
{
    V result;
    for (int i = 0; i < int(V::size); ++i)
        result[i] = a[i] > b[i] ? a[i] : b[i];
    return result;
}

using Vec2f = Vec2<float>;
using Vec3f = Vec3<float>;
using Vec4f = Vec4<float>;
using Vec2i = Vec2<int>;
using Vec3i = Vec3<int>;
