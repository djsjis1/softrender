#pragma once
#include <array>
#include <cstddef>
#include <utility>
#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include "softrenderer/core/color.h"
#include "softrenderer/core/float_framebuffer.h"
#include "softrenderer/core/framebuffer.h"
#include "softrenderer/math/mat4.h"
#include "softrenderer/math/vec.h"

// ============================================================
// 后处理效果合集
//
// 所有效果统一在 namespace postprocess 下，按功能分区：
//   1. FXAA        — 屏幕空间抗锯齿（8 位 Framebuffer 输入输出）
//   2. HDR 效果    — DOF 景深、径向模糊、暗角、色差（FloatFramebuffer）
//   3. SSAO        — 屏幕空间环境光遮蔽（深度 + 法线 → AO 因子图）
//
// 依赖说明：FXAA 操作 8 位缓冲，其余操作 HDR 浮点缓冲。
// 全部为 inline 函数/类，header-only，无编译期开销。
// ============================================================

namespace sr::postprocess
{

// FloatFramebuffer 已迁入 namespace sr（ADR-001），此处显式引入
using sr::FloatFramebuffer;

// ============================================================
// 分条并行契约（*Rows 行区间变体 + stripRows 切条助手）
//
// 后处理逐像素计算都是行独立的（输出行 y 只读输入缓冲），把整帧按行
// 切成条带交给调用方的调度器（线程池 / 多核裸机调度 / 单线程），即可
// 近线性加速——render 组件本身保持零线程依赖，调度权留给使用者。
//
// 多线程调用约定：
//   - 各线程处理互不重叠的行区间（写入无冲突）；
//   - 读越出区间边界的邻域行是允许的（输入缓冲在整个并行段内只读）；
//   - 需要"先全帧预处理"的效果（FXAA 亮度图、DOF 半径图）把预处理
//     拆成独立的行区间函数，先并行预处理、再并行应用。
// ============================================================

// 把 [0, height) 切成 stripCount 个近似等高的行区间，返回第 stripIndex 条
// 的 [yBegin, yEnd)。height/stripCount 非法或 stripIndex 越界返回 {0,0}。
inline std::pair<int, int> stripRows(int height, int stripIndex, int stripCount)
{
    if (height <= 0 || stripCount <= 0 || stripIndex < 0 || stripIndex >= stripCount)
        return {0, 0};
    const int base = height / stripCount;
    const int extra = height % stripCount;
    const int yBegin = stripIndex * base + std::min(stripIndex, extra);
    const int yEnd = yBegin + base + (stripIndex < extra ? 1 : 0);
    return {yBegin, yEnd};
}

// 行区间统一夹取：负值与越界收进 [0, total)，逆序区间返回 {0,0}。
inline std::pair<int, int> clampRows(int yBegin, int yEnd, int total)
{
    if (total <= 0)
        return {0, 0};
    yBegin = std::clamp(yBegin, 0, total);
    yEnd = std::clamp(yEnd, 0, total);
    if (yBegin >= yEnd)
        return {0, 0};
    return {yBegin, yEnd};
}

// ============================================================
// 1. FXAA — Fast Approximate Anti-Aliasing
// ============================================================
//
// 在最终图像上检测亮度边缘，沿边缘方向做方向性模糊。
// 不需要 MSAA 的多样本缓冲，开销极低（~1ms）。
//
// 原理简化：
//   1. 对每个像素取 3x3 邻域的亮度
//   2. 计算对比度（max - min），太低则跳过（平坦区不需 AA）
//   3. 判断边缘是水平还是垂直方向
//   4. 沿边缘方向搜索两端，确定边缘长度
//   5. 根据搜索距离做亚像素偏移的双线性采样
//
// 参考：Lottes 2011 "FXAA 3.11" 的简化实现。

inline float luminance(float r, float g, float b)
{
    return 0.299f * r + 0.587f * g + 0.114f * b;
}

inline float luminance(Color c)
{
    return luminance(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f);
}

// 以像素中心为原点的双线性采样（坐标超出范围时夹到边缘）。
// FXAA 的偏移量恒在半个像素以内，若把 x+0.5+offset 直接取整，
// 结果会落回原像素——边缘搜索全部白做，画面与输入完全一致。
inline Color sampleBilinearClamped(const Framebuffer& src, float u, float v)
{
    const int w = src.width(), h = src.height();
    if (w <= 0 || h <= 0)
        return Color(0, 0, 0);
    const float fx = std::clamp(u - 0.5f, 0.0f, float(w - 1));
    const float fy = std::clamp(v - 0.5f, 0.0f, float(h - 1));
    const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
    const int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
    const float tx = fx - float(x0), ty = fy - float(y0);
    const Color c0 = Color::lerp(src.getPixel(x0, y0), src.getPixel(x1, y0), tx);
    const Color c1 = Color::lerp(src.getPixel(x0, y1), src.getPixel(x1, y1), tx);
    return Color::lerp(c0, c1, ty);
}

// ---- FXAA 分条变体 ----

// 亮度图行区间预计算：lumMap 必须已按整帧（W*H）分配，各条带只写各自的行段。
inline void buildLuminanceMapRows(const Framebuffer& src, std::vector<float>& lumMap, int yBegin, int yEnd)
{
    const int W = src.width(), H = src.height();
    if (W <= 0 || H <= 0 || lumMap.size() < size_t(W) * size_t(H))
        return;
    const auto [y0, y1] = clampRows(yBegin, yEnd, H);
    const Color* srcData = src.data();
    for (int y = y0; y < y1; ++y)
    {
        const Color* srcRow = srcData + size_t(y) * size_t(W);
        float* lumRow = lumMap.data() + size_t(y) * size_t(W);
        for (int x = 0; x < W; ++x)
            lumRow[x] = luminance(srcRow[x]);
    }
}

// 整帧亮度图（单线程便捷入口；并行调度请按条带调 buildLuminanceMapRows）。
inline void buildLuminanceMap(const Framebuffer& src, std::vector<float>& lumMap)
{
    const int W = src.width(), H = src.height();
    if (W <= 0 || H <= 0)
        return;
    lumMap.resize(size_t(W) * size_t(H));
    buildLuminanceMapRows(src, lumMap, 0, H);
}

// FXAA 行区间应用：只处理 [yBegin, yEnd)，各条带共享同一份只读亮度图。
// 与整帧版逐像素一致：亮度图相同、逐像素计算不携带跨行状态。
inline void fxaaRows(const Framebuffer& src, Framebuffer& dst, const std::vector<float>& lumMap,
                     int yBegin, int yEnd)
{
    const int W = src.width(), H = src.height();
    if (W <= 0 || H <= 0 || lumMap.size() < size_t(W) * size_t(H))
        return;

    // 可调参数
    const float FXAA_EDGE_THRESHOLD = 1.0f / 8.0f;        // 最小对比度
    const float FXAA_EDGE_THRESHOLD_MIN = 1.0f / 128.0f;  // 亮度下限
    const int FXAA_SEARCH_STEPS = 8;                       // 边缘搜索步数

    auto lumAt = [&](int px, int py) {
        const int cx = std::clamp(px, 0, W - 1);
        const int cy = std::clamp(py, 0, H - 1);
        return lumMap[size_t(cy) * size_t(W) + size_t(cx)];
    };

    const Color* srcData = src.data();
    // 先整行拷回去：低对比度像素（绝大多数）根本不需要处理，
    // 没必要为它们走一遍 setPixel。尺寸不一致时只拷交集。
    const int copyW = std::min(W, dst.width());
    const int copyH = std::min(H, dst.height());
    const auto [y0, y1] = clampRows(yBegin, yEnd, copyH);
    for (int y = y0; y < y1; ++y)
        std::copy(srcData + size_t(y) * size_t(W), srcData + size_t(y) * size_t(W) + copyW, dst.row(y));

    for (int y = y0; y < y1; ++y)
    {
        // 行指针一次算好：x 方向的 clamp 只剩 2 次，y 方向完全不用 clamp。
        const float* rowN = lumMap.data() + size_t(std::max(y - 1, 0)) * size_t(W);
        const float* rowM = lumMap.data() + size_t(y) * size_t(W);
        const float* rowS = lumMap.data() + size_t(std::min(y + 1, H - 1)) * size_t(W);
        for (int x = 0; x < copyW; ++x)
        {
            const int xW = x > 0 ? x - 1 : 0;
            const int xE = x + 1 < W ? x + 1 : W - 1;

            const float lM = rowM[x];  // 中心
            const float lN = rowN[x];  // 上
            const float lS = rowS[x];  // 下
            const float lW = rowM[xW]; // 左
            const float lE = rowM[xE]; // 右

            const float lMin = std::min({lM, lN, lS, lW, lE});
            const float lMax = std::max({lM, lN, lS, lW, lE});
            const float lRange = lMax - lMin;

            // 对比度太低 → 保持原像素（已整行拷贝，无需再写）
            if (lRange < std::max(FXAA_EDGE_THRESHOLD_MIN, lMax * FXAA_EDGE_THRESHOLD))
                continue;

            // 确认是边缘后才读 4 个角落：非边缘像素（绝大多数）省掉 4 次读取。
            const float lNW = rowN[xW];
            const float lNE = rowN[xE];
            const float lSW = rowS[xW];
            const float lSE = rowS[xE];

            // 判断边缘方向：比较水平和垂直梯度（二阶导数）
            const float edgeHoriz = std::abs(-2.0f * lW + lNW + lSW) + std::abs(-2.0f * lM + lN + lS) * 2.0f +
                                    std::abs(-2.0f * lE + lNE + lSE);
            const float edgeVert = std::abs(-2.0f * lN + lNW + lNE) + std::abs(-2.0f * lM + lW + lE) * 2.0f +
                                   std::abs(-2.0f * lS + lSW + lSE);
            const bool isHorizontal = edgeHoriz >= edgeVert;

            // 沿边缘方向搜索
            float l1 = isHorizontal ? lN : lW;
            float l2 = isHorizontal ? lS : lE;

            // 确定搜索方向（朝梯度大的方向）
            const float grad1 = std::abs(l1 - lM);
            const float grad2 = std::abs(l2 - lM);
            const int dir1 = (grad1 >= grad2) ? -1 : 1;
            const int dir2 = (grad1 >= grad2) ? 1 : -1;
            if (grad1 < grad2)
            {
                std::swap(l1, l2);
            }

            // 搜索边缘两端
            float searchLen1 = 0, searchLen2 = 0;
            for (int i = 1; i <= FXAA_SEARCH_STEPS; ++i)
            {
                if (searchLen1 == 0)
                {
                    const int sx = x + (isHorizontal ? 0 : dir1 * i);
                    const int sy = y + (isHorizontal ? dir1 * i : 0);
                    const float lEnd = lumAt(sx, sy);
                    const bool done = (dir1 < 0) ? (lEnd <= lMin) : (lEnd >= lMax);
                    if (done)
                        searchLen1 = float(i);
                }
                if (searchLen2 == 0)
                {
                    const int sx = x + (isHorizontal ? 0 : dir2 * i);
                    const int sy = y + (isHorizontal ? dir2 * i : 0);
                    const float lEnd = lumAt(sx, sy);
                    const bool done = (dir2 < 0) ? (lEnd <= lMin) : (lEnd >= lMax);
                    if (done)
                        searchLen2 = float(i);
                }
                if (searchLen1 > 0 && searchLen2 > 0)
                    break;
            }
            if (searchLen1 == 0)
                searchLen1 = float(FXAA_SEARCH_STEPS);
            if (searchLen2 == 0)
                searchLen2 = float(FXAA_SEARCH_STEPS);

            // 亚像素偏移
            float pixelOffset = -0.25f * (1.0f / searchLen1 + 1.0f / searchLen2);
            pixelOffset = std::clamp(pixelOffset, -0.5f, 0.0f);

            // 采样偏移后的颜色（必须双线性，最近邻会把偏移量吃掉）
            const float u = float(x) + 0.5f + (isHorizontal ? 0.0f : pixelOffset);
            const float v = float(y) + 0.5f + (isHorizontal ? pixelOffset : 0.0f);
            dst.setPixel(x, y, sampleBilinearClamped(src, u, v));
        }
    }
}

// 对 Framebuffer 做 FXAA，结果写入 dst（同尺寸）。
// 并行用法：先按条带 buildLuminanceMapRows 填满亮度图，再按条带 fxaaRows。
inline void fxaa(const Framebuffer& src, Framebuffer& dst)
{
    const int W = src.width(), H = src.height();
    if (W <= 0 || H <= 0)
        return;

    // 亮度图预计算。
    // 原实现每个像素要算 9 次亮度（3×3 邻域），而每个亮度又被它周围 9 个像素
    // 各算一次——整体是 9 倍冗余，1080p 下等于 1800 万次 getPixel+luminance。
    // 预计算后每像素 1 次，搜索阶段也变成连续的数组读取。
    std::vector<float> lumMap(size_t(W) * size_t(H));
    buildLuminanceMap(src, lumMap);
    fxaaRows(src, dst, lumMap, 0, H);
}

// ============================================================
// 2. HDR 后处理效果（FloatFramebuffer 输入输出）
// ============================================================
//
// 存储与效果分离：FloatFramebuffer 只负责像素存取，色调映射、亮部提取、
// 模糊与合成都由本模块的自由函数完成，避免缓冲类累积不属于它的算法。

// ---- 色调映射与 LDR 输出 ----

// ACES 近似色调映射：把 HDR [0,∞) 压到 [0,1]，带肩部滚降
inline Vec3f acesToneMap(Vec3f x)
{
    const float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    auto f = [&](float v)
    { return std::clamp((v * (a * v + b)) / (v * (c * v + d) + e), 0.0f, 1.0f); };
    return {f(x.x), f(x.y), f(x.z)};
}

// Gamma 校正（线性 → sRGB 近似）
inline Vec3f gammaCorrect(Vec3f linear)
{
    auto g = [](float v)
    { return std::pow(std::max(v, 0.0f), 1.0f / 2.2f); };
    return {g(linear.x), g(linear.y), g(linear.z)};
}

// 2.2 伽马的查找表：1024 段、按 sqrt(v) 均匀取样。
//
// 为什么不直接调 pow：1080p 全屏是 200 万像素 × 3 通道 = 600 万次 pow，
// 在 MSVC 上约 13 ns/次，单项就占 80 ms。按 sqrt 取样让低值区自动加密，
// 1024 段下最大误差 < 1e-4（8 位下 0.03 LSB），与直接 pow 结果不可区分。
inline const std::array<float, 1025>& gamma22Table()
{
    static const std::array<float, 1025> table = [] {
        std::array<float, 1025> values{};
        for (int i = 0; i <= 1024; ++i)
        {
            const float u = float(i) / 1024.0f;
            values[size_t(i)] = std::pow(u * u, 1.0f / 2.2f); // v = u²
        }
        return values;
    }();
    return table;
}

// 与 gammaCorrect 数值等价（NaN 传播、负数归 0）的快速版本
inline float gammaCorrectFast(float v)
{
    if (v != v)
        return v; // NaN 原样传播，与 pow 版一致
    if (v <= 0.0f)
        return 0.0f;
    if (v >= 1.0f)
        return 1.0f;
    const float scaled = std::sqrt(v) * 1024.0f;
    const int index = int(scaled);
    const int next = index < 1024 ? index + 1 : 1024;
    const float a = gamma22Table()[size_t(index)];
    return a + (gamma22Table()[size_t(next)] - a) * (scaled - float(index));
}

inline Vec3f gammaCorrectFast(Vec3f linear)
{
    return {gammaCorrectFast(linear.x), gammaCorrectFast(linear.y), gammaCorrectFast(linear.z)};
}

// 行区间版：只处理 [yBegin, yEnd)。色调映射查表只读，天然行独立。
inline void resolveToLdrRows(const FloatFramebuffer& src, Framebuffer& dst, int yBegin, int yEnd)
{
    // 行指针直写：全屏 200 万像素上，逐像素 getPixel/setPixel 的边界检查
    // 本身是可观测的开销；尺寸取交集保持“越界静默丢弃”的原语义。
    const int w = std::min(src.width(), dst.width());
    const int h = std::min(src.height(), dst.height());
    const auto [y0, y1] = clampRows(yBegin, yEnd, h);
    for (int y = y0; y < y1; ++y)
    {
        const Vec3f* srcRow = src.row(y);
        Color* dstRow = dst.row(y);
        for (int x = 0; x < w; ++x)
        {
            const Vec3f mapped = gammaCorrectFast(acesToneMap(srcRow[x]));
            dstRow[x] = Color::fromFloat(mapped.x, mapped.y, mapped.z);
        }
    }
}

// 把 HDR 缓冲经色调映射 + gamma 转到 8 位 Framebuffer
inline void resolveToLdr(const FloatFramebuffer& src, Framebuffer& dst)
{
    resolveToLdrRows(src, dst, 0, std::min(src.height(), dst.height()));
}

// ---- Bloom 基础算子 ----

// 行区间版：dst 先由调用方 resize/clear（brightPass 编排这些全帧操作）。
inline void brightPassRows(const FloatFramebuffer& src, FloatFramebuffer& dst, float threshold,
                           int yBegin, int yEnd)
{
    const auto [y0, y1] = clampRows(yBegin, yEnd, src.height());
    const int w = src.width();
    for (int y = y0; y < y1; ++y)
    {
        const Vec3f* srcRow = src.row(y);
        Vec3f* dstRow = dst.row(y);
        for (int x = 0; x < w; ++x)
        {
            const Vec3f c = srcRow[x];
            float lum = 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
            if (lum > threshold)
                dstRow[x] = c;
        }
    }
}

// 亮部提取：只保留亮度 > threshold 的像素（用于 Bloom）
inline void brightPass(const FloatFramebuffer& src, FloatFramebuffer& dst, float threshold)
{
    // 先 resize/clear 再读 src：若两者是同一个缓冲，清空会先把输入抹掉。
    if (&src == &dst)
        return;
    dst.resize(src.width(), src.height());
    dst.clear();
    brightPassRows(src, dst, threshold, 0, src.height());
}

// 行区间版：dst 须已 resize 到与 src 同尺寸。本函数整行覆盖（含垂直 pass
// 的首个核项直接赋值），因此不需要调用方预清空。
inline void gaussianBlurRows(const FloatFramebuffer& src, FloatFramebuffer& dst, bool horizontal,
                             int radius, int yBegin, int yEnd)
{
    if (&src == &dst || src.width() <= 0 || src.height() <= 0)
        return;
    // radius<=0 时 sigma=0 会产生 NaN 核；负 radius 还会让核的 size 回绕成巨值。
    radius = std::clamp(radius, 1, std::max(src.width(), src.height()));
    // 预计算 1D 高斯核（每条带一份，代价相对 O(W×H×radius) 可忽略）
    std::vector<float> kernel(size_t(radius) * 2 + 1);
    float sigma = radius / 3.0f;
    float sum = 0;
    for (int i = -radius; i <= radius; ++i)
    {
        float w = std::exp(-(i * i) / (2 * sigma * sigma));
        kernel[size_t(i + radius)] = w;
        sum += w;
    }
    for (auto& w : kernel)
        w /= sum;

    const int w0 = src.width(), h0 = src.height();
    const Vec3f* srcData = src.data();
    const auto [y0, y1] = clampRows(yBegin, yEnd, h0);
    if (horizontal)
    {
        for (int y = y0; y < y1; ++y)
        {
            const Vec3f* srcRow = srcData + size_t(y) * size_t(w0);
            Vec3f* dstRow = dst.row(y);
            for (int x = 0; x < w0; ++x)
            {
                Vec3f acc{0, 0, 0};
                for (int i = -radius; i <= radius; ++i)
                    acc = acc + srcRow[std::clamp(x + i, 0, w0 - 1)] * kernel[size_t(i + radius)];
                dstRow[x] = acc;
            }
        }
    }
    else
    {
        // 垂直 pass：按核偏移逐行累加，内层在连续内存上循环。
        // 逐像素跨行取 17 次（步长 = 一整行）会把缓存打满，是这条路径的主要瓶颈。
        // 输出行 dstRow[y] 只聚合输入行 [y-r, y+r]，行间写不冲突，可按条带并行。
        // 首个核项用赋值而不是累加：dst 因此无需预清空，少一次全帧写，也消除了
        // 「忘 clear 就得到旧内容叠加」这种不报错只发灰的静默错误。
        for (int y = y0; y < y1; ++y)
        {
            Vec3f* dstRow = dst.row(y);
            for (int i = -radius; i <= radius; ++i)
            {
                const Vec3f* srcRow = srcData + size_t(std::clamp(y + i, 0, h0 - 1)) * size_t(w0);
                const float k = kernel[size_t(i + radius)];
                if (i == -radius)
                {
                    for (int x = 0; x < w0; ++x)
                        dstRow[x] = srcRow[x] * k;
                }
                else
                {
                    for (int x = 0; x < w0; ++x)
                        dstRow[x] = dstRow[x] + srcRow[x] * k;
                }
            }
        }
    }
}

// 可分离高斯模糊（水平或垂直 pass）
inline void gaussianBlur(const FloatFramebuffer& src, FloatFramebuffer& dst, bool horizontal, int radius)
{
    if (&src == &dst || src.width() <= 0 || src.height() <= 0)
        return;
    dst.resize(src.width(), src.height());
    dst.clear();
    gaussianBlurRows(src, dst, horizontal, radius, 0, src.height());
}

// 行区间版：dst += src * intensity 的条带应用。
inline void addBlendRows(FloatFramebuffer& dst, const FloatFramebuffer& src, float intensity,
                         int yBegin, int yEnd)
{
    const int w = std::min(dst.width(), src.width());
    const int h = std::min(dst.height(), src.height());
    const auto [y0, y1] = clampRows(yBegin, yEnd, h);
    for (int y = y0; y < y1; ++y)
    {
        Vec3f* dstRow = dst.row(y);
        const Vec3f* srcRow = src.row(y);
        for (int x = 0; x < w; ++x)
            dstRow[x] = dstRow[x] + srcRow[x] * intensity;
    }
}

// 加法合成：dst += src * intensity（Bloom 叠加）
inline void addBlend(FloatFramebuffer& dst, const FloatFramebuffer& src, float intensity = 1.0f)
{
    addBlendRows(dst, src, intensity, 0, std::min(dst.height(), src.height()));
}

// ---- DOF 景深 ----
// focusDist: 焦点距离（视图空间 z）
// focusRange: 焦点清晰范围
// maxBlur: 最大模糊半径（像素）
//
// 原理：根据深度差异计算 CoC（Circle of Confusion），
// 离焦点越远模糊越大，背景不模糊。模拟相机光圈效果。
//
// 参考：GPU Gems 3 Ch.28 (DOF)
inline void depthOfField(const FloatFramebuffer& color,
                         const std::vector<float>& depth,
                         int W, int H,
                         float focusDist, float focusRange, float maxBlur,
                         FloatFramebuffer& out)
{
    // 尺寸、输入长度与参数都要先校验：下面会按 W/H 直读 color 与 depth，
    // focusRange 参与除法，count 参与取平均。
    if (W <= 0 || H <= 0 || color.width() < W || color.height() < H ||
        depth.size() < size_t(W) * size_t(H))
        return;
    if (!(focusRange > 0.0f) || !std::isfinite(focusRange))
        focusRange = 1e-3f;
    if (!std::isfinite(focusDist))
        return; // 焦点距离非有限时 CoC 无法定义，直接不做处理
    if (!std::isfinite(maxBlur) || maxBlur < 0.0f)
        maxBlur = 0.0f;
    // 半径超过画面尺寸后窗口已经覆盖全图，再大只会白烧 CPU：
    // 不留上界时 maxBlur=1e6 会让单帧循环变成 (2e6)² 次（实测级别的挂死）。
    maxBlur = std::min(maxBlur, float(std::max(W, H)));
    out.resize(W, H);
    if (out.width() != W || out.height() != H)
        return; // 外部存储 out 是定尺寸的：尺寸仍不符说明调用方未按契约准备

    // 语义：每个像素 = 以它自己的 CoC 半径、带 clamp 边界的二维盒式平均。
    //
    // 直接双层循环是 O(r²)/像素。这里按半径分桶：对每个出现的半径 r，
    // 逐行维护「当前行窗口的列和」colSum（增量加新行/减旧行），
    // 再用行内前缀和得到水平窗口。两层 clamp 可交换，因此
    // sum = Σ_{dx} colSum[clamp(x+dx)] 与二维窗口严格相等，
    // 而每像素 O(1)、全程顺序访问（不按列跳着读）。
    // 总代价 O(不同半径数 × W × H)。
    const size_t pixelCount = size_t(W) * size_t(H);
    std::vector<int> radii(pixelCount, 0);
    int maxRadius = 0;
    for (int y = 0; y < H; ++y)
    {
        for (int x = 0; x < W; ++x)
        {
            const size_t idx = size_t(y) * size_t(W) + size_t(x);
            const float d = depth[idx];
            if (!std::isfinite(d) || d >= 1e8f)
                continue; // 背景保持半径 0 = 原像素
            const float coc = std::min(std::abs(d - focusDist) / focusRange, 1.0f);
            const int radius = int(coc * maxBlur);
            radii[idx] = radius;
            maxRadius = std::max(maxRadius, radius);
        }
    }

    auto clampIndex = [](int v, int n) { return v < 0 ? 0 : (v >= n ? n - 1 : v); };

    if (maxRadius == 0)
    {
        // 全画面清晰：直接拷贝，不做任何滤波
        for (int y = 0; y < H; ++y)
        {
            const Vec3f* src = color.row(y);
            Vec3f* dst = out.row(y);
            for (int x = 0; x < W; ++x)
                dst[x] = src[x];
        }
        return;
    }

    std::vector<char> used(size_t(maxRadius) + 1, 0);
    for (int radius : radii)
        used[size_t(radius)] = 1;

    // ---- 快路径：二维积分图（SAT），O(W×H) 与半径数无关 ----
    //
    // 每像素半径不同也没关系：窗口和就是四个 SAT 角点相减。代价是边界 clamp：
    // 越界部分会重复首/末行（列），因此拆成「中间矩形 + 左右列带 + 上下行带 + 四角」。
    // 临时内存 ≈ (W+1)(H+1)×3×8B（1080p 约 50MB），超过阈值时回退到分桶路径。
    const Vec3f* srcData = color.data();
    struct Sat3
    {
        double x = 0.0, y = 0.0, z = 0.0;
    };
    const size_t satStride = size_t(W) + 1;
    const size_t satBytes = satStride * (size_t(H) + 1) * sizeof(Sat3);
    const size_t kSatLimit = size_t(160) * 1024 * 1024;
    // 半径很小时分桶路径只需 R+1 趟全图，比建一张 W×H×24B 的积分图更划算（且省内存）。
    if (satBytes <= kSatLimit && maxRadius >= 4)
    {
        std::vector<Sat3> sat(satStride * (size_t(H) + 1));
        for (size_t i = 0; i < satStride; ++i)
            sat[i] = Sat3{};
        for (int y = 0; y < H; ++y)
        {
            const Vec3f* row = color.row(y);
            Sat3* cur = sat.data() + size_t(y + 1) * satStride;
            const Sat3* prev = sat.data() + size_t(y) * satStride;
            cur[0] = Sat3{};
            for (int x = 0; x < W; ++x)
            {
                const double vx = double(row[x].x), vy = double(row[x].y), vz = double(row[x].z);
                cur[x + 1].x = vx + prev[x + 1].x + cur[x].x - prev[x].x;
                cur[x + 1].y = vy + prev[x + 1].y + cur[x].y - prev[x].y;
                cur[x + 1].z = vz + prev[x + 1].z + cur[x].z - prev[x].z;
            }
        }
        auto at = [&](int x, int y) -> const Sat3& { return sat[size_t(y) * satStride + size_t(x)]; };
        const Sat3 tl{srcData[0].x, srcData[0].y, srcData[0].z};
        const Sat3 bl{srcData[size_t(W) - 1].x, srcData[size_t(W) - 1].y, srcData[size_t(W) - 1].z};
        const Vec3f* lastRow = srcData + size_t(H - 1) * size_t(W);
        const Sat3 tt{lastRow[0].x, lastRow[0].y, lastRow[0].z};
        const Sat3 bt{lastRow[W - 1].x, lastRow[W - 1].y, lastRow[W - 1].z};

        for (int y = 0; y < H; ++y)
        {
            Vec3f* dstRow = out.row(y);
            for (int x = 0; x < W; ++x)
            {
                const int r = radii[size_t(y) * size_t(W) + size_t(x)];
                if (r == 0)
                {
                    dstRow[x] = color.row(y)[x];
                    continue;
                }
                const int xlo = std::max(0, x - r), xhi = std::min(W - 1, x + r);
                const int ylo = std::max(0, y - r), yhi = std::min(H - 1, y + r);
                const double lx = double(std::max(0, r - x)), rx = double(std::max(0, x + r - (W - 1)));
                const double ly = double(std::max(0, r - y)), ry = double(std::max(0, y + r - (H - 1)));

                // 中间矩形：SAT 四角相减（直接用标量，避免三成员结构体的临时拷贝）
                const Sat3& m00 = at(xlo, ylo);
                const Sat3& m01 = at(xlo, yhi + 1);
                const Sat3& m10 = at(xhi + 1, ylo);
                const Sat3& m11 = at(xhi + 1, yhi + 1);
                double sx = m11.x - m01.x - m10.x + m00.x;
                double sy = m11.y - m01.y - m10.y + m00.y;
                double sz = m11.z - m01.z - m10.z + m00.z;

                // 只有靠近边缘的像素才需要补列带/行带（绝大多数像素走这条快路径）
                if (lx != 0.0 || rx != 0.0 || ly != 0.0 || ry != 0.0)
                {
                    if (lx != 0.0)
                    {
                        const Sat3& a0 = at(1, yhi + 1);
                        const Sat3& a1 = at(1, ylo);
                        sx += lx * (a0.x - a1.x);
                        sy += lx * (a0.y - a1.y);
                        sz += lx * (a0.z - a1.z);
                    }
                    if (rx != 0.0)
                    {
                        const Sat3& b0 = at(W, yhi + 1);
                        const Sat3& b1 = at(W - 1, yhi + 1);
                        const Sat3& b2 = at(W, ylo);
                        const Sat3& b3 = at(W - 1, ylo);
                        sx += rx * ((b0.x - b1.x) - (b2.x - b3.x));
                        sy += rx * ((b0.y - b1.y) - (b2.y - b3.y));
                        sz += rx * ((b0.z - b1.z) - (b2.z - b3.z));
                    }
                    if (ly != 0.0)
                    {
                        const Sat3& c0 = at(xhi + 1, 1);
                        const Sat3& c1 = at(xlo, 1);
                        sx += ly * ((c0.x - c1.x) + lx * tl.x + rx * bl.x);
                        sy += ly * ((c0.y - c1.y) + lx * tl.y + rx * bl.y);
                        sz += ly * ((c0.z - c1.z) + lx * tl.z + rx * bl.z);
                    }
                    if (ry != 0.0)
                    {
                        const Sat3& d0 = at(xhi + 1, H);
                        const Sat3& d1 = at(xhi + 1, H - 1);
                        const Sat3& d2 = at(xlo, H);
                        const Sat3& d3 = at(xlo, H - 1);
                        sx += ry * (((d0.x - d1.x) - (d2.x - d3.x)) + lx * tt.x + rx * bt.x);
                        sy += ry * (((d0.y - d1.y) - (d2.y - d3.y)) + lx * tt.y + rx * bt.y);
                        sz += ry * (((d0.z - d1.z) - (d2.z - d3.z)) + lx * tt.z + rx * bt.z);
                    }
                }

                const double taps = double(2 * r + 1);
                const double inv = 1.0 / (taps * taps);
                dstRow[x] = Vec3f{float(sx * inv), float(sy * inv), float(sz * inv)};
            }
        }
        return;
    }

    // 注意：不能写 colX(size_t(W))——那是「函数声明」而不是变量（most vexing parse）。
    std::vector<double> colX(static_cast<size_t>(W)), colY(static_cast<size_t>(W)), colZ(static_cast<size_t>(W));
    std::vector<double> runX(size_t(W) + 1), runY(size_t(W) + 1), runZ(size_t(W) + 1);

    for (int r = 0; r <= maxRadius; ++r)
    {
        if (!used[size_t(r)])
            continue;
        const int taps = 2 * r + 1;
        const float inv = 1.0f / float(taps * taps);

        // 初始化 y=0 的列窗口（clamp 后底部多出的行重复第 0 行）
        std::fill(colX.begin(), colX.end(), 0.0);
        std::fill(colY.begin(), colY.end(), 0.0);
        std::fill(colZ.begin(), colZ.end(), 0.0);
        for (int dy = -r; dy <= r; ++dy)
        {
            const Vec3f* row = srcData + size_t(clampIndex(dy, H)) * size_t(W);
            for (int x = 0; x < W; ++x)
            {
                colX[size_t(x)] += double(row[x].x);
                colY[size_t(x)] += double(row[x].y);
                colZ[size_t(x)] += double(row[x].z);
            }
        }

        for (int y = 0; y < H; ++y)
        {
            const Vec3f* add = srcData + size_t(clampIndex(y + r, H)) * size_t(W);
            const Vec3f* sub = srcData + size_t(clampIndex(y - 1 - r, H)) * size_t(W);
            // 列窗口与行内前缀一趟算完：两者都要遍历整行，分开写等于白跑一遍
            // （1080p、r=48 实测约 1.6s → 0.8s 的差距主要来自这种重复遍历）。
            runX[0] = runY[0] = runZ[0] = 0.0;
            if (y == 0)
            {
                for (int x = 0; x < W; ++x)
                {
                    runX[size_t(x) + 1] = runX[size_t(x)] + colX[size_t(x)];
                    runY[size_t(x) + 1] = runY[size_t(x)] + colY[size_t(x)];
                    runZ[size_t(x) + 1] = runZ[size_t(x)] + colZ[size_t(x)];
                }
            }
            else
            {
                for (int x = 0; x < W; ++x)
                {
                    const double nx = colX[size_t(x)] + double(add[x].x) - double(sub[x].x);
                    const double ny = colY[size_t(x)] + double(add[x].y) - double(sub[x].y);
                    const double nz = colZ[size_t(x)] + double(add[x].z) - double(sub[x].z);
                    colX[size_t(x)] = nx;
                    colY[size_t(x)] = ny;
                    colZ[size_t(x)] = nz;
                    runX[size_t(x) + 1] = runX[size_t(x)] + nx;
                    runY[size_t(x) + 1] = runY[size_t(x)] + ny;
                    runZ[size_t(x) + 1] = runZ[size_t(x)] + nz;
                }
            }

            Vec3f* dstRow = out.row(y);
            for (int x = 0; x < W; ++x)
            {
                if (radii[size_t(y) * size_t(W) + size_t(x)] != r)
                    continue;
                const int lo = clampIndex(x - r, W);
                const int hi = clampIndex(x + r, W);
                const double leftPad = double(lo - (x - r));
                const double rightPad = double((x + r) - hi);
                dstRow[x] = Vec3f{
                    float((runX[size_t(hi) + 1] - runX[size_t(lo)]) + leftPad * colX[0] + rightPad * colX[size_t(W) - 1]),
                    float((runY[size_t(hi) + 1] - runY[size_t(lo)]) + leftPad * colY[0] + rightPad * colY[size_t(W) - 1]),
                    float((runZ[size_t(hi) + 1] - runZ[size_t(lo)]) + leftPad * colZ[0] + rightPad * colZ[size_t(W) - 1])} *
                    inv;
            }
        }
    }
}

// 行区间版：只处理 [yBegin, yEnd)。
inline void radialMotionBlurRows(const FloatFramebuffer& color, int W, int H,
                                 float strength, int samples,
                                 FloatFramebuffer& out, int yBegin, int yEnd)
{
    // samples=1 时 (s / (samples-1)) 是 0/0；输出尺寸不匹配时 setPixel 越界。
    if (W <= 0 || H <= 0 || samples < 1 || color.width() < W || color.height() < H)
        return;
    // strength 参与 int(x + dx*t*W)：非有限值会让浮点转整成为未定义行为。
    if (!std::isfinite(strength))
        return;
    if (out.width() < W || out.height() < H)
        return; // 行区间版不重建输出：尺寸由调用方保证（整帧版负责 resize）
    const float denom = float(std::max(1, samples - 1));
    const int safeSamples = std::clamp(samples, 1, 256);
    const float invSamples = 1.0f / float(safeSamples);
    float cx = W * 0.5f, cy = H * 0.5f;
    const auto [y0, y1] = clampRows(yBegin, yEnd, H);
    for (int y = y0; y < y1; ++y)
    {
        Vec3f* dstRow = out.row(y);
        for (int x = 0; x < W; ++x)
        {
            float dx = (x - cx) / float(W);
            float dy = (y - cy) / float(H);
            float dist = std::sqrt(dx * dx + dy * dy);
            float blur = dist * strength;

            Vec3f sum{0, 0, 0};
            for (int s = 0; s < safeSamples; ++s)
            {
                float t = (float(s) / denom - 0.5f) * blur;
                int sx = std::clamp(int(x + dx * t * W), 0, W - 1);
                int sy = std::clamp(int(y + dy * t * H), 0, H - 1);
                // sx/sy 已 clamp，且入口已校验 color 尺寸 >= W/H，
                // 因此这里用行指针，省掉每样本一次乘法索引与边界比较。
                sum = sum + color.row(sy)[sx];
            }
            dstRow[x] = sum * invSamples;
        }
    }
}

// ---- 径向运动模糊 ----
// 以屏幕中心为原点，沿径向方向模糊，模拟相机前进的运动感。
// strength: 0~1，samples: 采样数
inline void radialMotionBlur(const FloatFramebuffer& color,
                             int W, int H,
                             float strength, int samples,
                             FloatFramebuffer& out)
{
    // 保持原契约：先按 W×H 重建输出（清零），再整帧计算。
    if (W <= 0 || H <= 0 || samples < 1 || color.width() < W || color.height() < H)
        return;
    if (!std::isfinite(strength))
        return;
    out.resize(W, H);
    radialMotionBlurRows(color, W, H, strength, samples, out, 0, H);
}

// 行区间版：只处理 [yBegin, yEnd)。行指针直写替代逐像素 getPixel/setPixel。
inline void vignetteRows(FloatFramebuffer& fb, float strength, int yBegin, int yEnd)
{
    int W = fb.width(), H = fb.height();
    float cx = W * 0.5f, cy = H * 0.5f;
    float maxDist = std::sqrt(cx * cx + cy * cy);
    const auto [y0, y1] = clampRows(yBegin, yEnd, H);
    for (int y = y0; y < y1; ++y)
    {
        Vec3f* row = fb.row(y);
        for (int x = 0; x < W; ++x)
        {
            const float nx = (x - cx) / maxDist;
            const float ny = (y - cy) / maxDist;
            // 归一化半径的平方。旧写法先算 sqrt 再用 d*d 平方回去，
            // 开方白算了一次；直接取平方与数学上恒等且少一次 sqrt。
            const float r2 = nx * nx + ny * ny;
            float factor = std::max(1.0f - r2 * strength, 0.0f);
            row[x] = row[x] * factor;
        }
    }
}

// ---- 暗角 ----
// 屏幕边缘变暗，模拟镜头光学特性。strength: 0~1
inline void vignette(FloatFramebuffer& fb, float strength = 0.3f)
{
    vignetteRows(fb, strength, 0, fb.height());
}

// ---- 色差 ----
// R/B 通道向屏幕边缘反向偏移，模拟镜头色散。offset: 偏移像素数
inline void chromaticAberration(const FloatFramebuffer& src,
                                int W, int H, float offset,
                                FloatFramebuffer& out)
{
    // offset 参与 int(x + dx * offset)，NaN/Inf 会让转换成为未定义行为。
    if (!std::isfinite(offset) || W <= 0 || H <= 0)
        return;
    // W/H 是尺寸参数而不是像素来源：用调用方传的值直接索引 src.getPixel，
    // 在它与实际尺寸不一致时会越界读；输出也不会被重建，只会写出左上角一块。
    // 以 src 的实际尺寸为准，并按该尺寸重建输出。
    const int srcW = src.width(), srcH = src.height();
    if (srcW <= 0 || srcH <= 0)
        return;
    W = srcW;
    H = srcH;
    if (out.width() != W || out.height() != H)
        out.resize(W, H);
    float cx = W * 0.5f, cy = H * 0.5f;
    for (int y = 0; y < H; ++y)
    {
        for (int x = 0; x < W; ++x)
        {
            float dx = (x - cx) / float(W);
            float dy = (y - cy) / float(H);
            // R 通道向外偏移，B 通道向内偏移
            int rx = std::clamp(int(x + dx * offset), 0, W - 1);
            int ry = std::clamp(int(y + dy * offset), 0, H - 1);
            int bx = std::clamp(int(x - dx * offset), 0, W - 1);
            int by = std::clamp(int(y - dy * offset), 0, H - 1);
            Vec3f cr = src.getPixel(rx, ry);
            Vec3f cg = src.getPixel(x, y);
            Vec3f cb = src.getPixel(bx, by);
            out.setPixel(x, y, Vec3f{cr.x, cg.y, cb.z});
        }
    }
}

// ---- Bloom 整链（半分辨率，可分条并行）----
//
// 亮部提取(全分辨率) → 2×2 box 降采样 → 高斯 H/V(半分辨率) → 双线性升采样
// → dst = src + bloom × intensity。
// Bloom 是低频信号：模糊核半径超过 2 像素后，半分辨率计算 + 升采样与
// 全分辨率链视觉不可分，但逐像素工作量降到约 1/4（48 号文路线 B）。
//
// 编排分段（供线程池分条；单线程直接用下面的 bloom() 整链入口）：
//   1. scratch.half / scratch.blurH 先按半分辨率 resize + clear（全帧一次）
//   2. bloomPrepareRows  按 src 的行条带（[0,H)）填 half —— 行独立
//   3. gaussianBlurRows(half→blurH, 水平) 与 (blurH→half, 垂直) 按 [0,hh) 条带
//      （两个 pass 都整行覆盖，dst 无需预清空）
//   4. bloomApplyRows    按 src 的行条带写 dst —— 行独立
//
// 契约：src 与 dst 不得为同一缓冲（合成要读 src）；dst 尺寸不匹配时整链入口
// 会 resize（外部存储 dst 是定尺寸的，须由调用方保证与 src 同尺寸）。
// 临时缓冲在 BloomScratch 内分配（宿主便利路径）；裸机/免堆场景请用
// brightPass/gaussianBlur/addBlend 的 *Rows 算子自行编排外部存储。
struct BloomParams
{
    float threshold = 1.0f;  // 亮度阈值
    float intensity = 0.8f;  // 叠加强度
    int blurRadius = 12;     // 全分辨率口径的高斯半径（内部折半使用）
};

struct BloomScratch
{
    FloatFramebuffer half;   // 亮部 + 降采样结果，也是垂直模糊后的最终 bloom 图
    FloatFramebuffer blurH;  // 水平模糊临时

