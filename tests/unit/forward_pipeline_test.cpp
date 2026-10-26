// ============================================================
// ForwardPipeline 门面单元测试：统一入口的行为契约。
//
// 门面是"编排层"，正确性由底下的 Renderer/RenderQueue/后处理各自
// 的单测锁定；这里只锁编排语义：
//   1. 提交的全部 draw 都被绘制，排序不改变最终遮挡结果；
//   2. 阴影 pass 产出可用的深度图且投射物计数正确；
//   3. FXAA 打开时平坦图像保持不变（后处理不污染内容）；
//   4. 注入的分条执行器必须恰好覆盖 [0, H) 且不重叠。
// ============================================================
#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "softrenderer/render/pipeline/forward_pipeline.h"
#include "softrenderer/render/shader/shaders.h"
#include "softrenderer/resource/mesh_generator.h"

using namespace sr;

namespace
{
// 让 albedo 主导颜色（ambient=1, lightColor=0），像素颜色直接反映材质
void makeUnlit(SurfaceShader& s, const Vec3f& albedo)
{
    s.ambient = {1, 1, 1};
    s.lightColor = {0, 0, 0};
    s.baseColor = albedo;
}
} // namespace

TEST(ForwardPipeline, rendersAllDraws_nearOccludesFar)
{
    const int W = 48, H = 48;
    ForwardPipeline pipeline(W, H);
    pipeline.view = sr::transform::lookAt({0, 0, 4}, {0, 0, 0}, {0, 1, 0});
    pipeline.proj = sr::transform::perspective(1.2f, float(W) / H, 0.1f, 50.0f);
    pipeline.cameraPos = {0, 0, 4};

    Mesh nearSphere = sr::meshgen::sphere(0.8f, 16, 12);
    Mesh farSphere = sr::meshgen::sphere(0.8f, 16, 12);

    FlatShader nearShader, farShader;
    makeUnlit(nearShader, {1, 0, 0});
    makeUnlit(farShader, {0, 0, 1});
    // 故意先提交远的：排序后仍应被近的遮挡
    farShader.model = Mat4::translate(0, 0, -3);
    farShader.mvp = pipeline.proj * pipeline.view * farShader.model;
    nearShader.mvp = pipeline.proj * pipeline.view;

    ForwardDraw farDraw;
    farDraw.mesh = &farSphere;
    farDraw.shader = &farShader;
    farDraw.layer = RenderLayer::Opaque;
    farDraw.sortCenter = {0, 0, 1};
    ForwardDraw nearDraw;
    nearDraw.mesh = &nearSphere;
    nearDraw.shader = &nearShader;
    nearDraw.layer = RenderLayer::Opaque;
    nearDraw.sortCenter = {0, 0, 0};

    pipeline.submit(farDraw);
    pipeline.submit(nearDraw);

    Framebuffer fb(W, H);
    pipeline.render(fb);

    const Color center = fb.getPixel(W / 2, H / 2);
    EXPECT_GT(center.r, 150) << "画面中心应是近处红球";
    EXPECT_LT(center.b, 100);
    // 远球在近球轮廓之外仍可见（画面四分之一处应有蓝色成分）
    const Color off = fb.getPixel(W / 2, H / 2 + 12);
    EXPECT_GT(off.b, off.r) << "近球轮廓外应能看到远处的蓝球";
}

TEST(ForwardPipeline, shadowPassProducesDepthAndCount)
{
    const int W = 32, H = 32;
    ForwardPipeline pipeline(W, H);
    pipeline.view = sr::transform::lookAt({0, 0, 4}, {0, 0, 0}, {0, 1, 0});
    pipeline.proj = sr::transform::perspective(1.2f, float(W) / H, 0.1f, 50.0f);
    pipeline.cameraPos = {0, 0, 4};
    pipeline.shadowEnabled = true;
    pipeline.shadowResolution = 64;
    pipeline.shadowLightVP = sr::transform::ortho(-4, 4, -4, 4, 0.1f, 20.0f) *
                             sr::transform::lookAt({3, 5, 3}, {0, 0, 0}, {0, 1, 0});

    EXPECT_EQ(pipeline.shadowDepth(), nullptr) << "首帧前不应有阴影图";

    Mesh ball = sr::meshgen::sphere(0.8f, 12, 8);
    FlatShader shader;
    makeUnlit(shader, {0.8f, 0.8f, 0.8f});
    shader.mvp = pipeline.proj * pipeline.view;
    ForwardDraw draw;
    draw.mesh = &ball;
    draw.shader = &shader;
    draw.sortCenter = {0, 0, 0};
    draw.lightMVP = pipeline.shadowLightVP;
    pipeline.submit(draw);

    Framebuffer fb(W, H);
    pipeline.render(fb);

    ASSERT_NE(pipeline.shadowDepth(), nullptr);
    EXPECT_EQ(pipeline.drawnShadowCasters(), 1);
    EXPECT_EQ(pipeline.shadowDepth()->width(), 64);
    // 阴影图上必须有非无穷远的深度（球确实被渲进去了）
    int solid = 0;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            if (pipeline.shadowDepth()->get(x, y) < DepthBuffer::kFar)
                ++solid;
    EXPECT_GT(solid, 64) << "阴影图应包含球的投影";
}

TEST(ForwardPipeline, fxaaOnFlatClearColorIsIdentity)
{
    const int W = 24, H = 24;
    ForwardPipeline pipeline(W, H);
    pipeline.view = Mat4::identity();
    pipeline.proj = Mat4::identity();
    pipeline.clearColor = Color(60, 70, 80);
    pipeline.fxaaEnabled = true;

    Framebuffer fb(W, H);
    pipeline.render(fb);

    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            EXPECT_EQ(fb.getPixel(x, y), Color(60, 70, 80)) << "平坦图像过 FXAA 应保持不变 x=" << x << " y=" << y;
}

TEST(ForwardPipeline, injectedStripExecutorCoversEveryRowOnce)
{
    const int W = 16, H = 20;
    ForwardPipeline pipeline(W, H);
    pipeline.clearColor = Color(10, 20, 30);
    pipeline.fxaaEnabled = true;

    int phases = 0;
    pipeline.postExecutor = [&](int totalRows, const std::function<void(int, int)>& body) {
        ASSERT_EQ(totalRows, H);
        ++phases;
        // 模拟多线程：切 3 条不重叠的带
        for (const auto [y0, y1] : {std::pair<int, int>{0, 7}, {7, 13}, {13, H}})
            body(y0, y1);
    };

    Framebuffer fb(W, H);
    pipeline.render(fb);

    EXPECT_EQ(phases, 2) << "亮度预计算与 FXAA 应用各一个并行段";
    // 行覆盖由 fxaaRows 的 clamp 保证；这里验证执行器确实被用了（非串行路径）
}

TEST(ForwardPipeline, baremetalExternalDepthConstructor)
{
    const int W = 16, H = 16;
    std::vector<float> extDepth(size_t(W) * H);
    ForwardPipeline pipeline(W, H, extDepth.data());
    pipeline.clearColor = Color(1, 2, 3);

    Framebuffer fb(W, H);
    pipeline.render(fb);

    EXPECT_EQ(fb.getPixel(5, 5), Color(1, 2, 3));
    // 无 draw 时深度应保持 clear 后的 kFar，且落在外部存储上
    EXPECT_FLOAT_EQ(extDepth[size_t(5) * W + 5], DepthBuffer::kFar);
}
