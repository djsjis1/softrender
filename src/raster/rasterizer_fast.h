#pragma once
// ============================================================
// 实验性光栅化器：增量式边函数 + SSE2 四像素并行
//
// 【定位：技术验证 / 基准测试用，未接入 Renderer 主渲染链路】
//   全仓引用方只有 core/benchmark.h 与单元测试，Renderer::rasterTriangle
//   走的是自己的一套增量边函数实现。不接入的两个原因：
//     1. 像素写入用 zb.testAndSet + fb.setPixel，两者各做一次边界检查；
//        而 renderer.h 用 fb.row(y) / zbuf_.row(y) 行指针直写，完全免检。
//        本模块在写入路径上反而更慢。
//     2. 只接受单一 Color，不支持 Varyings 插值与 Shader::fragment 回调，
//        无法承载完整着色管线。
//   接入需同时改造这两点，属架构级变更，必须先立 ADR。
//
// 【绕序】同时接受逆时针与顺时针三角形，setupEdges 内部已做符号归一化。
//   本模块不做背面剔除——那是 Renderer::cullBackface 的职责。
//
// 【增量式边函数原理】
// 边函数 E(x,y) = cross2(edge, p - v) 对 x,y 是线性的：
//   E(x+1, y) = E(x,y) + dy_edge    （dy_edge = edge.y 为常数）
//   E(x, y+1) = E(x,y) - dx_edge    （dx_edge = edge.x 为常数）
// 所以只需在包围盒左上角算一次 E，之后逐像素加常数——
// 把每像素 3 次叉积（6 次乘法+6 次减法）降为 3 次加法。
//
// 【SSE2 四像素并行】
// 一次处理 4 个连续 x 像素：用 __m128 同时维护 3 条边的 E 值，
// 比较是否全 >= 0（_mm_movemask_ps），一次判断 4 像素内外。
// 对内部像素批量插值深度/属性。
//
// 参考：Mesa llvmpipe 的 tile 光栅化、Fabian Giesen 的系列博客。
// ============================================================

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "core/depthbuffer.h"
#include "core/framebuffer.h"
#include "math/vec.h"

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#    define SR_HAS_SSE2 1
#    include <emmintrin.h>
#else
#    define SR_HAS_SSE2 0
#endif

