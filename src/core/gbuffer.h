#pragma once
#include <vector>
#include "math/vec.h"
#include "core/float_framebuffer.h"

// ============================================================
// G-Buffer：延迟渲染的几何缓冲
//
// 延迟渲染分两趟：
//   Pass 1（Geometry Pass）：只输出几何信息到 G-Buffer，不做光照
//   Pass 2（Lighting Pass）：对每个像素读 G-Buffer，算光照
//
// 优势：光照计算量与像素数成正比，而非与"像素×光源×三角形"成正比。
//       多光源场景下比前向渲染快得多。
//
// G-Buffer 布局（每像素）：
//   RT0: worldPosition.xyz + 1.0 (w 留空)
//   RT1: worldNormal.xyz + roughness
//   RT2: albedo.rgb + metallic
//   Depth: 深度（共用 FloatFramebuffer 的 depth）
//
// 参考：LearnOpenGL Deferred Shading、Filament 的 FrameGraph。
//
// 【命名空间】GBuffer 及本文件的延迟光照自由函数已迁入 namespace sr
// （ADR-001 第 2 批）。实测引用面：仅本文件 + tests/test_core.cpp，故不留全局别名。
// ============================================================

namespace sr
{

struct GBuffer
{
    int width = 0, height = 0;
    std::vector<Vec3f> worldPos;    // RT0
    std::vector<Vec4f> normalRough; // RT1: xyz=normal, w=roughness
    std::vector<Vec4f> albedoMetal; // RT2: rgb=albedo, a=metallic
    std::vector<float> depth;       // 深度

    void resize(int w, int h)
    {
        width = w;
        height = h;
        size_t n = size_t(w) * h;
        worldPos.assign(n, Vec3f{0, 0, 0});
        normalRough.assign(n, Vec4f{0, 0, 1, 0.5f});
        albedoMetal.assign(n, Vec4f{0.8f, 0.8f, 0.8f, 0});
        depth.assign(n, 1e9f);
    }

    void clear()
    {
        std::fill(worldPos.begin(), worldPos.end(), Vec3f{0, 0, 0});
        std::fill(normalRough.begin(), normalRough.end(), Vec4f{0, 0, 1, 0.5f});
        std::fill(albedoMetal.begin(), albedoMetal.end(), Vec4f{0.8f, 0.8f, 0.8f, 0});
        std::fill(depth.begin(), depth.end(), 1e9f);
    }

    // 写入一个像素的 G-Buffer 数据（带深度测试）
    bool write(int x, int y, float z, const Vec3f &pos,
               const Vec3f &normal, float roughness,
               const Vec3f &albedo, float metallic)
    {
        if (x < 0 || x >= width || y < 0 || y >= height)
            return false;
        size_t idx = size_t(y) * width + x;
        if (z >= depth[idx])
            return false;
        depth[idx] = z;
        worldPos[idx] = pos;
        normalRough[idx] = Vec4f{normal.x, normal.y, normal.z, roughness};
        albedoMetal[idx] = Vec4f{albedo.x, albedo.y, albedo.z, metallic};
        return true;
    }
};

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