    void resizeFor(int W, int H)
    {
        const int hw = std::max(1, W / 2), hh = std::max(1, H / 2);
        if (half.width() != hw || half.height() != hh)
            half.resize(hw, hh);
        if (blurH.width() != hw || blurH.height() != hh)
            blurH.resize(hw, hh);
    }
};

// 半分辨率口径的高斯半径（外部编排调 gaussianBlurRows 时用同一式子）
inline int bloomHalfRadius(const BloomParams& p, int halfWidth, int halfHeight)
{
    return std::clamp(std::max(1, p.blurRadius / 2), 1, std::max(halfWidth, halfHeight));
}

// 步骤 2：亮部提取 + 2×2 平均降采样（一趟完成），只写 [yBegin, yEnd) 的 half 行。
inline void bloomPrepareRows(const FloatFramebuffer& src, BloomScratch& scratch,
                             const BloomParams& params, int yBegin, int yEnd)
{
    const int W = src.width(), H = src.height();
    const int hw = scratch.half.width(), hh = scratch.half.height();
    if (W <= 0 || H <= 0 || hw <= 0 || hh <= 0)
        return;
    if (hw != std::max(1, W / 2) || hh != std::max(1, H / 2))
        return; // scratch 与 src 不匹配：编排层责任，静默丢弃与整帧版一致
    const auto [y0, y1] = clampRows(yBegin, yEnd, hh);
    for (int y = y0; y < y1; ++y)
    {
        Vec3f* dstRow = scratch.half.row(y);
        for (int x = 0; x < hw; ++x)
        {
            const int x0 = std::min(2 * x, W - 1), x1 = std::min(2 * x + 1, W - 1);
            const int yy0 = std::min(2 * y, H - 1), yy1 = std::min(2 * y + 1, H - 1);
            Vec3f acc = src.row(yy0)[x0] + src.row(yy0)[x1] + src.row(yy1)[x0] + src.row(yy1)[x1];
            acc = acc * 0.25f;
            const float lum = 0.2126f * acc.x + 0.7152f * acc.y + 0.0722f * acc.z;
            dstRow[x] = lum > params.threshold ? acc : Vec3f{0, 0, 0};
        }
    }
}

// 步骤 4：双线性升采样 + 合成，只处理 [yBegin, yEnd) 的 dst 行。
// bloomHalf 即 scratch.half（垂直模糊完成后）。
inline void bloomApplyRows(const FloatFramebuffer& src, const FloatFramebuffer& bloomHalf,
                           FloatFramebuffer& dst, const BloomParams& params, int yBegin, int yEnd)
{
    const int W = src.width(), H = src.height();
    const int hw = bloomHalf.width(), hh = bloomHalf.height();
    if (W <= 0 || H <= 0 || hw != std::max(1, W / 2) || hh != std::max(1, H / 2))
        return;
    const float scaleX = float(hw) / float(W);
    const float scaleY = float(hh) / float(H);
    const auto [y0, y1] = clampRows(yBegin, yEnd, H);
    for (int y = y0; y < y1; ++y)
    {
        const float fy = std::clamp((float(y) + 0.5f) * scaleY - 0.5f, 0.0f, float(hh - 1));
        const int yy0 = int(fy);
        const int yy1 = std::min(yy0 + 1, hh - 1);
        const float ty = fy - float(yy0);
        const Vec3f* rowA = bloomHalf.row(yy0);
        const Vec3f* rowB = bloomHalf.row(yy1);
        Vec3f* dstRow = dst.row(y);
        const Vec3f* srcRow = src.row(y);
        for (int x = 0; x < W; ++x)
        {
            const float fx = std::clamp((float(x) + 0.5f) * scaleX - 0.5f, 0.0f, float(hw - 1));
            const int x0 = int(fx);
            const int x1 = std::min(x0 + 1, hw - 1);
            const float tx = fx - float(x0);
            const Vec3f a = rowA[x0] + (rowA[x1] - rowA[x0]) * tx;
            const Vec3f b = rowB[x0] + (rowB[x1] - rowB[x0]) * tx;
            dstRow[x] = srcRow[x] + (a + (b - a) * ty) * params.intensity;
        }
    }
}

// 整链入口（单线程）：分段的参考编排，与分条并行结果逐像素一致。
inline void bloom(const FloatFramebuffer& src, FloatFramebuffer& dst, const BloomParams& params = {})
{
    if (&src == &dst || src.width() <= 0 || src.height() <= 0)
        return;
    const int W = src.width(), H = src.height();
    if (dst.width() != W || dst.height() != H)
        dst.resize(W, H);
    if (dst.width() != W || dst.height() != H)
        return; // 外部存储 dst 是定尺寸的：尺寸仍不符说明调用方未按契约准备

    BloomScratch scratch;
    scratch.resizeFor(W, H);
    scratch.half.clear();
    bloomPrepareRows(src, scratch, params, 0, scratch.half.height());

    const int radius = bloomHalfRadius(params, scratch.half.width(), scratch.half.height());
    scratch.blurH.clear();
    gaussianBlurRows(scratch.half, scratch.blurH, true, radius, 0, scratch.half.height());
    scratch.half.clear();
    gaussianBlurRows(scratch.blurH, scratch.half, false, radius, 0, scratch.half.height());

    bloomApplyRows(src, scratch.half, dst, params, 0, H);
}

// ============================================================
// 3. SSAO — Screen-Space Ambient Occlusion
// ============================================================
//
// 对每个像素，在法线半球内随机撒采样点，把每个采样点
// 投影到屏幕空间查深度——如果采样点比该处实际深度更"深入"
// 场景（被遮挡），则计为遮挡。遮挡比例越高，AO 越暗。
//
// 输入：深度缓冲 + 法线缓冲（视图空间）+ 视图/投影矩阵
// 输出：AO 因子图 [0,1]（1=无遮挡，0=全遮挡）
//
// 优化：用低分辨率噪声纹理旋转采样核，减少方向偏差。
//
// 参考：CryEngine 3 / LearnOpenGL SSAO 教程。

struct SSAOConfig
{
    int sampleCount = 16;  // 半球采样数
    float radius = 0.5f;   // 采样半径（视图空间）
    float bias = 0.025f;   // 深度比较偏移（防自遮挡）
    int noiseSize = 4;     // 噪声纹理尺寸
};

class SSAO
{
   public:
    SSAO()
    {
        config_ = sanitize(SSAOConfig{});
        generateKernel();
        generateNoise();
    }

