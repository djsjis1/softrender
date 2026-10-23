#pragma once
#include "softrenderer/math/vec.h"

// ============================================================
// Vertex：网格顶点属性（模型空间）。
//
// 【为什么独立成头】Vertex 是 resource（网格）与 render（着色器输入）
// 的交汇类型。若它只存在于 mesh.h，只想声明「顶点着色器输入」的
// 着色器契约就必须连带拉入整个网格与程序化生成器。
//
// 携带四种最常用的顶点属性：
//   position - 模型空间坐标
//   normal   - 法线（光照用）
//   uv       - 纹理坐标
//   tangent  - 沿 U 增大的切向（法线贴图 TBN 用）
// ============================================================
namespace sr
{

struct Vertex
{
    Vec3f position;
    Vec3f normal;
    Vec2f uv;
    Vec3f tangent;
};

}  // namespace sr
