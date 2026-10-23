// ============================================================
// 端到端像素回归测试
//
// 【为什么需要】tests/unit 下的 113 个用例全是**局部性质断言**
// （某像素是否被涂色、某点积是否为零、某矩阵是否正交）。它们无法回答
// 「整条渲染管线跑完，画面是不是对的」这个问题。
//
// rasterizer_fast 的深度插值重心权重错位（见 docs/tasks/DONE/TASK-002）
// 能长期潜伏，正是因为缺这张网：单元测试只断言了「中心像素被涂白」，
// 而深度值算错了 0.325 都不会被发现。
//
// 【三个场景各锁定什么】
//   zbuffer_twoOverlappingCubes  → 深度测试 + 背面剔除 + 视锥裁剪的联合行为
//   pipeline_phongTexturedSphere → 变换链 + 透视校正插值 + 纹理采样 + Phong 着色
//   blend2d_alphaComposition     → alpha 混合 + sr2d 图元 + 变换栈 + scissor
//
// 【确定性要求】场景必须无随机、无时间依赖、无多线程顺序依赖。
// 因此刻意**不**回归 demo_stars（含随机星点）与 stage14_threaded（线程顺序不定）。
//
// 【基准图维护】
//   基准图在 tests/fixtures/golden/<场景名>.tga，已纳入版本控制。
//   有意变更视觉输出时：配置加 -DSR_UPDATE_GOLDEN=ON → 跑一次测试 → 提交新基准图，
//   并在 commit 信息中说明视觉变更原因。
//   基准图缺失时测试**直接失败**（而非静默创建），避免 CI 假通过。
//
// 【跨平台注意】浮点结果在不同编译器间可能有微差。compare() 的容差是
// 「不匹配像素 < 0.1% 且平均差 < 0.5」。若三平台不稳定，改为只在单一平台跑。
// ============================================================
#include <gtest/gtest.h>

#include "softrenderer/core/blend.h"
#include "softrenderer/core/framebuffer.h"
#include "softrenderer/math/rect.h"
#include "softrenderer/render/pipeline/renderer.h"
#include "softrenderer/render/draw2d/renderer2d.h"
#include "softrenderer/scene/camera.h"
#include "softrenderer/resource/mesh.h"
#include "softrenderer/resource/texture.h"
#include "softrenderer/render/shader/shaders.h"
#include "softrenderer/resource/mesh_generator.h"
#include "softrenderer/resource/texture_generator.h"
#include "support/pixel_regression.h"

using namespace sr;

namespace {

// 把 CompareResult 转成可读的失败信息。
// 独立成函数而非在每个用例里重复：三处断言逻辑完全相同，
// 且失败信息必须带上基准图路径与更新方式，否则排查时无从下手。
void expectMatchesGolden(const char* sceneName, const Framebuffer& fb)
{
    const regression::CompareResult r = regression::compareWithGolden(sceneName, fb);
    EXPECT_TRUE(r.passed) << "场景 [" << sceneName << "] 与基准图不符\n"
                          << "  不匹配像素: " << r.mismatchedPixels << " / " << r.totalPixels << "（占比 "
                          << r.mismatchRatio << "，阈值 0.001）\n"
                          << "  最大单通道差: " << r.maxDelta << "（像素阈值 2.0）\n"
                          << "  平均差: " << r.avgDelta << "（阈值 0.5）\n"
                          << "  基准图: " << regression::goldenPath(sceneName) << "\n"
                          << "  若为有意的视觉变更：配置加 -DSR_UPDATE_GOLDEN=ON 重跑一次以更新基准，"
                          << "并在 commit 信息中说明变更原因。";
}

// 回归场景统一用小分辨率：基准图体积小（每张约 49 KB）、渲染快，
// 且 128x96 已足够暴露深度/裁剪/插值/混合类回归。
constexpr int kRegressionWidth = 128;
constexpr int kRegressionHeight = 96;

}  // namespace

// 深度测试 + 背面剔除 + 视锥裁剪。
// 近处立方体与远处立方体在屏幕上重叠，且远处那个更大 —— 若深度测试失效，
// 遮挡关系会整个反过来；若背面剔除失效，立方体内部面会透出。
TEST(RenderRegression, zbuffer_twoOverlappingCubes)
{
    Framebuffer fb(kRegressionWidth, kRegressionHeight);
    Renderer renderer(kRegressionWidth, kRegressionHeight);

    Camera cam;
    cam.position = {0.0f, 1.5f, 6.0f};
    cam.pitch = -0.15f;
    cam.aspect = float(kRegressionWidth) / kRegressionHeight;

    PhongShader shader;
    shader.view = cam.viewMatrix();
    shader.proj = cam.projMatrix();
    shader.lightDir = Vec3f{0.4f, 0.9f, 0.5f}.normalized();
    shader.cameraPos = cam.position;

    renderer.clear(fb, Color(30, 34, 44));
    renderer.cullBackface = true;

    // 近处小红立方体，稍微偏左，部分遮挡远处的大蓝立方体
    const Mesh nearBox = sr::meshgen::cube(1.6f);
    shader.model = Mat4::translate(-0.7f, 0.8f, 1.5f) * Mat4::rotateY(0.5f);
    shader.mvp = shader.proj * shader.view * shader.model;
    shader.normalMatrix = shader.model.inversed().transposed();
    shader.baseColor = {0.9f, 0.35f, 0.3f};
    shader.texture = nullptr;
    renderer.drawMesh(fb, nearBox, shader);

    // 远处大蓝立方体，位置更靠后、更靠右
    const Mesh farBox = sr::meshgen::cube(2.6f);
    shader.model = Mat4::translate(1.1f, 1.2f, -2.0f) * Mat4::rotateY(-0.6f);
    shader.mvp = shader.proj * shader.view * shader.model;
    shader.normalMatrix = shader.model.inversed().transposed();
    shader.baseColor = {0.35f, 0.55f, 0.95f};
    renderer.drawMesh(fb, farBox, shader);

    expectMatchesGolden("zbuffer_twoOverlappingCubes", fb);
}

