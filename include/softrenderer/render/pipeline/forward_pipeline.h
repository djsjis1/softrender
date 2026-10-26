#pragma once
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

#include "softrenderer/core/framebuffer.h"
#include "softrenderer/render/pipeline/render_queue.h"
#include "softrenderer/render/pipeline/renderer.h"
#include "softrenderer/render/pipeline/shadow_pass.h"
#include "softrenderer/render/postprocess/post_processing.h"

// ============================================================
// ForwardPipeline：统一前向管线门面。
//
// 【解决什么问题】此前"画一帧带阴影 + 抗锯齿的场景"需要自己拼装：
// 阴影贴图 Renderer + 主 Renderer + RenderQueue 排序 + FXAA 缓冲管理，
// 这些能力分散在各示例中（54 号文：MSAA/PBR/SSAO/后处理"并非单个
// 统一渲染入口的所有开关"）。本门面把它们串成一次 render() 调用：
//
//   阴影 pass（可选）→ clear → 队列排序（不透明近→远 / 透明远→近）
//   → 逐项 drawMesh → 后处理链（FXAA，可选分条并行）
//
// 【边界】只依赖 core + render 组件：不引入场景图、加载器、线程或窗口。
// 相机用 view/proj 矩阵直接传入（scene::Camera 的 viewMatrix/projMatrix
// 结果可以原样塞进来）；并行后处理通过 StripExecutor 回调注入，调度
// 设施（线程池）由使用者提供。
//
// 【仍推荐的用法】学习光栅化原理请直接用 Renderer::drawMesh——门面
// 隐藏的正是那些值得理解的细节。
// ============================================================
namespace sr
{

// 一条绘制命令。shader 的 uniform 状态（mvp、材质、光照）由调用方在
// 提交前写好——门面只保存指针做排序与调度，不复制 shader。
struct ForwardDraw
{
    const Mesh* mesh = nullptr;
    Shader* shader = nullptr;
    RenderLayer layer = RenderLayer::Opaque;
    int materialId = 0;         // 同材质相邻绘制（减少状态切换的排序次级键）
    Vec3f sortCenter{0, 0, 0};  // 包围球中心（世界空间），供排序距离用
    // 阴影启用时作为投射物：mvp = lightProj * lightView * model（完整链）。
    // 与主 pass 的 mvp（proj*view*model）是两条独立矩阵链。
    Mat4 lightMVP = Mat4::identity();
    bool castShadow = true;
};

// 分条并行执行器：把 totalRows 行切成条带喂给 body(y0, y1) 并等待完成。
// 典型实现：把条带 submit 给 sr::ThreadPool 再 waitAll。为空 = 单线程。
using StripExecutor = std::function<void(int totalRows, const std::function<void(int, int)>& body)>;

// 默认单线程执行器（整帧一次调用，等价于不分条）
inline void serialStripExecutor(int totalRows, const std::function<void(int, int)>& body)
{
    body(0, totalRows);
}

class ForwardPipeline
{
   public:
    ForwardPipeline(int width, int height)
        : width_(width), height_(height), mainRenderer_(std::make_unique<Renderer>(width, height))
    {
    }

    // 裸机路径：主深度缓冲用外部存储，整条渲染链不经堆分配
    // （帧缓冲本身也应是外部存储构造；FXAA 需要临时缓冲，与免堆互斥）。
    ForwardPipeline(int width, int height, float* depthStorage)
        : width_(width), height_(height),
          mainRenderer_(std::make_unique<Renderer>(width, height, depthStorage))
    {
    }

    int width() const { return width_; }
    int height() const { return height_; }

    // ---- 帧参数（每帧渲染前设置）----
    Mat4 view = Mat4::identity();
    Mat4 proj = Mat4::identity();
    Vec3f cameraPos{0, 0, 3};
    Color clearColor{20, 30, 40};
    bool cullBackface = true;

    // ---- 阴影（可选）----
    bool shadowEnabled = false;
    int shadowResolution = 1024;
    // lightProj * lightView（不含 model）。casters 的完整链见 ForwardDraw::lightMVP。
    Mat4 shadowLightVP = Mat4::identity();

    // ---- 后处理（可选）----
    bool fxaaEnabled = false;
    StripExecutor postExecutor{};

    void clearDraws() { draws_.clear(); }
    void submit(const ForwardDraw& draw) { draws_.push_back(draw); }

