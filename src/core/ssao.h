#pragma once
#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include "math/mat4.h"
#include "math/vec.h"

// ============================================================
// SSAO：Screen-Space Ambient Occlusion
//
// 原理：对每个像素，在半球内随机撒 N 个采样点，把每个采样点
// 投影到屏幕空间查深度——如果采样点比该处实际深度更"深入"
// 场景（被遮挡），则计为遮挡。遮挡比例越高，AO 越暗。
//
// 参考：CryEngine 3 / LearnOpenGL SSAO 教程。
// 优化：用低分辨率噪声纹理旋转采样核，减少方向偏差。
//
// 输入：深度缓冲 + 法线缓冲（世界空间或视图空间）+ 视图/投影矩阵
// 输出：AO 因子图 [0,1]（1=无遮挡，0=全遮挡）
//
// 【命名空间】SSAOConfig / SSAO 已迁入 namespace sr（ADR-001 第 2 批）。
// 实测引用面：全仓仅本文件自身引用，零外部使用者，故不留全局别名。
// 内部对 Vec3f / Mat4 的引用无需限定：它们尚在全局作用域，
// 而在 namespace sr 内的名字查找会回落到全局作用域。
// ============================================================

namespace sr
{

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

    // 生成 AO 图（视图空间法线 + 视图空间位置）
    // viewPos: 每像素的视图空间位置 (x,y,z)
    // viewNormal: 每像素的视图空间法线
    // depth: 深度缓冲（NDC [0,1]）
    // W,H: 分辨率
    // viewProj: 视图*投影矩阵（用于把采样点投回屏幕）
    void compute(const std::vector<Vec3f>& viewPos, const std::vector<Vec3f>& viewNormal,
                 const std::vector<float>& depth, int W, int H, const Mat4& viewProj, std::vector<float>& aoOut)
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
                    continue;  // 背景跳过

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
                    // 把 NDC 深度转回视图空间 z 做比较
                    // 简化：直接用 NDC 深度比较 + bias
                    float ndcZ = ndc.z * 0.5f + 0.5f;
                    float rangeCheck = std::abs(d - sampleDepth) < config_.radius ? 1.0f : 0.0f;
                    occlusion += (sampleDepth <= ndcZ - config_.bias ? 1.0f : 0.0f) * rangeCheck;
                }
                aoOut[idx] = 1.0f - (occlusion / float(samples));
            }
        }
    }

    // 对 AO 图做简单 box blur 降噪
    static void blur(const std::vector<float>& aoIn, int W, int H, std::vector<float>& aoOut, int radius = 2)
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
    std::vector<Vec3f> kernel_;  // 半球采样核
    std::vector<Vec3f> noise_;   // 4x4 随机旋转向量

    void generateKernel()
    {
        kernel_.resize(config_.sampleCount);
        std::mt19937 rng(42);
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        for (int i = 0; i < config_.sampleCount; ++i)
        {
            Vec3f sample{dist(rng) * 2.0f - 1.0f, dist(rng) * 2.0f - 1.0f, dist(rng)};  // z >= 0 半球
            sample = sample.normalized();
            sample = sample * dist(rng);
            // 加速分布：靠近中心密、远处疏
            float scale = float(i) / float(config_.sampleCount);
            scale = 0.1f + scale * scale * 0.9f;  // lerp(0.1, 1.0, scale²)
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

} // namespace sr
