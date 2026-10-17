#pragma once
#include <cmath>
#include <limits>
#include <vector>

#include "math/mat4.h"
#include "math/quat.h"
#include "math/vec.h"
#include "scene/gltf_accessor.h"
#include "scene/gltf_types.h"
#include "tiny_gltf.h"

// ============================================================
// 动画播放器：给定时间 t，采样 glTF 动画的关键帧
//
// 支持 Linear / Step / CubicSpline；非法数据不修改输出
// 输出：每个 channel 的插值结果（translation/rotation/scale）
// ============================================================

// 动画采样结果
struct AnimSample
{
    int nodeIndex = -1;
    Vec3f translation{0, 0, 0};
    Quat rotation = Quat::identity();
    Vec3f scale{1, 1, 1};
    bool hasTranslation = false;
    bool hasRotation = false;
    bool hasScale = false;
};

// Return false without modifying out when data is invalid. See ADR-002.
inline bool sampleChannel(const tinygltf::Model& model, const GltfAnimation& anim,
                          const GltfAnimation::Channel& channel, float time, AnimSample& out)
{
    using View = sr::gltf::AccessorView;
    using Path = GltfAnimation::Channel::Path;
    using Interpolation = GltfAnimation::Sampler::Interpolation;
    if (!std::isfinite(time) || channel.targetNode < 0 || size_t(channel.targetNode) >= model.nodes.size() ||
        channel.samplerIndex < 0 || size_t(channel.samplerIndex) >= anim.samplers.size())
        return false;
    if (channel.path != Path::Translation && channel.path != Path::Rotation && channel.path != Path::Scale)
        return false;
    const auto& sampler = anim.samplers[size_t(channel.samplerIndex)];
    if (sampler.interpolation != Interpolation::Linear && sampler.interpolation != Interpolation::Step &&
        sampler.interpolation != Interpolation::CubicSpline)
        return false;
    auto input = View::create(model, sampler.input);
    auto output = View::create(model, sampler.output);
    const bool rotation = channel.path == Path::Rotation;
    const bool cubic = sampler.interpolation == Interpolation::CubicSpline;
    if (!input || !output || input->hasStride() || output->hasStride() || input->type() != TINYGLTF_TYPE_SCALAR ||
        input->componentType() != TINYGLTF_COMPONENT_TYPE_FLOAT ||
        output->type() != (rotation ? TINYGLTF_TYPE_VEC4 : TINYGLTF_TYPE_VEC3))
        return false;
    if (output->componentType() != TINYGLTF_COMPONENT_TYPE_FLOAT && !(rotation && output->normalized()))
        return false;
    const size_t factor = cubic ? 3 : 1;
    if (output->count() % factor || output->count() / factor != input->count())
        return false;
    // Validate and locate the interval in one pass without allocating per-channel scratch storage.
    size_t lo = 0;
    float previous = -1;
    for (size_t i = 0; i < input->count(); ++i)
    {
        float timestamp = 0;
        if (!input->readFloat(i, 0, timestamp) || timestamp < 0 || timestamp <= previous)
            return false;
        if (timestamp <= time)
            lo = i;
        previous = timestamp;
    }
    size_t hi = std::min(lo + 1, input->count() - 1);
    float lowerTime = 0, upperTime = 0;
    if (!input->readFloat(lo, 0, lowerTime))
        return false;
    if (time <= lowerTime)
        hi = lo;
    if (!input->readFloat(hi, 0, upperTime))
        return false;
    const double duration = double(upperTime) - lowerTime;
    double alpha = lo == hi ? 0.0 : (double(time) - lowerTime) / duration;
    if (sampler.interpolation == Interpolation::Step)
        alpha = 0;
    float a[4]{}, b[4]{}, value[4]{};
    const size_t components = rotation ? 4 : 3;
    for (size_t c = 0; c < components; ++c)
    {
        if (!output->readFloat(lo * factor + (cubic ? 1 : 0), c, a[c]) ||
            !output->readFloat(hi * factor + (cubic ? 1 : 0), c, b[c]))
            return false;
        double result = double(a[c]) * (1 - alpha) + double(b[c]) * alpha;
        if (cubic && lo != hi)
        {
            float outgoing = 0, incoming = 0;
            if (!output->readFloat(lo * 3 + 2, c, outgoing) || !output->readFloat(hi * 3, c, incoming))
                return false;
            const double t2 = alpha * alpha, t3 = t2 * alpha;
            result = (2 * t3 - 3 * t2 + 1) * a[c] + (t3 - 2 * t2 + alpha) * duration * outgoing +
                     (-2 * t3 + 3 * t2) * b[c] + (t3 - t2) * duration * incoming;
        }
        if (!std::isfinite(result) || std::abs(result) > double(std::numeric_limits<float>::max()))
            return false;
        value[c] = float(result);
    }
    AnimSample sampled;
    sampled.nodeIndex = channel.targetNode;
    if (rotation)
    {
        auto normalize = [](Quat q, Quat& result) {
            const double length =
                std::sqrt(double(q.x) * q.x + double(q.y) * q.y + double(q.z) * q.z + double(q.w) * q.w);
            if (!std::isfinite(length) || length == 0)
                return false;
            result = {float(q.x / length), float(q.y / length), float(q.z / length), float(q.w / length)};
            return true;
        };
        Quat q0, q1;
        if (!normalize({a[0], a[1], a[2], a[3]}, q0) || !normalize({b[0], b[1], b[2], b[3]}, q1))
            return false;
        const Quat q = cubic ? Quat{value[0], value[1], value[2], value[3]} : slerp(q0, q1, float(alpha));
        if (!normalize(q, sampled.rotation))
            return false;
        sampled.hasRotation = true;
    }
    else if (channel.path == Path::Translation)
    {
        sampled.translation = {value[0], value[1], value[2]};
        sampled.hasTranslation = true;
    }
    else
    {
        sampled.scale = {value[0], value[1], value[2]};
        sampled.hasScale = true;
    }
    out = sampled;
    return true;
}
// 采样整个动画在时间 t 的所有 channel，返回每个节点的变换
inline std::vector<AnimSample> sampleAnimation(const tinygltf::Model& model, const GltfAnimation& anim, float time)
{
    // 按节点聚合
    std::vector<AnimSample> samples;
    samples.resize(model.nodes.size());
    for (size_t i = 0; i < samples.size(); ++i)
    {
        samples[i].nodeIndex = (int)i;
        // 默认用节点的 bind pose
        const auto& node = model.nodes[i];
        if (node.translation.size() >= 3)
            samples[i].translation = {(float)node.translation[0], (float)node.translation[1],
                                      (float)node.translation[2]};
        if (node.rotation.size() >= 4)
            samples[i].rotation = Quat{(float)node.rotation[0], (float)node.rotation[1], (float)node.rotation[2],
                                       (float)node.rotation[3]};
        if (node.scale.size() >= 3)
            samples[i].scale = {(float)node.scale[0], (float)node.scale[1], (float)node.scale[2]};
    }

    for (const auto& ch : anim.channels)
    {
        AnimSample s;
        if (!sampleChannel(model, anim, ch, time, s))
            continue;
        if (s.nodeIndex >= 0 && s.nodeIndex < (int)samples.size())
        {
            auto& target = samples[s.nodeIndex];
            if (s.hasTranslation)
            {
                target.translation = s.translation;
                target.hasTranslation = true;
            }
            if (s.hasRotation)
            {
                target.rotation = s.rotation;
                target.hasRotation = true;
            }
            if (s.hasScale)
            {
                target.scale = s.scale;
                target.hasScale = true;
            }
        }
    }
    return samples;
}

// 从 AnimSample 构建局部变换矩阵
inline Mat4 animSampleToMatrix(const AnimSample& s)
{
    return Mat4::translate(s.translation.x, s.translation.y, s.translation.z) * s.rotation.toMat4() *
           Mat4::scale(s.scale.x, s.scale.y, s.scale.z);
}
