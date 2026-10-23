#pragma once
#include "softrenderer/math/mat4.h"
#include "softrenderer/math/vec.h"
#include "softrenderer/render/pipeline/shader.h"
#include "softrenderer/resource/texture.h"

// ============================================================
// SurfaceShader：带一整套常用 uniform 的着色器基类。
//
// 公共成员即 "uniform"：每帧由外部设置，着色期间对所有顶点/片元只读。
// 内置着色器（Flat / Gouraud / Phong / PBR / Final …）都继承它；
// 只输出几何或做遮罩的着色器直接继承 Shader 即可，不必携带光照参数。
//
// 【复用工具】toWorldNormal 用法线矩阵处理非等比缩放；sampleAlbedo 在
// 「有贴图用贴图、无贴图回退基色」这一常见分支上统一口径。
// ============================================================
namespace sr
{

struct SurfaceShader : Shader
{
    // ---- 变换 ----
    Mat4 model = Mat4::identity();
    Mat4 view = Mat4::identity();
    Mat4 proj = Mat4::identity();
    Mat4 mvp = Mat4::identity();          // proj * view * model（预乘好）
    Mat4 normalMatrix = Mat4::identity(); // model 的逆转置，变换法线用

    // ---- 光照与材质 ----
    Vec3f lightDir{0, 1, 0};            // 指向光源的方向（世界空间，已单位化）
    Vec3f lightColor{1, 1, 1};          // 光颜色
    Vec3f ambient{0.12f, 0.12f, 0.14f}; // 环境光
    Vec3f cameraPos{0, 0, 0};           // 相机世界坐标（高光用）
    Vec3f baseColor{0.8f, 0.8f, 0.8f};  // 材质基色
    float shininess = 32.0f;            // 高光指数
    const Texture *texture = nullptr;   // 可选漫反射贴图

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

}  // namespace sr
