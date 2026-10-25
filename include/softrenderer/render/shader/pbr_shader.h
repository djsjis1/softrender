#pragma once
#include <algorithm>
#include <cmath>

#include "softrenderer/math/scalar.h"
#include "softrenderer/resource/cubemap.h"
#include "softrenderer/render/pipeline/surface_shader.h"

// ============================================================
// PBR 着色器：Cook-Torrance 微表面 BRDF
//
// 与 Blinn-Phong 的本质区别：PBR 基于物理——能量守恒、
// 金属/非金属统一工作流、粗糙度控制微表面分布。
//
// BRDF = D * G * F / (4 * N·V * N·L)
//
//   D = GGX 法线分布函数（粗糙度控制微表面朝向集中度）
//   G = Smith-Schlick 几何遮蔽函数（微表面自遮挡）
//   F = Schlick 菲涅尔近似（掠射角反射增强）
//
// 金属度工作流：
//   金属：albedo = 镜面反射色 F0（有色的金属反射），漫反射 = 0
//   非金属：F0 ≈ 0.04（白色弱反射），漫反射 = albedo
// ============================================================
namespace sr
{

struct PBRShader : SurfaceShader
{
    float metallic = 0.0f;            // 0=非金属, 1=金属
    float roughness = 0.5f;           // 0=镜面, 1=粗糙
    const Cubemap* envMap = nullptr;  // IBL 环境贴图（可选）

    Vec4f vertex(const Vertex& in, Varyings& out) override
    {
        out.worldPos = model.transformPoint(in.position);
        out.normal = toWorldNormal(in.normal);
        out.uv = in.uv;
        return mvp * Vec4f(in.position, 1.0f);
    }

    bool fragment(const Varyings& v, Vec3f& color) override
    {
        // 法线/视向量/半程向量都可能退化（零法线、相机与片元重合、光向与视线反向）。
        // 用 normalizedOr 兜底：正常输入结果与 normalized() 完全一致，
        // 退化输入不再触发 Debug 断言，也不会把 NaN 传进 BRDF。
        Vec3f N = v.normal.normalizedOr({0, 0, 1});
        Vec3f V = (cameraPos - v.worldPos).normalizedOr({0, 0, 1});
        Vec3f L = lightDir;
        if (!(L.lengthSq() > 0.0f) || !std::isfinite(L.lengthSq()))
            L = {0, 1, 0};
        Vec3f H = (V + L).normalizedOr(N);

        float NdotL = std::max(dot(N, L), 0.0f);
        float NdotV = std::max(dot(N, V), 0.001f);
        float NdotH = std::max(dot(N, H), 0.0f);
        float VdotH = std::max(dot(V, H), 0.0f);

        Vec3f albedo = sampleAlbedo(v.uv);

        // F0：金属用 albedo 作反射色，非金属用 0.04
        Vec3f F0 = Vec3f{0.04f, 0.04f, 0.04f} * (1.0f - metallic) + albedo * metallic;

        // --- D: GGX / Trowbridge-Reitz ---
        float a = roughness * roughness;
        float a2 = a * a;
        float denom = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
        float D = a2 / (3.14159265f * denom * denom + 1e-7f);

        // --- G: Smith-Schlick-GGX ---
        float k = (roughness + 1.0f) * (roughness + 1.0f) / 8.0f;
        float G1V = NdotV / (NdotV * (1.0f - k) + k);
        float G1L = NdotL / (NdotL * (1.0f - k) + k);
        float G = G1V * G1L;

        // --- F: Schlick 菲涅尔 ---
        Vec3f F = F0 + (Vec3f{1, 1, 1} - F0) * pow5(1.0f - VdotH);

        // --- 镜面 BRDF ---
        float specScalar = (D * G) / (4.0f * NdotV * NdotL + 1e-7f);
        Vec3f specColor = F * specScalar;

        // --- 漫反射（金属无漫反射）---
        Vec3f kD = (Vec3f{1, 1, 1} - F) * (1.0f - metallic);
        Vec3f diffuse = kD * albedo * (1.0f / 3.14159265f);

        // --- 合成：直接光 + IBL 环境光 ---
        Vec3f directLight = (diffuse + specColor) * lightColor * NdotL;

        // --- IBL 环境反射 ---
        Vec3f iblColor{0, 0, 0};
        if (envMap && envMap->valid())
        {
            // 镜面 IBL：用反射向量采样环境贴图。
            // V 指向相机，入射方向是 -V；这里必须用未夹取的真实点积，
            // NdotV 已被 max(...,0.001) 夹取，不能拿它代换。
            Vec3f R = reflect(-V, N);
            // 粗糙度越大，反射越模糊（当前 cubemap 无预滤波链，见 sampleReflection 注释）
            Vec3f envSpec = envMap->sampleReflection(R, roughness);
            // 菲涅尔加权：掠射角反射更强
            Vec3f F_ibl = F0 + (Vec3f{1, 1, 1} - F0) * pow5(1.0f - NdotV);
            // 粗糙度衰减（简化 GGX 环境积分）
            float roughFactor = 1.0f - roughness * 0.7f;
            iblColor = iblColor + F_ibl * envSpec * roughFactor;

            // 漫反射 IBL：用法线采样环境辐照度
            Vec3f envIrradiance = envMap->sampleIrradiance(N, 16);
            iblColor = iblColor + kD * albedo * envIrradiance;
        }

        color = directLight + iblColor + albedo * ambient;
        return true;
    }
};

}  // namespace sr
