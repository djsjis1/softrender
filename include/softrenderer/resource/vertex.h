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
    // 四个成员都带默认值，两个原因：
    // 1. 不初始化时 `Vertex v;` 的字段是未初始化内存（obj_loader 等处依赖后续赋值，
    //    一旦漏赋就是垃圾值进光栅化）；
    // 2. 聚合初始化只写前几个字段是常见写法（`{{pos},{normal},{uv}}`），
    //    没有默认值时 GCC / Clang 的 -Wmissing-field-initializers 会直接报错（CI 就挂在这里）。
    Vec3f position{0, 0, 0};
    Vec3f normal{0, 0, 1}; // 朝向观察者的单位法线，避免零向量进光照
    Vec2f uv{0, 0};
    Vec3f tangent{0, 0, 0};
};

}  // namespace sr
