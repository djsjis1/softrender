#pragma once
#include <string>
#include <vector>

#include "softrenderer/math/mat4.h"
#include "softrenderer/math/quat.h"
#include "softrenderer/math/vec.h"

// ============================================================
// SceneNode：场景图节点（运行时数据）
//
// 与 glTF 等文件格式解耦：字段是"渲染需要什么"，不是"文件里写了什么"。
// 变换支持两种互斥表达：
//   - TRS（translation / rotation / scale），动画采样写的就是这三项
//   - matrix（若有则覆盖 TRS，glTF 允许直接给矩阵）
// ============================================================

namespace sr
{

struct SceneNode
{
    std::string name;
    int meshIndex = -1; // 引用 SceneAsset::meshes，-1 = 不绘制
    int skinIndex = -1; // 引用 SceneAsset::skins，-1 = 不蒙皮
    std::vector<int> children;

    Vec3f translation{0, 0, 0};
    Quat rotation = Quat::identity();
    Vec3f scale{1, 1, 1};

    Mat4 matrix = Mat4::identity();
    bool hasMatrix = false;

    // 节点的局部变换：有矩阵用矩阵，否则 T * R * S
    Mat4 localMatrix() const
    {
        if (hasMatrix)
            return matrix;
        return Mat4::translate(translation.x, translation.y, translation.z) * rotation.toMat4() *
               Mat4::scale(scale.x, scale.y, scale.z);
    }
};

} // namespace sr