    // 配置变更必须同步重建依赖数组：噪声数组尺寸由 noiseSize 决定，
    // 只改 config_ 会让 compute() 按新尺寸索引旧数组。
    void configure(const SSAOConfig& cfg)
    {
        config_ = sanitize(cfg);
        generateKernel();
        generateNoise();
    }

    // 行区间版：只计算 [yBegin, yEnd) 行。输入与 aoOut 均为整帧尺寸；
    // aoOut 须先由调用方填满 1.0（compute 编排），条带间写不冲突。
    void computeRows(const std::vector<Vec3f>& viewPos, const std::vector<Vec3f>& viewNormal,
                     const std::vector<float>& depth, int W, int H, const Mat4& viewProj,
                     std::vector<float>& aoOut, int yBegin, int yEnd)
    {
        if (W <= 0 || H <= 0)
            return;
        const size_t pixelCount = size_t(W) * size_t(H);
        // 三路输入长度不足时下面的逐像素读取会越界。
        if (viewPos.size() < pixelCount || viewNormal.size() < pixelCount || depth.size() < pixelCount)
            return;
        if (aoOut.size() < pixelCount)
            return;
        const size_t noiseCount = size_t(config_.noiseSize) * size_t(config_.noiseSize);
        if (kernel_.empty() || noise_.size() < noiseCount)
            return;
        const float* depthData = depth.data();
        int noiseScale_x = W / config_.noiseSize;
        int noiseScale_y = H / config_.noiseSize;
        const auto [y0, y1] = clampRows(yBegin, yEnd, H);

        for (int y = y0; y < y1; ++y)
        {
            for (int x = 0; x < W; ++x)
            {
                size_t idx = size_t(y) * W + x;
                float d = depthData[idx];
                if (d >= 0.999f)
                    continue; // 背景跳过

                Vec3f pos = viewPos[idx];
                Vec3f normal = viewNormal[idx];
                if (normal.lengthSq() < 1e-6f)
                    continue;
                normal = normal.normalized();

                // 随机旋转核（用噪声纹理）
                int nx = noiseScale_x > 0 ? (x / noiseScale_x) % config_.noiseSize : 0;
                int ny = noiseScale_y > 0 ? (y / noiseScale_y) % config_.noiseSize : 0;
                Vec3f randomVec = noise_[size_t(ny) * config_.noiseSize + nx];

                // 构造 TBN（Gram-Schmidt）
                Vec3f tangent = (randomVec - normal * dot(randomVec, normal));
                if (tangent.lengthSq() < 1e-8f)
                    tangent = Vec3f{1, 0, 0};
                tangent = tangent.normalized();
                Vec3f bitangent = cross(normal, tangent);

                float occlusion = 0.0f;
                int samples = std::min(config_.sampleCount, int(kernel_.size()));
                for (int s = 0; s < samples; ++s)
                {
                    // 采样点在视图空间
                    Vec3f sampleDir = tangent * kernel_[s].x + bitangent * kernel_[s].y + normal * kernel_[s].z;
                    Vec3f samplePos = pos + sampleDir * config_.radius;

                    // 投影到屏幕空间。除法改成乘倒数：1080p × 16 样本是 3300 万次，
                    // 每次省两次除法在这个量级上是可观的。
                    const Vec4f clip = viewProj * Vec4f(samplePos, 1.0f);
                    if (clip.w <= 0)
                    {
                        occlusion += 1.0f;
                        continue;
                    }
                    const float invW = 1.0f / clip.w;
                    const int sx = int((clip.x * invW * 0.5f + 0.5f) * W);
                    const int sy = int((clip.y * invW * 0.5f + 0.5f) * H);
                    if (sx < 0 || sx >= W || sy < 0 || sy >= H)
                        continue;

                    const float sampleDepth = depthData[size_t(sy) * W + sx];
                    const float ndcZ = clip.z * invW * 0.5f + 0.5f;
                    const float rangeCheck = std::abs(d - sampleDepth) < config_.radius ? 1.0f : 0.0f;
                    occlusion += (sampleDepth <= ndcZ - config_.bias ? 1.0f : 0.0f) * rangeCheck;
                }
                aoOut[idx] = 1.0f - (occlusion / float(samples));
            }
        }
    }

