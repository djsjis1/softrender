#pragma once
#include "softrenderer/render/pipeline/varyings.h"
#include "softrenderer/resource/vertex.h"

// ============================================================
// Shader：可编程着色管线的契约（只有三段回调）。
//
// GPU 渲染管线的两个可编程阶段：
//   vertex   —— 对每个顶点执行，输入模型空间顶点，输出裁剪空间坐标，
//               并把需要插值的量写进 Varyings。
//   geometry —— 逐三角形钩子（可算面法线等），默认空实现。
//   fragment —— 对每个像素执行，输入插值后的 Varyings，输出颜色，
//               返回 false 表示丢弃该片元。
//
// 【为什么这里没有 uniform】uniform 与采样工具属于「带材质的表面着色」
// 这一具体场景，放在 SurfaceShader（surface_shader.h）。只关心几何输出的
// 着色器（如深度、遮罩）无需被迫携带光照与纹理依赖。
// ============================================================
namespace sr
{

struct Shader
{
    virtual ~Shader() = default;

    // 顶点着色：返回裁剪空间坐标，填充 out
    virtual Vec4f vertex(const Vertex &in, Varyings &out) = 0;

    // 逐三角形钩子：拿到三个顶点的 Varyings，可算面法线等（Flat 用）。
    virtual void geometry(const Varyings &, const Varyings &, const Varyings &) {}

    // 片元着色：给定插值后的 Varyings，输出颜色；返回 false 丢弃该片元
    virtual bool fragment(const Varyings &v, Vec3f &color) = 0;
};

}  // namespace sr
