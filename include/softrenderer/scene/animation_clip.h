#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "softrenderer/math/vec.h"

// ============================================================
// AnimationClip：已解码的动画片段
//
// 【为什么先解码】文件里的动画是"时间轴 accessor + 关键帧 accessor"，
// 运行时逐帧去读 accessor、查 bufferView、判类型，既慢又让渲染/场景层
// 依赖解析器类型。因此加载阶段一次性把时间轴与关键帧值拷进本结构并完成
// 校验（长度、类型、索引、有限值、时间轴严格递增、四元数非零），
// 运行时只做纯数学插值。
//
// 【values 的布局】
//   - Linear / Step：每个关键帧 1 条，vec3 用 xyz，rotation 用 xyzw
//   - CubicSpline  ：每个关键帧 3 条，顺序为 (入切线, 值, 出切线)
//   frameCount() == times.size()，values.size() == frameCount * stride()
// ============================================================

namespace sr
{

struct AnimationSampler
{
    enum class Interpolation : uint8_t
    {
        Linear,
        Step,
        CubicSpline
    };
    Interpolation interpolation = Interpolation::Linear;

    std::vector<float> times;  // 严格递增、非负
    std::vector<Vec4f> values; // 见上方布局说明

    bool cubic() const { return interpolation == Interpolation::CubicSpline; }
    size_t frameCount() const { return times.size(); }
    size_t stride() const { return cubic() ? 3 : 1; }
};

struct AnimationChannel
{
    enum class Path : uint8_t
    {
        Translation,
        Rotation,
        Scale
    };

    int samplerIndex = -1; // -1 = 未设置
    int targetNode = -1;   // -1 = 未设置
    Path path = Path::Translation;
};

struct AnimationClip
{
    std::string name;
    std::vector<AnimationSampler> samplers;
    std::vector<AnimationChannel> channels;

    // 片段时长 = 所有采样器时间轴的最大值（空片段为 0）
    float duration() const
    {
        float end = 0.0f;
        for (const auto &sampler : samplers)
            if (!sampler.times.empty())
                end = std::max(end, sampler.times.back());
        return end;
    }
};

} // namespace sr