    // 生成 AO 图
    // viewPos: 每像素的视图空间位置 (x,y,z)
    // viewNormal: 每像素的视图空间法线
    // depth: 深度缓冲（NDC [0,1]）
    // viewProj: 视图*投影矩阵（用于把采样点投回屏幕）
    // 并行用法：先 aoOut.assign(N, 1.0f)，再按条带调 computeRows。
    void compute(const std::vector<Vec3f>& viewPos, const std::vector<Vec3f>& viewNormal,
                 const std::vector<float>& depth, int W, int H, const Mat4& viewProj,
                 std::vector<float>& aoOut)
    {
        aoOut.clear();
        if (W <= 0 || H <= 0)
            return;
        const size_t pixelCount = size_t(W) * size_t(H);
        // 每帧重新填满：resize 在尺寸不变时不会重置，被 continue 跳过的像素
        // 会残留上一帧的 AO。
        aoOut.assign(pixelCount, 1.0f);
        computeRows(viewPos, viewNormal, depth, W, H, viewProj, aoOut, 0, H);
    }

    // 半分辨率 SSAO：AO 是低频信号，半分辨率计算 + 双线性上采样后
    // 视觉上与全分辨率几乎不可分辨，但样本数降到 1/4。
    // 1080p / 16 样本：全分辨率 727 ms，半分辨率约 1/4。
    void computeHalfResolution(const std::vector<Vec3f>& viewPos, const std::vector<Vec3f>& viewNormal,
                               const std::vector<float>& depth, int W, int H, const Mat4& viewProj,
                               std::vector<float>& aoOut)
    {
        aoOut.clear();
        if (W <= 0 || H <= 0)
            return;
        const size_t pixelCount = size_t(W) * size_t(H);
        if (viewPos.size() < pixelCount || viewNormal.size() < pixelCount || depth.size() < pixelCount)
            return;

        const int hw = std::max(1, W / 2);
        const int hh = std::max(1, H / 2);
        std::vector<Vec3f> halfPos(size_t(hw) * size_t(hh));
        std::vector<Vec3f> halfNormal(size_t(hw) * size_t(hh));
        std::vector<float> halfDepth(size_t(hw) * size_t(hh));
        for (int y = 0; y < hh; ++y)
            for (int x = 0; x < hw; ++x)
            {
                // 取 2×2 的代表像素：平均会把法线拉平，反而丢遮蔽细节
                const size_t src = size_t(std::min(2 * y, H - 1)) * size_t(W) + size_t(std::min(2 * x, W - 1));
                const size_t dst = size_t(y) * size_t(hw) + size_t(x);
                halfPos[dst] = viewPos[src];
                halfNormal[dst] = viewNormal[src];
                halfDepth[dst] = depth[src];
            }

        std::vector<float> halfAo;
        compute(halfPos, halfNormal, halfDepth, hw, hh, viewProj, halfAo);
        if (halfAo.size() != size_t(hw) * size_t(hh))
            return;

        // 双线性上采样回全分辨率（AO 在 [0,1] 内，插值不会超界）
        aoOut.assign(pixelCount, 1.0f);
        const float scaleX = float(hw) / float(W);
        const float scaleY = float(hh) / float(H);
        for (int y = 0; y < H; ++y)
        {
            const float fy = std::clamp((float(y) + 0.5f) * scaleY - 0.5f, 0.0f, float(hh - 1));
            const int y0 = int(fy);
            const int y1 = std::min(y0 + 1, hh - 1);
            const float ty = fy - float(y0);
            for (int x = 0; x < W; ++x)
            {
                const float fx = std::clamp((float(x) + 0.5f) * scaleX - 0.5f, 0.0f, float(hw - 1));
                const int x0 = int(fx);
                const int x1 = std::min(x0 + 1, hw - 1);
                const float tx = fx - float(x0);
                const float a = halfAo[size_t(y0) * size_t(hw) + size_t(x0)];
                const float b = halfAo[size_t(y0) * size_t(hw) + size_t(x1)];
                const float c = halfAo[size_t(y1) * size_t(hw) + size_t(x0)];
                const float d = halfAo[size_t(y1) * size_t(hw) + size_t(x1)];
                aoOut[size_t(y) * size_t(W) + size_t(x)] = (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
            }
        }
    }

    // 对 AO 图做简单 box blur 降噪（行区间版：条带间写不冲突，可并行）
    static void blurRows(const std::vector<float>& aoIn, int W, int H,
                         std::vector<float>& aoOut, int radius, int yBegin, int yEnd)
    {
        if (W <= 0 || H <= 0)
            return;
        const size_t pixelCount = size_t(W) * size_t(H);
        if (aoIn.size() < pixelCount || aoOut.size() < pixelCount)
            return;
        radius = std::clamp(radius, 0, std::max(W, H));
        const auto [y0, y1] = clampRows(yBegin, yEnd, H);
        for (int y = y0; y < y1; ++y)
        {
            for (int x = 0; x < W; ++x)
            {
                float sum = 0;
                int count = 0;
                for (int dy = -radius; dy <= radius; ++dy)
                    for (int dx = -radius; dx <= radius; ++dx)
                    {
                        int nx = std::clamp(x + dx, 0, W - 1);
                        int ny = std::clamp(y + dy, 0, H - 1);
                        sum += aoIn[size_t(ny) * W + nx];
                        ++count;
                    }
                aoOut[size_t(y) * W + x] = sum / count;
            }
        }
    }

    // 对 AO 图做简单 box blur 降噪
    static void blur(const std::vector<float>& aoIn, int W, int H,
                     std::vector<float>& aoOut, int radius = 2)
    {
        aoOut.clear();
        if (W <= 0 || H <= 0)
            return;
        aoOut.assign(size_t(W) * size_t(H), 0.0f);
        blurRows(aoIn, W, H, aoOut, radius, 0, H);
    }

   private:
    SSAOConfig config_;
    std::vector<Vec3f> kernel_; // 半球采样核
    std::vector<Vec3f> noise_;  // noiseSize x noiseSize 随机旋转向量

    // 参数在此集中收敛：sampleCount=0 会让 occlusion/samples 无效，
    // noiseSize=0 会让 W/noiseSize 整数除零。
    static SSAOConfig sanitize(const SSAOConfig& cfg)
    {
        SSAOConfig out = cfg;
        out.sampleCount = std::clamp(cfg.sampleCount, 1, 256);
        out.noiseSize = std::clamp(cfg.noiseSize, 1, 256);
        out.radius = (std::isfinite(cfg.radius) && cfg.radius > 0.0f) ? cfg.radius : 0.5f;
        out.bias = std::isfinite(cfg.bias) ? cfg.bias : 0.0f;
        return out;
    }

    void generateKernel()
    {
        kernel_.resize(config_.sampleCount);
        std::mt19937 rng(42);
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        for (int i = 0; i < config_.sampleCount; ++i)
        {
            Vec3f sample{dist(rng) * 2.0f - 1.0f, dist(rng) * 2.0f - 1.0f, dist(rng)};
            sample = sample.normalized();
            sample = sample * dist(rng);
            // 加速分布：靠近中心密、远处疏
            float scale = float(i) / float(config_.sampleCount);
            scale = 0.1f + scale * scale * 0.9f;
            kernel_[i] = sample * scale;
        }
    }

    void generateNoise()
    {
        noise_.resize(size_t(config_.noiseSize) * config_.noiseSize);
        std::mt19937 rng(123);
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        for (auto& n : noise_)
            n = Vec3f{dist(rng), dist(rng), 0.0f}.normalized();
    }
};

} // namespace sr::postprocess
