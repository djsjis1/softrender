#pragma once
#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include "core/color.h"
#include "core/float_framebuffer.h"
#include "core/framebuffer.h"
#include "math/mat4.h"
#include "math/vec.h"

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

namespace postprocess
{

// FloatFramebuffer 已迁入 namespace sr（ADR-001），此处显式引入
using sr::FloatFramebuffer;

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

// 对 Framebuffer 做 FXAA，结果写入 dst（同尺寸）
inline void fxaa(const Framebuffer& src, Framebuffer& dst)
{
    const int W = src.width(), H = src.height();

    // 可调参数
    const float FXAA_EDGE_THRESHOLD = 1.0f / 8.0f;        // 最小对比度
    const float FXAA_EDGE_THRESHOLD_MIN = 1.0f / 128.0f;  // 亮度下限
    const int FXAA_SEARCH_STEPS = 8;                       // 边缘搜索步数

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
            float pixelOffset = -0.25f * (1.0f / searchLen1 + 1.0f / searchLen2);
            pixelOffset = std::clamp(pixelOffset, -0.5f, 0.0f);

            // 采样偏移后的颜色
            float u = x + 0.5f + (isHorizontal ? 0.0f : pixelOffset);
            float v = y + 0.5f + (isHorizontal ? pixelOffset : 0.0f);
            int ix = std::clamp(int(u), 0, W - 1);
            int iy = std::clamp(int(v), 0, H - 1);
            dst.setPixel(x, y, src.getPixel(ix, iy));
        }
    }
}

// ============================================================
// 2. HDR 后处理效果（FloatFramebuffer 输入输出）
// ============================================================

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
    out.resize(W, H);
    for (int y = 0; y < H; ++y)
    {
        for (int x = 0; x < W; ++x)
        {
            size_t idx = size_t(y) * W + x;
            float d = depth[idx];
            if (d >= 1e8f)
            {
                out.setPixel(x, y, color.getPixel(x, y));
                continue;
            }
            // CoC: 离焦点越远，模糊越大
            float coc = std::abs(d - focusDist) / focusRange;
            coc = std::min(coc, 1.0f);
            int radius = int(coc * maxBlur);
            if (radius <= 0)
            {
                out.setPixel(x, y, color.getPixel(x, y));
                continue;
            }
            // Box blur
            Vec3f sum{0, 0, 0};
            int count = 0;
            for (int dy = -radius; dy <= radius; ++dy)
            {
                for (int dx = -radius; dx <= radius; ++dx)
                {
                    int nx = std::clamp(x + dx, 0, W - 1);
                    int ny = std::clamp(y + dy, 0, H - 1);
                    sum = sum + color.getPixel(nx, ny);
                    ++count;
                }
            }
            out.setPixel(x, y, sum * (1.0f / count));
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
    float cx = W * 0.5f, cy = H * 0.5f;
    for (int y = 0; y < H; ++y)
    {
        for (int x = 0; x < W; ++x)
        {
            float dx = (x - cx) / float(W);
            float dy = (y - cy) / float(H);
            float dist = std::sqrt(dx * dx + dy * dy);
            float blur = dist * strength;

            Vec3f sum{0, 0, 0};
            for (int s = 0; s < samples; ++s)
            {
                float t = (float(s) / (samples - 1) - 0.5f) * blur;
                int sx = std::clamp(int(x + dx * t * W), 0, W - 1);
                int sy = std::clamp(int(y + dy * t * H), 0, H - 1);
                sum = sum + color.getPixel(sx, sy);
            }
            out.setPixel(x, y, sum * (1.0f / samples));
        }
    }
}

// ---- 暗角 ----
// 屏幕边缘变暗，模拟镜头光学特性。strength: 0~1
inline void vignette(FloatFramebuffer& fb, float strength = 0.3f)
{
    int W = fb.width(), H = fb.height();
    float cx = W * 0.5f, cy = H * 0.5f;
    float maxDist = std::sqrt(cx * cx + cy * cy);
    for (int y = 0; y < H; ++y)
    {
        for (int x = 0; x < W; ++x)
        {
            float dx = (x - cx) / maxDist;
            float dy = (y - cy) / maxDist;
            float d = std::sqrt(dx * dx + dy * dy);
            float factor = 1.0f - d * d * strength;
            factor = std::max(factor, 0.0f);
            Vec3f c = fb.getPixel(x, y);
            fb.setPixel(x, y, c * factor);
        }
    }
}

// ---- 色差 ----
// R/B 通道向屏幕边缘反向偏移，模拟镜头色散。offset: 偏移像素数
inline void chromaticAberration(const FloatFramebuffer& src,
                                int W, int H, float offset,
                                FloatFramebuffer& out)
{
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
        generateKernel();
        generateNoise();
    }

    void configure(const SSAOConfig& cfg)
    {
        config_ = cfg;
        generateKernel();
    }

    // 生成 AO 图
    // viewPos: 每像素的视图空间位置 (x,y,z)
    // viewNormal: 每像素的视图空间法线
    // depth: 深度缓冲（NDC [0,1]）
    // viewProj: 视图*投影矩阵（用于把采样点投回屏幕）
    void compute(const std::vector<Vec3f>& viewPos, const std::vector<Vec3f>& viewNormal,
                 const std::vector<float>& depth, int W, int H, const Mat4& viewProj,
                 std::vector<float>& aoOut)
    {
        aoOut.resize(size_t(W) * H, 1.0f);
        int noiseScale_x = W / config_.noiseSize;
        int noiseScale_y = H / config_.noiseSize;

        for (int y = 0; y < H; ++y)
        {
            for (int x = 0; x < W; ++x)
            {
                size_t idx = size_t(y) * W + x;
                float d = depth[idx];
                if (d >= 0.999f)
                    continue; // 背景跳过

                Vec3f pos = viewPos[idx];
                Vec3f normal = viewNormal[idx];
                if (normal.lengthSq() < 1e-6f)
                    continue;
                normal = normal.normalized();

                // 随机旋转核（用噪声纹理）
                int nx = (x / std::max(1, noiseScale_x)) % config_.noiseSize;
                int ny = (y / std::max(1, noiseScale_y)) % config_.noiseSize;
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

                    // 投影到屏幕空间
                    Vec4f clip = viewProj * Vec4f(samplePos, 1.0f);
                    if (clip.w <= 0)
                    {
                        occlusion += 1.0f;
                        continue;
                    }
                    Vec3f ndc{clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};
                    int sx = int((ndc.x * 0.5f + 0.5f) * W);
                    int sy = int((ndc.y * 0.5f + 0.5f) * H);
                    if (sx < 0 || sx >= W || sy < 0 || sy >= H)
                        continue;

                    float sampleDepth = depth[size_t(sy) * W + sx];
                    float ndcZ = ndc.z * 0.5f + 0.5f;
                    float rangeCheck = std::abs(d - sampleDepth) < config_.radius ? 1.0f : 0.0f;
                    occlusion += (sampleDepth <= ndcZ - config_.bias ? 1.0f : 0.0f) * rangeCheck;
                }
                aoOut[idx] = 1.0f - (occlusion / float(samples));
            }
        }
    }

    // 对 AO 图做简单 box blur 降噪
    static void blur(const std::vector<float>& aoIn, int W, int H,
                     std::vector<float>& aoOut, int radius = 2)
    {
        aoOut.resize(size_t(W) * H);
        for (int y = 0; y < H; ++y)
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

   private:
    SSAOConfig config_;
    std::vector<Vec3f> kernel_; // 半球采样核
    std::vector<Vec3f> noise_;  // 4x4 随机旋转向量

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

} // namespace postprocess
