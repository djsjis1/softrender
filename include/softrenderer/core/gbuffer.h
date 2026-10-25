#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>
#include "softrenderer/math/vec.h"
#include "softrenderer/core/float_framebuffer.h"

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
        // 先在局部构造完再发布：旧实现先改 width/height 再 assign，
        // 任一 assign 抛异常都会留下“尺寸已变、数据是旧的”的失配状态。
        if (w < 0 || h < 0)
            return;
        const size_t n = size_t(w) * size_t(h);
        std::vector<Vec3f> newWorldPos(n, Vec3f{0, 0, 0});
        std::vector<Vec4f> newNormalRough(n, Vec4f{0, 0, 1, 0.5f});
        std::vector<Vec4f> newAlbedoMetal(n, Vec4f{0.8f, 0.8f, 0.8f, 0});
        std::vector<float> newDepth(n, 1e9f);
        worldPos.swap(newWorldPos);
        normalRough.swap(newNormalRough);
        albedoMetal.swap(newAlbedoMetal);
        depth.swap(newDepth);
        width = w;
        height = h;
    }

    void clear()
    {
        std::fill(worldPos.begin(), worldPos.end(), Vec3f{0, 0, 0});
        std::fill(normalRough.begin(), normalRough.end(), Vec4f{0, 0, 1, 0.5f});
        std::fill(albedoMetal.begin(), albedoMetal.end(), Vec4f{0.8f, 0.8f, 0.8f, 0});
        std::fill(depth.begin(), depth.end(), 1e9f);
    }

    // 写入一个像素的 G-Buffer 数据（带深度测试）。
    //
    // z 必须是有限值：NaN 与任何值的比较都为假，`z >= depth[idx]` 拦不住它，
    // 一旦写进去，后续所有像素与 NaN 比较也是假，深度测试会整体失效。
    bool write(int x, int y, float z, const Vec3f &pos,
               const Vec3f &normal, float roughness,
               const Vec3f &albedo, float metallic)
    {
        if (x < 0 || x >= width || y < 0 || y >= height)
            return false;
        if (!std::isfinite(z))
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


} // namespace sr
