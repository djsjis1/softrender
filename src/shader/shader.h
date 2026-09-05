#pragma once
#include "math/mat4.h"
#include "scene/mesh.h"
#include "scene/texture.h"

// ============================================================
// 可编程着色器管线的核心抽象（模仿 GPU 的两段式着色）
//
// GPU 渲染管线的两个可编程阶段：
//   顶点着色器(vertex)：对每个顶点执行。输入模型空间顶点，
//     输出裁剪空间坐标(clip)，并把需要插值的量写进 Varyings。
//   片元着色器(fragment)：对每个像素执行。输入插值后的 Varyings，
//     输出该像素颜色（可 discard）。
//
// Varyings：顶点着色器的输出、片元着色器的输入。光栅化器负责
//   把三个顶点的 Varyings 按重心坐标做"透视校正插值"喂给片元。
//   这正是 GLSL 里 varying / in-out 变量的机制。
// ============================================================

// 需要在三角形内插值的顶点属性集合
struct Varyings
{
    Vec3f worldPos;       // 世界空间位置（片元光照、阴影用）
    Vec3f normal;         // 世界空间法线
    Vec2f uv;             // 纹理坐标
    Vec3f color{1, 1, 1}; // 顶点色（Gouraud 把逐顶点光照结果存这里）
    Vec3f tangent;        // 世界空间切线（法线贴图 TBN 用）
};

// 透视校正插值：见阶段 5。iwN = 1/clip.w。
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

// ============================================================
// Shader 基类：具体着色器继承它，实现 vertex / fragment。
// 公共成员即"uniform"——每帧由外部设置，着色期间对所有顶点/片元只读。
// ============================================================
struct Shader
{
    // ---- uniforms（渲染前设置）----
    Mat4 model = Mat4::identity();
    Mat4 view = Mat4::identity();
    Mat4 proj = Mat4::identity();
    Mat4 mvp = Mat4::identity();          // proj * view * model（预乘好）
    Mat4 normalMatrix = Mat4::identity(); // model 的逆转置，变换法线用

    Vec3f lightDir{0, 1, 0};            // 指向光源的方向（世界空间，已单位化）
    Vec3f lightColor{1, 1, 1};          // 光颜色
    Vec3f ambient{0.12f, 0.12f, 0.14f}; // 环境光
    Vec3f cameraPos{0, 0, 0};           // 相机世界坐标（高光用）
    Vec3f baseColor{0.8f, 0.8f, 0.8f};  // 材质基色
    float shininess = 32.0f;            // 高光指数
    const Texture *texture = nullptr;   // 可选漫反射贴图

    virtual ~Shader() = default;

    // 顶点着色：返回裁剪空间坐标，填充 out
    virtual Vec4f vertex(const Vertex &in, Varyings &out) = 0;

    // 逐三角形钩子：拿到三个顶点的 Varyings，可算面法线等（Flat 用）。
    // 默认空实现。
    virtual void geometry(const Varyings &, const Varyings &, const Varyings &) {}

    // 片元着色：给定插值后的 Varyings，输出颜色；返回 false 丢弃该片元
    virtual bool fragment(const Varyings &v, Vec3f &color) = 0;

    // ---- 供子类复用的工具 ----

    // 法线变换到世界空间（逆转置矩阵，处理非等比缩放）
    Vec3f toWorldNormal(const Vec3f &n) const
    {
        return normalMatrix.transformDir(n).normalized();
    }

    // 采样漫反射贴图（无贴图则返回基色）
    Vec3f sampleAlbedo(const Vec2f &uv) const
    {
        if (texture && texture->valid())
            return texture->sampleBilinear(uv.x, uv.y);
        return baseColor;
    }
};
