#pragma once
#include <algorithm>
#include <cstddef>
#include <cmath>
#include <string>
#include <vector>

#include "softrenderer/math/mat4.h"
#include "softrenderer/math/vec.h"

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
namespace sr
{

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

    // 计算全局变换矩阵。
    //
    // 旧实现单次顺序遍历，隐含要求「父骨骼索引 < 子骨骼索引」；反序或环
    // 会读到未初始化的 global。现在按父链上溯解析，与数组顺序无关，
    // 并在父索引越界或存在环时返回空数组（调用方据此判失败，而不是拿到错矩阵）。
    std::vector<Mat4> computeGlobalTransforms() const
    {
        const size_t n = bones.size();
        std::vector<Mat4> global(n, Mat4::identity());
        std::vector<char> resolved(n, 0);
        std::vector<size_t> chain;

        for (size_t i = 0; i < n; ++i)
        {
            if (resolved[i])
                continue;

            chain.clear();
            size_t cur = i;
            while (!resolved[cur])
            {
                chain.push_back(cur);
                const int parent = bones[cur].parent;
                if (parent < 0)
                    break;
                if (size_t(parent) >= n)
                    return {}; // 父索引越界
                if (std::find(chain.begin(), chain.end(), size_t(parent)) != chain.end())
                    return {}; // 环
                cur = size_t(parent);
            }

            // 从根端向叶端累乘：链路末端要么是根，要么已解析。
            for (size_t k = chain.size(); k-- > 0;)
            {
                const Bone& b = bones[chain[k]];
                global[chain[k]] = b.parent < 0 ? b.localTransform : global[size_t(b.parent)] * b.localTransform;
                resolved[chain[k]] = 1;
            }
        }
        return global;
    }

    // 计算蒙皮矩阵 = globalTransform * inverseBindMatrix
    std::vector<Mat4> computeSkinMatrices() const
    {
        auto global = computeGlobalTransforms();
        if (global.empty() && !bones.empty())
            return {};
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

// 对单个顶点做蒙皮变换。
//
// 骨骼索引越界的权重直接忽略；权重总和为 0（或全部无效）时退回原始位置，
// 避免得到零向量或 NaN。权重不归一化是因为 glTF 合法数据本应和为 1，
// 归一化会掩盖上游数据错误。
inline Vec3f skinPosition(const Vec3f &pos, const VertexBoneWeights &bw,
                          const std::vector<Mat4> &skinMatrices)
{
    Vec3f result{0, 0, 0};
    float totalWeight = 0.0f;
    for (int i = 0; i < 4; ++i)
    {
        const int bone = bw.boneIndices[i];
        if (bone < 0 || bw.weights[i] <= 0 || size_t(bone) >= skinMatrices.size())
            continue;
        result = result + skinMatrices[size_t(bone)].transformPoint(pos) * bw.weights[i];
        totalWeight += bw.weights[i];
    }
    return totalWeight > 0.0f ? result : pos;
}

// 对法线做蒙皮变换（只旋转，不平移）
inline Vec3f skinNormal(const Vec3f &normal, const VertexBoneWeights &bw,
                        const std::vector<Mat4> &skinMatrices)
{
    Vec3f result{0, 0, 0};
    float totalWeight = 0.0f;
    for (int i = 0; i < 4; ++i)
    {
        const int bone = bw.boneIndices[i];
        if (bone < 0 || bw.weights[i] <= 0 || size_t(bone) >= skinMatrices.size())
            continue;
        result = result + skinMatrices[size_t(bone)].transformDir(normal) * bw.weights[i];
        totalWeight += bw.weights[i];
    }
    if (totalWeight <= 0.0f || result.lengthSq() < 1e-20f)
        return normal; // 无有效权重或抵消为零向量：退回原法线，不返回 NaN
    return result.normalized();
}

}  // namespace sr
