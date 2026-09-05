#pragma once
#include <cmath>
#include <vector>

#include "math/mat4.h"
#include "math/quat.h"
#include "math/vec.h"
#include "scene/gltf_types.h"
#include "tiny_gltf.h"

// ============================================================
// 动画播放器：给定时间 t，采样 glTF 动画的关键帧
//
// 支持 Linear / Step 插值（CubicSpline 简化为 Linear）
// 输出：每个 channel 的插值结果（translation/rotation/scale）
// ============================================================

// 动画采样结果
struct AnimSample
{
    int nodeIndex;
    Vec3f translation{0, 0, 0};
    Quat rotation = Quat::identity();
    Vec3f scale{1, 1, 1};
    bool hasTranslation = false;
    bool hasRotation = false;
    bool hasScale = false;
};

// 从 accessor 读取 float 数组
inline const float* accessorFloats(const tinygltf::Model& model, int accessorIdx)
{
    if (accessorIdx < 0 || accessorIdx >= (int)model.accessors.size())
        return nullptr;
    const auto& acc = model.accessors[accessorIdx];
    if (acc.bufferView < 0)
        return nullptr;
    const auto& bv = model.bufferViews[acc.bufferView];
    const auto& buf = model.buffers[bv.buffer];
    return reinterpret_cast<const float*>(buf.data.data() + bv.byteOffset + acc.byteOffset);
}

// 采样单个 channel 在时间 t 的值
inline void sampleChannel(const tinygltf::Model& model, const GltfAnimation& anim,
                          const GltfAnimation::Channel& channel, float time, AnimSample& out)
{
    if (channel.samplerIndex < 0 || channel.samplerIndex >= (int)anim.samplers.size())
        return;
    const auto& sampler = anim.samplers[channel.samplerIndex];

    const float* input = accessorFloats(model, sampler.input);    // 时间戳
    const float* output = accessorFloats(model, sampler.output);  // 关键帧值
    if (!input || !output)
        return;

    const auto& inAcc = model.accessors[sampler.input];
    const auto& outAcc = model.accessors[sampler.output];
    int keyCount = (int)inAcc.count;
    if (keyCount < 1)
        return;

    // 确定 component 数量
    int comps = 1;
    if (outAcc.type == TINYGLTF_TYPE_VEC3)
        comps = 3;
    else if (outAcc.type == TINYGLTF_TYPE_VEC4)
        comps = 4;

    // 二分查找时间区间
    int lo = 0, hi = keyCount - 1;
    if (time <= input[0])
    {
        lo = 0;
        hi = 0;
    }
    else if (time >= input[keyCount - 1])
    {
        lo = keyCount - 1;
        hi = keyCount - 1;
    }
    else
    {
        while (lo + 1 < hi)
        {
            int mid = (lo + hi) / 2;
            if (input[mid] <= time)
                lo = mid;
            else
                hi = mid;
        }
    }

    float t0 = input[lo], t1 = input[hi];
    float alpha = (hi == lo) ? 0.0f : (time - t0) / (t1 - t0);

    // Step 插值
    if (sampler.interpolation == GltfAnimation::Sampler::Interpolation::Step)
        alpha = 0.0f;

    const float* v0 = output + lo * comps;
    const float* v1 = output + hi * comps;

    out.nodeIndex = channel.targetNode;

    switch (channel.path)
    {
        case GltfAnimation::Channel::Path::Translation:
            out.hasTranslation = true;
            out.translation = {v0[0] + (v1[0] - v0[0]) * alpha, v0[1] + (v1[1] - v0[1]) * alpha,
                               v0[2] + (v1[2] - v0[2]) * alpha};
            break;
        case GltfAnimation::Channel::Path::Rotation:
            out.hasRotation = true;
            // 四元数 SLERP
            {
                Quat q0{v0[0], v0[1], v0[2], v0[3]};
                Quat q1{v1[0], v1[1], v1[2], v1[3]};
                out.rotation = slerp(q0, q1, alpha);
            }
            break;
        case GltfAnimation::Channel::Path::Scale:
            out.hasScale = true;
            out.scale = {v0[0] + (v1[0] - v0[0]) * alpha, v0[1] + (v1[1] - v0[1]) * alpha,
                         v0[2] + (v1[2] - v0[2]) * alpha};
            break;
    }
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
        sampleChannel(model, anim, ch, time, s);
        if (s.nodeIndex >= 0 && s.nodeIndex < (int)samples.size())
        {
            auto& target = samples[s.nodeIndex];
            if (s.hasTranslation)
                target.translation = s.translation;
            if (s.hasRotation)
                target.rotation = s.rotation;
            if (s.hasScale)
                target.scale = s.scale;
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
