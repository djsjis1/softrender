#pragma once
#include <vector>

#include "softrenderer/core/framebuffer.h"
#include "softrenderer/render/pipeline/renderer.h"
#include "softrenderer/render/shader/advanced_shaders.h"

// ============================================================
// ShadowPass：阴影贴图渲染（从示例收敛进库的公共 API）。
//
// 两趟阴影映射的第一趟：从光源视角渲一遍场景，深度缓冲即阴影贴图。
// 采样端用 advanced_shaders.h 的 FinalShader（含 3×3 PCF 软化）：
//   finalShader.shadowMap = &lightRenderer.depth();
//   finalShader.lightVP   = lightVP;   // lightProj * lightView（不含 model）
//
// 为什么收敛进库：此前每个示例各自手写"清屏 + DepthShader + 循环
// drawMesh"，口径（双面渲入、清屏颜色）容易漂移；统一后示例与门面
// 共用同一实现，覆盖规则与主 pass 天然一致。
// ============================================================
namespace sr
{

// 一个阴影投射物。mvp = lightProj * lightView * model（完整链）。
struct ShadowCaster
{
    const Mesh* mesh = nullptr;
    Mat4 mvp = Mat4::identity();
};

// 把 casters 从光源视角渲进 lightRenderer 的深度缓冲。
// lightRenderer 的尺寸即阴影贴图分辨率；lightTarget 是同尺寸的哑帧缓冲
// （颜色输出无意义，仅为满足 drawMesh 的接口）。
//
// lightTarget 是模板参数，只需满足 BasicFramebuffer 的接口。一张 1024² 的
// RGBA8888 哑缓冲要 4 MB，而这些像素永远不会被读——传
// BasicFramebuffer<Gray8> 即可降到 1 MB（每帧被写的一百万个字节里没有有效信息）。
//
// 阴影图双面渲入（关闭背面剔除）以减少薄几何漏光——与示例 stage10 同口径。
template <class FB>
inline void renderShadowPass(Renderer& lightRenderer, FB& lightTarget,
                             const std::vector<ShadowCaster>& casters)
{
    if (lightRenderer.width() <= 0 || lightRenderer.height() <= 0)
        return;
    lightRenderer.clear(lightTarget, Color(0, 0, 0));
    DepthShader depthShader;
    lightRenderer.cullBackface = false;
    for (const auto& caster : casters)
    {
        if (caster.mesh == nullptr)
            continue;
        depthShader.mvp = caster.mvp;
        lightRenderer.drawMesh(lightTarget, *caster.mesh, depthShader);
    }
}

}  // namespace sr
