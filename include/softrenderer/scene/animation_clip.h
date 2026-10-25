#pragma once
#include <cstddef>
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

    // 是否已通过加载期校验（时间轴严格递增、非负、长度匹配）。
    //
    // 每次采样都重新扫描整条时间轴是 O(n)：1 万关键帧的片段每帧采样会扫 1 万次，
    // 实测 2 万次采样从 1.4 s 降到几毫秒的差别就在这里。
    // 由加载器在完成校验后置 true；手工构造的片段保持 false，仍走完整校验。
    bool validated = false;

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
