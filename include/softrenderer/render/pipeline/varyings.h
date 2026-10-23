#pragma once
#include "softrenderer/math/vec.h"

// ============================================================
// Varyings：顶点着色器输出、片元着色器输入的插值契约。
//
// 光栅化器按重心坐标做「透视校正插值」，把三个顶点的 Varyings 混合后
// 喂给 fragment —— 等价于 GLSL 里 varying / in-out 变量的机制。
//
// 【为什么独立成头】裁剪（clip.h）只需要「可插值的顶点属性」这一契约，
// 不应连带拉入整套 Shader 接口、uniform 与纹理依赖。
//
// ⚠ 新增字段必须同步修改 interpolateVaryings，否则该字段不会被插值，
// 会静默得到未初始化的值（工程红线，见 engineering_rules.md）。
// ============================================================
namespace sr
{

struct Varyings
{
    Vec3f worldPos;       // 世界空间位置（片元光照、阴影用）
    Vec3f normal;         // 世界空间法线
    Vec2f uv;             // 纹理坐标
    Vec3f color{1, 1, 1}; // 顶点色（Gouraud 把逐顶点光照结果存这里）
    Vec3f tangent;        // 世界空间切线（法线贴图 TBN 用）
};

// 透视校正插值：iwN = 1/clip.w。
// 属性 A 的正确插值 = Σ(bc_i * A_i / w_i) / Σ(bc_i / w_i)
inline Varyings interpolateVaryings(const Varyings &a, const Varyings &b,
                                    const Varyings &c, const Vec3f &bc,
                                    float iw0, float iw1, float iw2)
{
    float w0 = bc.x * iw0, w1 = bc.y * iw1, w2 = bc.z * iw2;
    float inv = 1.0f / (w0 + w1 + w2);
    Varyings r;
    r.worldPos = (a.worldPos * w0 + b.worldPos * w1 + c.worldPos * w2) * inv;
    r.normal = (a.normal * w0 + b.normal * w1 + c.normal * w2) * inv;
    r.uv = (a.uv * w0 + b.uv * w1 + c.uv * w2) * inv;
    r.color = (a.color * w0 + b.color * w1 + c.color * w2) * inv;
    r.tangent = (a.tangent * w0 + b.tangent * w1 + c.tangent * w2) * inv;
    return r;
}

}  // namespace sr