namespace raster_fast {

// 边函数的增量常数
struct EdgeSetup
{
    float e0, e1, e2;              // 包围盒左上角像素中心的边函数值
    float de0_dx, de1_dx, de2_dx;  // x 方向增量（= edge.y）
    float de0_dy, de1_dy, de2_dy;  // y 方向增量（= -edge.x）
    float invArea;                 // 1 / (2 * signed_area)
};

// 预计算边函数增量
inline EdgeSetup setupEdges(const Vec2f& v0, const Vec2f& v1, const Vec2f& v2, int minX, int minY)
{
    // 边函数增量（直接推导，无需中间变量）
    // 面积 = cross2(v1-v0, v2-v0)
    float area2 = (v1.x - v0.x) * (v2.y - v0.y) - (v1.y - v0.y) * (v2.x - v0.x);
    if (std::abs(area2) < 1e-8f)
        return {};

    // 左上角像素中心
    float px = minX + 0.5f, py = minY + 0.5f;
    // 边函数值：E0 = cross2(v1-v0, p-v0)
    // cross2(a,b) = a.x*b.y - a.y*b.x
    float e0 = (v1.x - v0.x) * (py - v0.y) - (v1.y - v0.y) * (px - v0.x);
    float e1 = (v2.x - v1.x) * (py - v1.y) - (v2.y - v1.y) * (px - v1.x);
    float e2 = (v0.x - v2.x) * (py - v2.y) - (v0.y - v2.y) * (px - v2.x);

    // 增量推导：E(p) = (v_next-v_cur) × (p-v_cur)
    //   dE/dx = -(v_next.y - v_cur.y)
    //   dE/dy =  (v_next.x - v_cur.x)
    float de0x = -(v1.y - v0.y);
    float de0y = (v1.x - v0.x);
    float de1x = -(v2.y - v1.y);
    float de1y = (v2.x - v1.x);
    float de2x = -(v0.y - v2.y);
    float de2y = (v0.x - v2.x);

    // 顺时针绕序（area2 < 0）时三个边函数在三角形内部全为负，
    // 内点判定 e0>=0 && e1>=0 && e2>=0 会永不成立，导致整个三角形被静默丢弃。
    // 在此统一翻转符号，让调用方无需感知绕序；invArea 取 |area2| 的倒数，
    // 保证重心权重恒为正。与 renderer.h 的 sign 处理等价。
    const float sign = (area2 < 0.0f) ? -1.0f : 1.0f;
    e0 *= sign;
    e1 *= sign;
    e2 *= sign;
    de0x *= sign;
    de0y *= sign;
    de1x *= sign;
    de1y *= sign;
    de2x *= sign;
    de2y *= sign;

    return {e0, e1, e2, de0x, de1x, de2x, de0y, de1y, de2y, 1.0f / std::abs(area2)};
}

// 标量版增量光栅化（无 SIMD，但用增量边函数）
inline void fillTriangleIncremental(Framebuffer& fb, DepthBuffer& zb, const Vec2f& v0, const Vec2f& v1, const Vec2f& v2,
                                    float z0, float z1, float z2, Color color)
{
    int minX = std::max(0, (int)std::floor(std::min({v0.x, v1.x, v2.x})));
    int maxX = std::min(fb.width() - 1, (int)std::ceil(std::max({v0.x, v1.x, v2.x})));
    int minY = std::max(0, (int)std::floor(std::min({v0.y, v1.y, v2.y})));
    int maxY = std::min(fb.height() - 1, (int)std::ceil(std::max({v0.y, v1.y, v2.y})));

    EdgeSetup es = setupEdges(v0, v1, v2, minX, minY);
    if (std::abs(es.invArea) < 1e-20f)
        return;

    // 深度增量（z 也是像素坐标的线性函数，可用同一套增量递推）。
    //
    // 关键：边函数与重心权重是「错位」对应，不是同序对应——
    //   e0 = 边 v0→v1 的函数 → 对应 **v2** 的权重
    //   e1 = 边 v1→v2 的函数 → 对应 **v0** 的权重
    //   e2 = 边 v2→v0 的函数 → 对应 **v1** 的权重
    // 因为 e_i 度量的是「p 离边 i 有多远」，离边 v0→v1 越远意味着 v2 的贡献越大。
    // 所以 z = (e1*z0 + e2*z1 + e0*z2) * invArea，
    // 与 renderer.h 中 bc{e1*invArea, e2*invArea, e0*invArea} 一致。
    //
    // 曾在此处写成同序映射 (e0*z0 + e1*z1 + e2*z2)，因单元测试全部使用
    // 平坦深度（z0==z1==z2）而长期未被发现：顶点深度全相等时任何权重排列
    // 都算出同一个值。回归用例见 RasterizerFast.fillTriangleIncremental_slopeDepthMatchesBarycentric。
    float dz_dx = (es.de1_dx * z0 + es.de2_dx * z1 + es.de0_dx * z2) * es.invArea;
    float dz_dy = (es.de1_dy * z0 + es.de2_dy * z1 + es.de0_dy * z2) * es.invArea;

    float e0_row = es.e0, e1_row = es.e1, e2_row = es.e2;
    float z_row = (es.e1 * z0 + es.e2 * z1 + es.e0 * z2) * es.invArea;

    for (int y = minY; y <= maxY; ++y)
    {
        float e0 = e0_row, e1 = e1_row, e2 = e2_row;
        float z = z_row;
        for (int x = minX; x <= maxX; ++x)
        {
            if (e0 >= 0 && e1 >= 0 && e2 >= 0)
            {
                if (zb.testAndSet(x, y, z))
                    fb.setPixel(x, y, color);
            }
            e0 += es.de0_dx;
            e1 += es.de1_dx;
            e2 += es.de2_dx;
            z += dz_dx;
        }
        e0_row += es.de0_dy;
        e1_row += es.de1_dy;
        e2_row += es.de2_dy;
        z_row += dz_dy;
    }
}

#if SR_HAS_SSE2
// SSE2 版：每次处理 4 个 x 像素
inline void fillTriangleSSE2(Framebuffer& fb, DepthBuffer& zb, const Vec2f& v0, const Vec2f& v1, const Vec2f& v2,
                             float z0, float z1, float z2, Color color)
{
    int minX = std::max(0, (int)std::floor(std::min({v0.x, v1.x, v2.x})));
    int maxX = std::min(fb.width() - 1, (int)std::ceil(std::max({v0.x, v1.x, v2.x})));
    int minY = std::max(0, (int)std::floor(std::min({v0.y, v1.y, v2.y})));
    int maxY = std::min(fb.height() - 1, (int)std::ceil(std::max({v0.y, v1.y, v2.y})));

    EdgeSetup es = setupEdges(v0, v1, v2, minX, minY);
    if (std::abs(es.invArea) < 1e-20f)
        return;

    // 权重错位映射的理由见标量版 fillTriangleIncremental 中的注释
    float dz_dx = (es.de1_dx * z0 + es.de2_dx * z1 + es.de0_dx * z2) * es.invArea;
    float dz_dy = (es.de1_dy * z0 + es.de2_dy * z1 + es.de0_dy * z2) * es.invArea;

    // SSE 常数
    __m128 de0x4 = _mm_set1_ps(es.de0_dx * 4.0f);
    __m128 de1x4 = _mm_set1_ps(es.de1_dx * 4.0f);
    __m128 de2x4 = _mm_set1_ps(es.de2_dx * 4.0f);
    __m128 de0x1 = _mm_set1_ps(es.de0_dx);
    __m128 de1x1 = _mm_set1_ps(es.de1_dx);
    __m128 de2x1 = _mm_set1_ps(es.de2_dx);
    __m128 zero = _mm_setzero_ps();

    float e0_row = es.e0, e1_row = es.e1, e2_row = es.e2;
    float z_row = (es.e1 * z0 + es.e2 * z1 + es.e0 * z2) * es.invArea;

    for (int y = minY; y <= maxY; ++y)
    {
        // 行首 4 像素的边函数值
        __m128 e0 = _mm_set_ps(e0_row + es.de0_dx * 3, e0_row + es.de0_dx * 2, e0_row + es.de0_dx, e0_row);
        __m128 e1 = _mm_set_ps(e1_row + es.de1_dx * 3, e1_row + es.de1_dx * 2, e1_row + es.de1_dx, e1_row);
        __m128 e2 = _mm_set_ps(e2_row + es.de2_dx * 3, e2_row + es.de2_dx * 2, e2_row + es.de2_dx, e2_row);
        float z = z_row;

        int x = minX;
        // 4 像素一组
        for (; x + 3 <= maxX; x += 4)
        {
            // 三条边全 >= 0 的像素 mask
            __m128 m0 = _mm_cmpge_ps(e0, zero);
            __m128 m1 = _mm_cmpge_ps(e1, zero);
            __m128 m2 = _mm_cmpge_ps(e2, zero);
            int mask = _mm_movemask_ps(_mm_and_ps(_mm_and_ps(m0, m1), m2));

            if (mask == 0xF)
            {
                // 4 像素全在内部
                for (int i = 0; i < 4; ++i)
                {
                    if (zb.testAndSet(x + i, y, z + dz_dx * i))
                        fb.setPixel(x + i, y, color);
                }
            }
            else if (mask != 0)
            {
                // 部分在内部
                for (int i = 0; i < 4; ++i)
                {
                    if (mask & (1 << i))
                    {
                        if (zb.testAndSet(x + i, y, z + dz_dx * i))
                            fb.setPixel(x + i, y, color);
                    }
                }
            }
            // 增量 +4
            e0 = _mm_add_ps(e0, de0x4);
            e1 = _mm_add_ps(e1, de1x4);
            e2 = _mm_add_ps(e2, de2x4);
            z += dz_dx * 4.0f;
        }
        // 尾部不足 4 像素
        float e0s = e0_row + es.de0_dx * (x - minX);
        float e1s = e1_row + es.de1_dx * (x - minX);
        float e2s = e2_row + es.de2_dx * (x - minX);
        float zs = z_row + dz_dx * (x - minX);
        for (; x <= maxX; ++x)
        {
            if (e0s >= 0 && e1s >= 0 && e2s >= 0)
            {
                if (zb.testAndSet(x, y, zs))
                    fb.setPixel(x, y, color);
            }
            e0s += es.de0_dx;
            e1s += es.de1_dx;
            e2s += es.de2_dx;
            zs += dz_dx;
        }
        e0_row += es.de0_dy;
        e1_row += es.de1_dy;
        e2_row += es.de2_dy;
        z_row += dz_dy;
    }
}
#endif  // SR_HAS_SSE2

// 统一入口：有 SSE2 用 SSE2，否则用标量增量
inline void fillTriangleFast(Framebuffer& fb, DepthBuffer& zb, const Vec2f& v0, const Vec2f& v1, const Vec2f& v2,
                             float z0, float z1, float z2, Color color)
{
#if SR_HAS_SSE2
    fillTriangleSSE2(fb, zb, v0, v1, v2, z0, z1, z2, color);
#else
    fillTriangleIncremental(fb, zb, v0, v1, v2, z0, z1, z2, color);
#endif
}

}  // namespace raster_fast