    // 阴影贴图访问。指向门面内部缓冲，地址跨帧稳定：自定义 Shader 可在
    // 首帧前一次性接入（FinalShader::shadowMap / lightVP）。
    // 阴影未启用或首帧未渲染前返回 nullptr。
    const DepthBuffer* shadowDepth() const { return shadowRenderer_ ? &shadowRenderer_->depth() : nullptr; }
    int drawnShadowCasters() const { return lastShadowCasters_; }

    // 预创建阴影贴图（可选）：首帧前调用一次，让 shader 能先拿到稳定指针。
    // 不调用也可以——render() 内部会在需要时自行创建。
    void ensureShadowMap()
    {
        const int res = shadowResolution > 0 ? shadowResolution : 1024;
        if (!shadowRenderer_ || shadowRenderer_->width() != res)
            shadowRenderer_ = std::make_unique<Renderer>(res, res);
        if (!shadowTarget_ || shadowTarget_->width() != res)
            shadowTarget_ = std::make_unique<Framebuffer>(res, res);
    }

    // 渲一帧。fb 为 RGBA8888（FXAA 作用于 8 位图像；其他格式请直接用
    // Renderer::drawMesh 走通用模板路径）。
    void render(Framebuffer& fb)
    {
        // 1. 阴影贴图（在主 pass 之前：shader 已持有本门面阴影图的稳定指针）
        if (shadowEnabled)
            renderShadowMapInternal();

        // 2. 清屏 + 队列排序：不透明近→远（利于深度裁剪），透明远→近
        mainRenderer_->clear(fb, clearColor);
        mainRenderer_->cullBackface = cullBackface;
        buildQueue();
        queue_.sort(cameraPos, positions_);

        // 3. 主 pass：按排序逐项绘制
        for (const DrawItem& item : queue_.items())
        {
            const ForwardDraw& draw = draws_[size_t(item.objectIndex)];
            if (draw.mesh && draw.shader)
                mainRenderer_->drawMesh(fb, *draw.mesh, *draw.shader);
        }

        // 4. 后处理链
        if (fxaaEnabled)
            applyFxaa(fb);
    }

   private:
    void buildQueue()
    {
        queue_.clear();
        positions_.clear();
        positions_.reserve(draws_.size());
        for (const ForwardDraw& draw : draws_)
        {
            DrawItem item;
            item.layer = draw.layer;
            item.materialId = draw.materialId;
            item.objectIndex = int(positions_.size());
            queue_.add(item);
            positions_.push_back(draw.sortCenter);
        }
    }

    void renderShadowMapInternal()
    {
        ensureShadowMap();

        std::vector<ShadowCaster> casters;
        casters.reserve(draws_.size());
        for (const ForwardDraw& draw : draws_)
            if (draw.castShadow && draw.mesh)
                casters.push_back({draw.mesh, draw.lightMVP});
        lastShadowCasters_ = int(casters.size());

        renderShadowPass(*shadowRenderer_, *shadowTarget_, casters);
    }

    void applyFxaa(Framebuffer& fb)
    {
        const int W = fb.width(), H = fb.height();
        if (W <= 0 || H <= 0)
            return;
        if (!fxaaScratch_ || fxaaScratch_->width() != W || fxaaScratch_->height() != H)
            fxaaScratch_ = std::make_unique<Framebuffer>(W, H);
        Framebuffer& scratch = *fxaaScratch_;

        if (lumMap_.size() < size_t(W) * size_t(H))
            lumMap_.resize(size_t(W) * size_t(H));

        auto runStrips = [&](const std::function<void(int, int)>& body) {
            if (postExecutor)
                postExecutor(H, body);
            else
                serialStripExecutor(H, body);
        };
        runStrips([&](int y0, int y1) { postprocess::buildLuminanceMapRows(fb, lumMap_, y0, y1); });
        runStrips([&](int y0, int y1) { postprocess::fxaaRows(fb, scratch, lumMap_, y0, y1); });

        // FXAA 需要 src/dst 分离（原地写会污染未处理的输入行），算完拷回
        for (int y = 0; y < H; ++y)
            std::memcpy(fb.row(y), scratch.row(y), size_t(W) * sizeof(Color));
    }

    int width_, height_;
    std::unique_ptr<Renderer> mainRenderer_;
    std::unique_ptr<Renderer> shadowRenderer_;
    std::unique_ptr<Framebuffer> shadowTarget_;
    std::unique_ptr<Framebuffer> fxaaScratch_;
    std::vector<float> lumMap_;

    std::vector<ForwardDraw> draws_;
    RenderQueue queue_;
    std::vector<Vec3f> positions_;
    int lastShadowCasters_ = 0;
};

}  // namespace sr
