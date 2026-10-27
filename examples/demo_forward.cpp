// ============================================================
// Demo：ForwardPipeline 统一前向管线门面
//
// 一条 render() 调用串起：阴影贴图 pass → 队列排序 → 主 pass →
// FXAA 后处理（线程池分条并行）。对照 stage10 可以看到：手写
// 两趟渲染的所有样板（阴影 Renderer、DepthShader 循环、排序、
// 后处理缓冲管理）都收进了门面。
//
// 输出：output/demo_forward.tga
// ============================================================
#include <cstdio>
#include <filesystem>
#include <memory>

#include "softrenderer/core/framebuffer.h"
#include "softrenderer/io/tga.h"
#include "softrenderer/render/pipeline/forward_pipeline.h"
#include "softrenderer/render/shader/advanced_shaders.h"
#include "softrenderer/resource/mesh_generator.h"
#include "softrenderer/runtime/thread_pool.h"

using namespace sr;

int main()
{
    const int W = 800, H = 600;

    // ---- 调度器：把后处理条带喂给线程池（render 组件本身不碰线程）----
    ThreadPool pool;
    StripExecutor parallelStrips = [&](int totalRows, const std::function<void(int, int)>& body) {
        const int strips = pool.threadCount();
        for (int i = 0; i < strips; ++i)
        {
            const auto [y0, y1] = postprocess::stripRows(totalRows, i, strips);
            if (y1 > y0)
                pool.submit([&body, y0, y1] { body(y0, y1); });
        }
        pool.waitAll();
    };

    // ---- 门面与帧参数 ----
    ForwardPipeline pipeline(W, H);
    pipeline.view = sr::transform::lookAt({4.5f, 4.2f, 7.5f}, {0, 0.5f, 0}, {0, 1, 0});
    pipeline.proj = sr::transform::perspective(0.9f, float(W) / H, 0.1f, 100.0f);
    pipeline.cameraPos = {4.5f, 4.2f, 7.5f};
    pipeline.clearColor = Color(135, 160, 200);
    pipeline.shadowEnabled = true;
    pipeline.shadowResolution = 1024;
    pipeline.fxaaEnabled = true;
    pipeline.postExecutor = parallelStrips;

    // ---- 阴影矩阵（光 proj * 光 view，不含 model）----
    const Vec3f lightDir = Vec3f{0.5f, 0.5f, 0.32f}.normalized();
    pipeline.shadowLightVP = sr::transform::ortho(-9, 9, -9, 9, 0.1f, 30.0f) *
                             sr::transform::lookAt(Vec3f{0, 0.5f, 0} + lightDir * 12.0f, {0, 0.5f, 0}, {0, 1, 0});

    // ---- 着色器与几何 ----
    // 先创建阴影贴图拿到稳定地址，再接线到着色器
    pipeline.ensureShadowMap();
    FinalShader groundShader, ballShader, boxShader;
    for (auto* s : {&groundShader, &ballShader, &boxShader})
    {
        s->view = pipeline.view;
        s->proj = pipeline.proj;
        s->lightDir = lightDir;
        s->cameraPos = pipeline.cameraPos;
        s->ambient = {0.22f, 0.24f, 0.30f};
        s->shininess = 48.0f;
        // 阴影采样端：门面的阴影图地址跨帧稳定，首帧前接一次即可
        s->shadowMap = pipeline.shadowDepth();
        s->lightVP = pipeline.shadowLightVP;
    }
    groundShader.baseColor = {1, 1, 1};
    ballShader.baseColor = {0.55f, 0.75f, 0.95f};
    boxShader.baseColor = {0.9f, 0.45f, 0.4f};

    Mesh ground = sr::meshgen::planeGrid(24.0f, 24, 6.0f);
    Mesh ball = sr::meshgen::sphere(1.1f, 48, 32);
    Mesh box = sr::meshgen::cube(1.8f);

    // ---- 提交（排序：不透明近→远由门面完成）----
    auto submit = [&](const Mesh& mesh, Shader& shader, const Mat4& model, const Vec3f& center) {
        ForwardDraw draw;
        draw.mesh = &mesh;
        draw.shader = &shader;
        draw.sortCenter = center;
        draw.model = model;                        // 视锥剔除用（与 shader.model 保持一致）
        draw.lightMVP = pipeline.shadowLightVP * model;
        pipeline.submit(draw);
    };
    submit(ground, groundShader, Mat4::identity(), {0, 0, 0});
    submit(ball, ballShader, Mat4::translate(0.6f, 1.1f, -0.3f), {0.6f, 1.1f, -0.3f});
    submit(box, boxShader, Mat4::translate(-2.6f, 0.9f, 0.5f) * Mat4::rotateY(0.5f), {-2.6f, 0.9f, 0.5f});

    // ---- 渲染 + 写出 ----
    Framebuffer fb(W, H);
    pipeline.render(fb);

    // 着色器持有的是 pipeline 内部阴影图指针：render 之后指针仍有效
    std::filesystem::create_directories("output");
    sr::io::writeTga("output/demo_forward.tga", fb);
    std::printf("已生成 output/demo_forward.tga（阴影投射物 %d 个）\n", pipeline.drawnShadowCasters());
    return 0;
}
