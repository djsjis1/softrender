#pragma once
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

#include "softrenderer/core/framebuffer.h"
#include "softrenderer/core/pixel_format.h"
#include "softrenderer/math/frustum.h"
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
    // 模型的局部→世界变换。仅用于视锥剔除：判定矩阵为 viewProj * model。
    // ⚠ 若顶点已被预先变换到世界空间，保持单位阵即可（此时 mesh 包围盒就是世界包围盒）。
    // 若你把变换写进了 shader.model 却没有同步到这里，剔除会拿未变换的范围去判——
    // 想避开这个隐患就把 frustumCull 关掉。
    Mat4 model = Mat4::identity();
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

    // 裸机路径：主深度缓冲用外部存储。
    //
    // 能保证的是**渲染调用路径的稳态零分配**：预热一帧后 render() 不再分配
    // （顶点缓存按 epoch 复用、阴影 casters 向量预留容量、阴影图尺寸不变）。
    // 不包含的部分必须知道：门面自身的 Renderer 与阴影图是 make_unique 出来的
    // （构造或首次使用时一次），FXAA 的临时缓冲与亮度图同样要分配——需要整对象
    // 零堆时应直接用 Renderer + 外部存储自己编排。
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

    // ---- 视锥剔除 ----
    // 整块包围盒都在画面外的网格不再进入管线（顶点着色、裁剪、光栅化全跳过）。
    // 判定矩阵用本帧的 view*proj 乘 draw.shader->model，不读 shader.mvp：
    // 门面自己持有 view/proj，不依赖调用方有没有把 mvp 写对。
    // 保守实现：只剔“整块肯定看不见”的，斜放的长条物体可能漏剔（只是慢一点）。
    bool frustumCull = true;
    // 上一帧被剔除的 draw 数，供测试与统计使用。
    int culledDraws() const { return lastCulled_; }

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

    // 阴影贴图访问。指向门面内部缓冲：**只要 shadowResolution 不变**，地址跨帧
    // 稳定，自定义 Shader 可在首帧前一次性接入（FinalShader::shadowMap / lightVP）。
    //
    // ⚠ 改 shadowResolution 会重建阴影 Renderer，此前取得的指针当场失效（同帧内
    // 采样就是悬垂读）。约定：持有者在渲染前比对 shadowMapVersion()，发现变化必须
    // 重新取指针。
    // 阴影未启用或首帧未渲染前返回 nullptr。
    const DepthBuffer* shadowDepth() const { return shadowRenderer_ ? &shadowRenderer_->depth() : nullptr; }
    // 阴影图身份版本：每次因分辨率变化而重建时 +1。
    uint32_t shadowMapVersion() const { return shadowMapVersion_; }
    int drawnShadowCasters() const { return lastShadowCasters_; }

    // 预创建阴影贴图（可选）：首帧前调用一次，让 shader 能先拿到稳定指针。
    // 不调用也可以——render() 内部会在需要时自行创建。
    void ensureShadowMap()
    {
        const int res = shadowResolution > 0 ? shadowResolution : 1024;
        if (!shadowRenderer_ || shadowRenderer_->width() != res)
        {
            shadowRenderer_ = std::make_unique<Renderer>(res, res);
            // 指针身份变了：外部持有的 shadowDepth() 全部作废
            ++shadowMapVersion_;
        }
        // 哑目标只承载「颜色输出无意义」的写入，用 1 字节/像素的 Gray8：
        // 1024² 从 4 MB 降到 1 MB，每帧被写的一百万个字节也不再是 4 倍浪费。
        if (!shadowTarget_ || shadowTarget_->width() != res)
            shadowTarget_ = std::make_unique<BasicFramebuffer<Gray8>>(res, res);
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

        // 3. 主 pass：先视锥剔除，再按排序逐项绘制
        const Mat4 viewProj = proj * view;
        lastCulled_ = 0;
        for (const DrawItem& item : queue_.items())
        {
            const ForwardDraw& draw = draws_[size_t(item.objectIndex)];
            if (!draw.mesh || !draw.shader)
                continue;
            if (frustumCull && !isMeshVisible(draw, viewProj))
            {
                ++lastCulled_;
                continue;
            }
            mainRenderer_->drawMesh(fb, *draw.mesh, *draw.shader);
        }

        // 4. 后处理链
        if (fxaaEnabled)
            applyFxaa(fb);
    }

   private:
    // 整块网格是否可能可见。空网格（没有顶点）一律放行，交给 drawMesh 自己处理。
    // 判定矩阵取 viewProj * draw.model，不读 shader 里的 matrix：
    // 基类 Shader 刻意不带 uniform（见 render/pipeline/shader.h 的设计说明）。
    static bool isMeshVisible(const ForwardDraw& draw, const Mat4& viewProj)
    {
        const Mesh::Bounds& b = draw.mesh->bounds();
        if (b.empty)
            return true;
        return Frustum::fromMVP(viewProj * draw.model).intersectsAabb(b.min, b.max);
    }

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

        // casters_ 是成员而不是每帧新建：阴影开启时每帧一次 vector 分配
        // 会直接打穿「渲染路径稳态零分配」。
        if (casters_.capacity() < draws_.size())
            casters_.reserve(draws_.size());
        casters_.clear();
        for (const ForwardDraw& draw : draws_)
            if (draw.castShadow && draw.mesh)
                casters_.push_back({draw.mesh, draw.lightMVP});
        lastShadowCasters_ = int(casters_.size());

        renderShadowPass(*shadowRenderer_, *shadowTarget_, casters_);
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
        // 三个并行段：拷入 scratch → 算亮度图 → FXAA 直接写回 fb。
        // FXAA 需要 src/dst 分离（原地写会污染未处理的输入行），所以先把整帧搬到
        // scratch；这次拷贝放进并行段，原来「算完再整帧 memcpy 回 fb」的串行搬运
        // （1080p 约 8 MB）就省掉了。
        runStrips([&](int y0, int y1) {
            for (int y = y0; y < y1; ++y)
                std::memcpy(scratch.row(y), fb.row(y), size_t(W) * sizeof(Color));
        });
        runStrips([&](int y0, int y1) { postprocess::buildLuminanceMapRows(scratch, lumMap_, y0, y1); });
        runStrips([&](int y0, int y1) { postprocess::fxaaRows(scratch, fb, lumMap_, y0, y1); });
    }

    int width_, height_;
    std::unique_ptr<Renderer> mainRenderer_;
    std::unique_ptr<Renderer> shadowRenderer_;
    // 阴影哑目标：颜色无人读取，用 1 字节/像素的 Gray8（1024² 仅 1 MB）
    std::unique_ptr<BasicFramebuffer<Gray8>> shadowTarget_;
    std::unique_ptr<Framebuffer> fxaaScratch_;
    std::vector<float> lumMap_;

    std::vector<ForwardDraw> draws_;
    RenderQueue queue_;
    std::vector<Vec3f> positions_;
    std::vector<ShadowCaster> casters_;  // 复用：避免阴影开启时每帧分配
    int lastShadowCasters_ = 0;
    int lastCulled_ = 0;
    // 阴影图身份版本，供持有 shadowDepth() 指针的一方检测失效
    uint32_t shadowMapVersion_ = 0;
};

}  // namespace sr
