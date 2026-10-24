#pragma once
#include <cstddef>
#include <cmath>
#include <limits>
#include <vector>

#include "softrenderer/math/mat4.h"
#include "softrenderer/math/quat.h"
#include "softrenderer/math/vec.h"
#include "softrenderer/scene/animation_clip.h"
#include "softrenderer/scene/scene_node.h"

// ============================================================
// 动画采样：给定时间 t，在已解码的 AnimationClip 上做纯数学插值
//
// 支持 Linear / Step / CubicSpline；非法数据不修改输出（见 ADR-002）。
// 本头不访问任何文件格式或解析器类型——加载阶段已把时间轴、关键帧值
// 和三次插值切线都解码进 AnimationSampler。
//
// 【约定】
//   - times 严格递增；采样点落在区间外时钳到首/尾关键帧
//   - rotation 通道插值前归一化、插值后再归一化，零四元数视为非法
//   - 返回值只表达"该通道是否采样成功"；失败时 out 保持原值
// ============================================================

namespace sr::animation
{

// 单个节点的采样结果（bind pose 作为默认值）
struct NodePose
{
    int nodeIndex = -1;
    Vec3f translation{0, 0, 0};
    Quat rotation = Quat::identity();
    Vec3f scale{1, 1, 1};
    bool hasTranslation = false;
    bool hasRotation = false;
    bool hasScale = false;
};

namespace detail
{

// 归一化四元数；零长度或非有限值返回 false
inline bool normalizeQuat(Quat q, Quat &result)
{
    const double length = std::sqrt(double(q.x) * q.x + double(q.y) * q.y + double(q.z) * q.z + double(q.w) * q.w);
    if (!std::isfinite(length) || length == 0)
        return false;
    result = {float(q.x / length), float(q.y / length), float(q.z / length), float(q.w / length)};
    return true;
}

// 采样器的静态合法性：长度关系与时间轴单调性。
// 加载阶段已校验一次，这里兜底防御手工构造的片段。
inline bool samplerIsValid(const AnimationSampler &sampler)
{
    if (sampler.times.empty())
        return false;
    const size_t stride = sampler.stride();
    if (sampler.values.size() != sampler.times.size() * stride)
        return false;
    float previous = -1.0f;
    for (float t : sampler.times)
    {
        if (!std::isfinite(t) || t < 0.0f || t <= previous)
            return false;
        previous = t;
    }
    return true;
}

} // namespace detail

// 采样单个通道。失败返回 false 且不修改 out。
inline bool sampleChannel(const AnimationClip &clip, const AnimationChannel &channel, float time, NodePose &out)
{
    using Path = AnimationChannel::Path;
    using Interpolation = AnimationSampler::Interpolation;

    if (!std::isfinite(time) || channel.targetNode < 0 || channel.samplerIndex < 0 ||
        size_t(channel.samplerIndex) >= clip.samplers.size())
        return false;
    if (channel.path != Path::Translation && channel.path != Path::Rotation && channel.path != Path::Scale)
        return false;

    const AnimationSampler &sampler = clip.samplers[size_t(channel.samplerIndex)];
    if (sampler.interpolation != Interpolation::Linear && sampler.interpolation != Interpolation::Step &&
        sampler.interpolation != Interpolation::CubicSpline)
        return false;
    if (!detail::samplerIsValid(sampler))
        return false;

    const bool rotation = channel.path == Path::Rotation;
    const bool cubic = sampler.cubic();
    const size_t frames = sampler.frameCount();
    const size_t stride = sampler.stride();

    // 定位区间；落在首帧之前或末帧之后时钳到端点
    size_t lo = 0;
    for (size_t i = 0; i < frames; ++i)
        if (sampler.times[i] <= time)
            lo = i;
    size_t hi = lo + 1 < frames ? lo + 1 : lo;
    if (time <= sampler.times[lo])
        hi = lo;

    const double duration = double(sampler.times[hi]) - double(sampler.times[lo]);
    double alpha = hi == lo ? 0.0 : (double(time) - double(sampler.times[lo])) / duration;
    if (sampler.interpolation == Interpolation::Step)
        alpha = 0.0;

    const size_t components = rotation ? 4u : 3u;
    float a[4]{}, b[4]{}, value[4]{};
    for (size_t c = 0; c < components; ++c)
    {
        const Vec4f &keyA = sampler.values[lo * stride + (cubic ? 1 : 0)];
        const Vec4f &keyB = sampler.values[hi * stride + (cubic ? 1 : 0)];
        const float av = c == 0 ? keyA.x : c == 1 ? keyA.y : c == 2 ? keyA.z : keyA.w;
        const float bv = c == 0 ? keyB.x : c == 1 ? keyB.y : c == 2 ? keyB.z : keyB.w;
        a[c] = av;
        b[c] = bv;
        double result = double(av) * (1.0 - alpha) + double(bv) * alpha;
        if (cubic && lo != hi)
        {
            const Vec4f &tangentOut = sampler.values[lo * stride + 2];
            const Vec4f &tangentIn = sampler.values[hi * stride];
            const float outgoing =
                c == 0 ? tangentOut.x : c == 1 ? tangentOut.y : c == 2 ? tangentOut.z : tangentOut.w;
            const float incoming = c == 0 ? tangentIn.x : c == 1 ? tangentIn.y : c == 2 ? tangentIn.z : tangentIn.w;
            const double t2 = alpha * alpha, t3 = t2 * alpha;
            result = (2 * t3 - 3 * t2 + 1) * av + (t3 - 2 * t2 + alpha) * duration * outgoing +
                     (-2 * t3 + 3 * t2) * bv + (t3 - t2) * duration * incoming;
        }
        if (!std::isfinite(result) || std::abs(result) > double(std::numeric_limits<float>::max()))
            return false;
        value[c] = float(result);
    }

    NodePose sampled;
    sampled.nodeIndex = channel.targetNode;
    if (rotation)
    {
        Quat q0, q1;
        if (!detail::normalizeQuat({a[0], a[1], a[2], a[3]}, q0) ||
            !detail::normalizeQuat({b[0], b[1], b[2], b[3]}, q1))
            return false;
        const Quat q = cubic ? Quat{value[0], value[1], value[2], value[3]} : slerp(q0, q1, float(alpha));
        if (!detail::normalizeQuat(q, sampled.rotation))
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

// 采样整个片段在时间 t 的所有通道：以节点的 bind pose 为默认值，
// 再按通道结果覆盖，未覆盖的通道保持 bind pose（has* 标志为 false）。
inline std::vector<NodePose> sampleAnimation(const std::vector<SceneNode> &nodes, const AnimationClip &clip, float time)
{
    std::vector<NodePose> poses(nodes.size());
    for (size_t i = 0; i < poses.size(); ++i)
    {
        poses[i].nodeIndex = int(i);
        poses[i].translation = nodes[i].translation;
        poses[i].rotation = nodes[i].rotation;
        poses[i].scale = nodes[i].scale;
    }

    for (const auto &channel : clip.channels)
    {
        if (channel.targetNode < 0 || size_t(channel.targetNode) >= poses.size())
            continue;
        NodePose sample;
        if (!sampleChannel(clip, channel, time, sample))
            continue;
        NodePose &target = poses[size_t(sample.nodeIndex)];
        if (sample.hasTranslation)
        {
            target.translation = sample.translation;
            target.hasTranslation = true;
        }
        if (sample.hasRotation)
        {
            target.rotation = sample.rotation;
            target.hasRotation = true;
        }
        if (sample.hasScale)
        {
            target.scale = sample.scale;
            target.hasScale = true;
        }
    }
    return poses;
}

// 从采样结果构建局部变换矩阵
inline Mat4 poseToMatrix(const NodePose &pose)
{
    return Mat4::translate(pose.translation.x, pose.translation.y, pose.translation.z) * pose.rotation.toMat4() *
           Mat4::scale(pose.scale.x, pose.scale.y, pose.scale.z);
}

} // namespace sr::animation
