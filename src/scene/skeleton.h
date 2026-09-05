#pragma once
#include <vector>
#include <cmath>
#include "math/mat4.h"
#include "math/vec.h"

// ============================================================
// 骨骼动画：顶点蒙皮（Linear Blend Skinning）
//
// 每个顶点绑定最多 4 根骨骼，带权重。每帧：
//   1. 根据当前动画时间，计算每根骨骼的全局变换矩阵
//   2. 对每个顶点：skinnedPos = Σ weight_i * boneMatrix_i * bindPoseInverse_i * position
//
// 参考：OpenGL 骨骼动画教程、glTF 规范。
// ============================================================

// 骨骼节点
struct Bone
{
    std::string name;
    int parent = -1;        // 父骨骼索引，-1 = 根
    Mat4 localTransform;    // 相对于父骨骼的变换（bind pose）
    Mat4 inverseBindMatrix; // 绑定姿态的逆矩阵
};

// 骨骼层级（Skeleton）
struct Skeleton
{
    std::vector<Bone> bones;

    // 计算全局变换矩阵（从根到叶递归）
    std::vector<Mat4> computeGlobalTransforms() const
    {
        std::vector<Mat4> global(bones.size());
        for (size_t i = 0; i < bones.size(); ++i)
        {
            if (bones[i].parent < 0)
            {
                global[i] = bones[i].localTransform;
            }
            else
            {
                global[i] = global[bones[i].parent] * bones[i].localTransform;
            }
        }
        return global;
    }

    // 计算蒙皮矩阵 = globalTransform * inverseBindMatrix
    std::vector<Mat4> computeSkinMatrices() const
    {
        auto global = computeGlobalTransforms();
        std::vector<Mat4> skin(bones.size());
        for (size_t i = 0; i < bones.size(); ++i)
        {
            skin[i] = global[i] * bones[i].inverseBindMatrix;
        }
        return skin;
    }
};

// 顶点的骨骼绑定信息（最多 4 根骨骼）
struct VertexBoneWeights
{
    int boneIndices[4] = {-1, -1, -1, -1};
    float weights[4] = {0, 0, 0, 0};
};

// 对单个顶点做蒙皮变换
inline Vec3f skinPosition(const Vec3f &pos, const VertexBoneWeights &bw,
                          const std::vector<Mat4> &skinMatrices)
{
    Vec3f result{0, 0, 0};
    for (int i = 0; i < 4; ++i)
    {
        if (bw.boneIndices[i] < 0 || bw.weights[i] <= 0)
            continue;
        Vec3f transformed = skinMatrices[bw.boneIndices[i]].transformPoint(pos);
        result = result + transformed * bw.weights[i];
    }
    return result;
}

// 对法线做蒙皮变换（只旋转，不平移）
inline Vec3f skinNormal(const Vec3f &normal, const VertexBoneWeights &bw,
                        const std::vector<Mat4> &skinMatrices)
{
    Vec3f result{0, 0, 0};
    for (int i = 0; i < 4; ++i)
    {
        if (bw.boneIndices[i] < 0 || bw.weights[i] <= 0)
            continue;
        Vec3f transformed = skinMatrices[bw.boneIndices[i]].transformDir(normal);
        result = result + transformed * bw.weights[i];
    }
    return result.normalized();
}

// ---- 动画关键帧 ----
struct Keyframe
{
    float time;
    Vec3f translation{0, 0, 0};
    Vec3f rotation{0, 0, 0}; // 欧拉角（简化，完整应用四元数）
    Vec3f scale{1, 1, 1};
};

struct AnimationChannel
{
    int boneIndex;
    std::vector<Keyframe> keyframes; // 按时间排序
};

struct Animation
{
    std::string name;
    float duration;
    std::vector<AnimationChannel> channels;
};

// 辅助：从关键帧构造变换矩阵
inline Mat4 makeTransform(const Keyframe &kf)
{
    return Mat4::translate(kf.translation.x, kf.translation.y, kf.translation.z) *
           Mat4::rotateY(kf.rotation.y) *
           Mat4::rotateX(kf.rotation.x) *
           Mat4::rotateZ(kf.rotation.z) *
           Mat4::scale(kf.scale.x, kf.scale.y, kf.scale.z);
}

// 在给定时间插值骨骼的局部变换
inline Mat4 sampleAnimation(const AnimationChannel &channel, float time)
{
    const auto &kf = channel.keyframes;
    if (kf.empty())
        return Mat4::identity();
    if (time <= kf.front().time)
        return makeTransform(kf.front());
    if (time >= kf.back().time)
        return makeTransform(kf.back());

    // 二分查找
    int lo = 0, hi = int(kf.size()) - 1;
    while (lo + 1 < hi)
    {
        int mid = (lo + hi) / 2;
        if (kf[mid].time <= time)
            lo = mid;
        else
            hi = mid;
    }
    float t = (time - kf[lo].time) / (kf[hi].time - kf[lo].time);
    // 线性插值
    Keyframe interp;
    interp.translation = kf[lo].translation + (kf[hi].translation - kf[lo].translation) * t;
    interp.rotation = kf[lo].rotation + (kf[hi].rotation - kf[lo].rotation) * t;
    interp.scale = kf[lo].scale + (kf[hi].scale - kf[lo].scale) * t;
    return makeTransform(interp);
}
