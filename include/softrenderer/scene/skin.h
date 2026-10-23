#pragma once
#include <string>
#include <vector>

#include "softrenderer/math/mat4.h"

// ============================================================
// Skin：骨骼蒙皮绑定（运行时数据）
//
// 逆绑定矩阵在加载阶段就已经解码成 Mat4，运行时不再访问 accessor。
// joints 与 inverseBindMatrices 一一对应；文件里允许省略逆绑定矩阵，
// 此时 inverseBindMatrices 为空，调用方按单位矩阵处理。
// ============================================================

namespace sr
{

struct Skin
{
    std::string name;
    std::vector<int> joints;              // 关节节点索引（引用 SceneAsset::nodes）
    int skeleton = -1;                    // 根骨骼节点索引，-1 = 未指定
    std::vector<Mat4> inverseBindMatrices; // 与 joints 等长，或为空
};

} // namespace sr
