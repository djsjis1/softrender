#pragma once
#include <cstdint>

namespace sr
{

// ============================================================
// Color：一个像素的颜色，RGBA 各 8 位（0~255）
// 软渲染器里 framebuffer 的最小存储单元。
// ============================================================
struct Color
{
    uint8_t r = 0, g = 0, b = 0, a = 255;

    Color() = default;
    Color(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 255) : r(r_), g(g_), b(b_), a(a_) {}

    // 从 [0,1] 浮点构造，着色计算通常在浮点域进行，最后写入时转 8 位。
    //
    // 把 NaN 夹到 0：旧写法 `v < 0 ? 0 : (v > 1 ? 1 : v)` 对 NaN 两个比较都是假，
    // 原样返回 NaN，随后 `uint8_t(NaN * 255 + 0.5f)` 是未定义行为。
    // 着色器输出 NaN（如退化法线、非有限光照参数）属于常见事故，
    // 这里必须把住最后一道闸门，让它们变成黑色而不是未定义行为。
    static Color fromFloat(float fr, float fg, float fb, float fa = 1.0f)
    {
        auto clamp01 = [](float v) { return v > 0.0f ? (v < 1.0f ? v : 1.0f) : 0.0f; };
        return Color(uint8_t(clamp01(fr) * 255.0f + 0.5f), uint8_t(clamp01(fg) * 255.0f + 0.5f),
                     uint8_t(clamp01(fb) * 255.0f + 0.5f), uint8_t(clamp01(fa) * 255.0f + 0.5f));
    }

    // 颜色线性插值：t=0 得 a，t=1 得 b（画渐变线、顶点色插值都会用到）
    // 结果做四舍五入而非截断，避免渐变出现可见的色带。
    // t 夹到 [0,1]：NaN 的 t 否则会让 `uint8_t(NaN)` 成为未定义行为。
    static Color lerp(const Color& c0, const Color& c1, float t)
    {
        t = t > 0.0f ? (t < 1.0f ? t : 1.0f) : 0.0f;
        auto mix8 = [t](uint8_t a, uint8_t b) { return uint8_t(float(a) + (float(b) - float(a)) * t + 0.5f); };
        return Color(mix8(c0.r, c1.r), mix8(c0.g, c1.g), mix8(c0.b, c1.b), mix8(c0.a, c1.a));
    }
};

// 常用颜色常量
namespace colors {
inline const Color black{0, 0, 0};
inline const Color white{255, 255, 255};
inline const Color red{255, 0, 0};
inline const Color green{0, 255, 0};
inline const Color blue{0, 0, 255};
inline const Color yellow{255, 255, 0};
inline const Color cyan{0, 255, 255};
inline const Color magenta{255, 0, 255};
}  // namespace colors

}  // namespace sr
