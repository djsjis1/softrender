#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

#include "softrenderer/core/gbuffer.h"

namespace sr
{

// 延迟光照 Pass：对 G-Buffer 每个像素算 Blinn-Phong / PBR
inline void lightingPass(const GBuffer &gbuf, const Vec3f &lightDir,
                         const Vec3f &lightColor, const Vec3f &cameraPos,
                         const Vec3f &ambient, std::vector<Vec3f> &outColor)
{
    size_t n = size_t(gbuf.width) * gbuf.height;
    outColor.resize(n);
    for (size_t i = 0; i < n; ++i)
    {
        if (gbuf.depth[i] >= 1e8f)
        {
            outColor[i] = ambient * 0.1f;
            continue;
        } // 背景

        Vec3f N = Vec3f{gbuf.normalRough[i].x, gbuf.normalRough[i].y, gbuf.normalRough[i].z};
        if (N.lengthSq() < 1e-6f)
        {
            outColor[i] = Vec3f{0, 0, 0};
            continue;
        }
        N = N.normalized();
        Vec3f albedo = Vec3f{gbuf.albedoMetal[i].x, gbuf.albedoMetal[i].y, gbuf.albedoMetal[i].z};
        float metallic = gbuf.albedoMetal[i].w;
        float roughness = gbuf.normalRough[i].w;
        Vec3f P = gbuf.worldPos[i];

        Vec3f L = lightDir;
        Vec3f V = (cameraPos - P).normalized();
        Vec3f H = (L + V).normalized();

        float NdotL = std::max(dot(N, L), 0.0f);
        float NdotV = std::max(dot(N, V), 0.001f);
        float NdotH = std::max(dot(N, H), 0.0f);

        // 简化 PBR
        Vec3f F0 = Vec3f{0.04f, 0.04f, 0.04f} * (1.0f - metallic) + albedo * metallic;
        float a = roughness * roughness;
        float a2 = a * a;
        float denom = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
        float D = a2 / (3.14159265f * denom * denom + 1e-7f);
        float k = (roughness + 1.0f) * (roughness + 1.0f) / 8.0f;
        float G = (NdotV / (NdotV * (1 - k) + k)) * (NdotL / (NdotL * (1 - k) + k));
        Vec3f F = F0 + (Vec3f{1, 1, 1} - F0) * std::pow(1.0f - std::max(dot(V, H), 0.0f), 5.0f);
        Vec3f spec = F * (D * G / (4.0f * NdotV * NdotL + 1e-7f));
        Vec3f kD = (Vec3f{1, 1, 1} - F) * (1.0f - metallic);
        Vec3f diff = kD * albedo * (1.0f / 3.14159265f);

        outColor[i] = (diff + spec) * lightColor * NdotL + albedo * ambient;
    }
}

} // namespace sr
