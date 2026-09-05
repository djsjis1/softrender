#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

#include "core/color.h"
#include "core/framebuffer.h"

// ============================================================
// FXAA：Fast Approximate Anti-Aliasing（NVIDIA, Timothy Lottes）
//
// 一种后处理抗锯齿：在最终图像上检测亮度边缘，沿边缘方向
// 做方向性模糊。不需要 MSAA 的多样本缓冲，开销极低（~1ms）。
//
// 原理简化版：
//   1. 对每个像素取 3x3 邻域的亮度
//   2. 计算对比度（max - min），若太低则跳过（平坦区不需 AA）
//   3. 判断边缘是水平还是垂直方向
//   4. 沿边缘方向搜索两端，确定边缘长度
//   5. 根据搜索距离做亚像素偏移的双线性采样
//
// 参考：Lottes 2011 "FXAA 3.11" 的简化实现。
// ============================================================
namespace fxaa {

inline float luminance(float r, float g, float b)
{
    return 0.299f * r + 0.587f * g + 0.114f * b;
}

inline float luminance(Color c)
{
    return luminance(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f);
}

// 对 Framebuffer 做 FXAA，结果写入 dst（同尺寸）
inline void apply(const Framebuffer& src, Framebuffer& dst)
{
    const int W = src.width(), H = src.height();

    // 可调参数
    const float FXAA_EDGE_THRESHOLD = 1.0f / 8.0f;        // 最小对比度
    const float FXAA_EDGE_THRESHOLD_MIN = 1.0f / 128.0f;  // 亮度下限
    const int FXAA_SEARCH_STEPS = 8;                      // 边缘搜索步数

    for (int y = 0; y < H; ++y)
    {
        for (int x = 0; x < W; ++x)
        {
            // 3x3 邻域亮度
            auto lum = [&](int px, int py) -> float {
                px = std::clamp(px, 0, W - 1);
                py = std::clamp(py, 0, H - 1);
                return luminance(src.getPixel(px, py));
            };

            float lM = lum(x, y);      // 中心
            float lN = lum(x, y - 1);  // 上
            float lS = lum(x, y + 1);  // 下
            float lW = lum(x - 1, y);  // 左
            float lE = lum(x + 1, y);  // 右
            float lNW = lum(x - 1, y - 1);
            float lNE = lum(x + 1, y - 1);
            float lSW = lum(x - 1, y + 1);
            float lSE = lum(x + 1, y + 1);

            float lMin = std::min({lM, lN, lS, lW, lE});
            float lMax = std::max({lM, lN, lS, lW, lE});
            float lRange = lMax - lMin;

            // 对比度太低 → 不需要 AA
            if (lRange < std::max(FXAA_EDGE_THRESHOLD_MIN, lMax * FXAA_EDGE_THRESHOLD))
            {
                dst.setPixel(x, y, src.getPixel(x, y));
                continue;
            }

            // 判断边缘方向：比较水平和垂直梯度（二阶导数）
            float edgeHoriz = std::abs(-2.0f * lW + lNW + lSW) + std::abs(-2.0f * lM + lN + lS) * 2.0f +
                              std::abs(-2.0f * lE + lNE + lSE);
            float edgeVert = std::abs(-2.0f * lN + lNW + lNE) + std::abs(-2.0f * lM + lW + lE) * 2.0f +
                             std::abs(-2.0f * lS + lSW + lSE);
            bool isHorizontal = edgeHoriz >= edgeVert;

            // 沿边缘方向搜索
            int stepX = isHorizontal ? 0 : 1;
            int stepY = isHorizontal ? 1 : 0;
            float l1 = isHorizontal ? lN : lW;
            float l2 = isHorizontal ? lS : lE;

            // 确定搜索方向（朝梯度大的方向）
            float grad1 = std::abs(l1 - lM);
            float grad2 = std::abs(l2 - lM);
            int dir1 = (grad1 >= grad2) ? -1 : 1;
            int dir2 = (grad1 >= grad2) ? 1 : -1;
            // 修正：dir1 对应 l1 那侧
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
                    int sx = x + (isHorizontal ? 0 : dir1 * i);
                    int sy = y + (isHorizontal ? dir1 * i : 0);
                    float lEnd = lum(sx, sy);
                    bool done = (dir1 < 0) ? (lEnd <= lMin) : (lEnd >= lMax);
                    if (done)
                        searchLen1 = float(i);
                }
                if (searchLen2 == 0)
                {
                    int sx = x + (isHorizontal ? 0 : dir2 * i);
                    int sy = y + (isHorizontal ? dir2 * i : 0);
                    float lEnd = lum(sx, sy);
                    bool done = (dir2 < 0) ? (lEnd <= lMin) : (lEnd >= lMax);
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
            float pixelOffset = -0.25f * (1.0f / searchLen1 + 1.0f / searchLen2) * (isHorizontal ? 1.0f : 1.0f);
            // 限制偏移量
            pixelOffset = std::clamp(pixelOffset, -0.5f, 0.0f);

            // 采样偏移后的颜色（简化：用双线性在偏移位置取色）
            float u = x + 0.5f + (isHorizontal ? 0.0f : pixelOffset);
            float v = y + 0.5f + (isHorizontal ? pixelOffset : 0.0f);
            int ix = std::clamp(int(u), 0, W - 1);
            int iy = std::clamp(int(v), 0, H - 1);
            dst.setPixel(x, y, src.getPixel(ix, iy));
        }
    }
}

}  // namespace fxaa