// 变换链 + 透视校正插值 + 纹理采样 + Phong 着色。
// 用 uvGrid 纹理：它的坐标线密度随 UV 变化，能同时暴露
// 插值错误（线会扭曲）与纹理采样错误（线会错位）。
// 球体刻意放在画面边缘并部分超出视口，以覆盖裁剪与包围盒 clamp 路径。
TEST(RenderRegression, pipeline_phongTexturedSphere)
{
    Framebuffer fb(kRegressionWidth, kRegressionHeight);
    Renderer renderer(kRegressionWidth, kRegressionHeight);

    Camera cam;
    cam.position = {0.0f, 0.8f, 4.2f};
    cam.pitch = -0.1f;
    cam.yaw = -1.8f;  // 非默认朝向，让球体落在画面偏侧位置
    cam.aspect = float(kRegressionWidth) / kRegressionHeight;

    const Texture uvgrid(sr::texgen::uvGrid(128, 8));

    PhongShader shader;
    shader.view = cam.viewMatrix();
    shader.proj = cam.projMatrix();
    shader.lightDir = Vec3f{-0.3f, 0.8f, 0.5f}.normalized();
    shader.cameraPos = cam.position;
    shader.texture = &uvgrid;
    shader.shininess = 48.0f;

    renderer.clear(fb, Color(18, 22, 30));
    renderer.cullBackface = true;

    // 分段数固定：改变分段数会改变几何，基准图自然失效
    const Mesh ball = sr::meshgen::sphere(1.3f, 32, 20);
    shader.model = Mat4::translate(0.9f, 0.6f, 0.0f);
    shader.mvp = shader.proj * shader.view * shader.model;
    shader.normalMatrix = shader.model.inversed().transposed();
    shader.baseColor = {1.0f, 1.0f, 1.0f};
    renderer.drawMesh(fb, ball, shader);

    expectMatchesGolden("pipeline_phongTexturedSphere", fb);
}

// alpha 混合 + sr2d 图元 + 变换栈 + scissor。
// 三种混合模式叠加绘制，能暴露 blend.h 的取整/钳制变更
// （TASK-003 把截断改成四舍五入，本场景就是它的视觉回归网）。
TEST(RenderRegression, blend2d_alphaComposition)
{
    Framebuffer fb(kRegressionWidth, kRegressionHeight, Color(24, 28, 38));
    Renderer2D r;
    r.begin(fb);

    // 不透明底层：渐变矩形铺满画面下半部
    r.setBlend(BlendMode::Opaque);
    r.setColor(Color(60, 90, 130));
    r.fillRect(0.0f, 0.0f, float(kRegressionWidth), float(kRegressionHeight) * 0.5f);

    // 半透明叠加：三个圆分别用不同混合模式，位置刻意重叠
    r.setBlend(BlendMode::AlphaBlend);
    r.setColor(Color(220, 80, 60, 140));
    r.fillCircle(44.0f, 40.0f, 22.0f);

    r.setBlend(BlendMode::Additive);
    r.setColor(Color(60, 200, 120, 160));
    r.fillCircle(64.0f, 52.0f, 20.0f);

    r.setBlend(BlendMode::Premultiplied);
    r.setColor(Color(90, 110, 230, 180));
    r.fillCircle(84.0f, 40.0f, 18.0f);

    // 变换栈 + scissor：旋转的矩形被裁剪框切掉一角
    r.push();
    r.setBlend(BlendMode::AlphaBlend);
    r.setColor(Color(240, 220, 90, 200));
    r.translate(32.0f, 74.0f);
    r.rotate(0.4f);
    r.setScissor(Recti(16, 62, 40, 26));
    r.fillRect(-18.0f, -9.0f, 36.0f, 18.0f);
    r.pop();

    // 文字：验证点阵字体与 deviceTransform 缓存路径
    r.clearScissor();
    r.setBlend(BlendMode::Opaque);
    r.setColor(sr::colors::white);
    r.text(8.0f, 8.0f, "REGRESSION");

    r.end();

    expectMatchesGolden("blend2d_alphaComposition", fb);
}
