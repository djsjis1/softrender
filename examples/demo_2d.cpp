// ============================================================
// 演示：2D 渲染子系统
//
// 本示例展示软渲染器“既做 3D 也做 2D”的能力，分两个场景离屏出图：
//
//   A) output/demo_2d_scene.png —— 纯 2D 场景
//      渐变天空 / 太阳发光 / 远山多边形 / UI 面板(圆角+半透明) /
//      血条进度条 / 精灵(字符模板) / 变换栈(旋转缩放) / 位图缩放旋转 /
//      scissor 裁剪 / 内置位图字体文字
//
//   B) output/demo_2d_hud.png —— 2D HUD 叠加在 3D 场景之上
//      先用 3D Renderer 画几何体，再用 Renderer2D 以 alpha 混合叠加
//      准星 / 血条 / 小地图 / 角标 / 文字，演示 2D 与 3D 共用一个 Framebuffer
//
// 运行： demo_2d  （无需窗口，直接写图到 output/）
// ============================================================
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "core/framebuffer.h"
#include "core/image_write.h"
#include "core/tga.h"
#include "raster/blit2d.h"
#include "raster/renderer.h"     // 3D 渲染器
#include "raster/renderer2d.h"   // 2D 渲染器
#include "raster/shape2d.h"
#include "scene/camera.h"
#include "scene/mesh.h"
#include "scene/texture.h"
#include "shader/shaders.h"

namespace
{

// 经典“太空侵略者”字符模板精灵（11x8），'.' 透明、'X' 上色
const std::vector<std::string> kInvader = {
    "..X.....X..",
    "...X...X...",
    "..XXXXXXX..",
    ".XX.XXX.XX.",
    "XXXXXXXXXXX",
    "X.XXXXXXX.X",
    "X.X.....X.X",
    "...XX.XX...",
};

// ------------------------------------------------------------
// 场景 A：纯 2D 展示
// ------------------------------------------------------------
void renderScene2D(Framebuffer& fb)
{
    const int W = fb.width(), H = fb.height();
    Renderer2D r2d;
    r2d.begin(fb); // 绑定目标，复位变换/裁剪

    // ---- 1. 天空：竖直渐变（下暖上冷）----
    r2d.setBlend(BlendMode::Opaque);
    r2d.rectGradient(0, 0, (float)W, (float)H, Color(255, 170, 110), Color(40, 58, 120), false);

    // ---- 2. 太阳：实心圆 + 加法混合光晕 ----
    r2d.setBlend(BlendMode::Additive);
    for (int i = 4; i >= 1; --i)
    {
        r2d.setColor(Color(255, 190, 90, (uint8_t)(30 * i)));
        r2d.fillCircle(740.0f, 430.0f, 40.0f + 18.0f * i);
    }
    r2d.setBlend(BlendMode::Opaque);
    r2d.setColor(Color(255, 236, 170));
    r2d.fillCircle(740.0f, 430.0f, 42.0f);

    // ---- 3. 远山：两个多边形，越远越暗 ----
    r2d.setColor(Color(70, 74, 110));
    r2d.fillPolygon({{0, 210}, {160, 330}, {320, 210}, {470, 320}, {640, 200}, {820, 300}, {960, 220},
                     {960, 0}, {0, 0}});
    r2d.setColor(Color(48, 50, 82));
    r2d.fillPolygon({{0, 150}, {200, 250}, {420, 150}, {620, 240}, {820, 160}, {960, 230}, {960, 0}, {0, 0}});

    // ---- 4. 地面 ----
    r2d.setColor(Color(30, 28, 46));
    r2d.fillRect(0, 0, (float)W, 120.0f);

    // ---- 5. 精灵：一排不同缩放/染色的侵略者 ----
    std::vector<sr2d::SpritePalette> pal = {{'X', Color(120, 240, 140)}};
    for (int i = 0; i < 5; ++i)
    {
        r2d.setColor(Color(255, 255, 255, 255));
        // 逐个染色：从左到右由绿变黄
        Color tint = Color::lerp(Color(120, 240, 140), Color(240, 220, 90), i / 4.0f);
        pal[0].color = tint;
        r2d.setColor(colors::white); // sprite 内部用调色板色 * 当前 tint(白=不变)
        r2d.sprite(kInvader, pal, 60.0f + i * 90.0f, 40.0f, 4.0f);
    }

    // ---- 6. 变换栈：绕中心旋转缩放的方块阵列 ----
    r2d.push();
    r2d.translate(240.0f, 300.0f);
    for (int i = 0; i < 8; ++i)
    {
        r2d.rotate(3.14159265f / 8.0f);
        r2d.setColor(Color(255, 255, 255, 60));
        r2d.setBlend(BlendMode::AlphaBlend);
        r2d.fillRect(-10.0f, 40.0f, 20.0f, 70.0f);
    }
    r2d.pop();

    // ---- 7. UI 面板：圆角 + 半透明，内含血条与文字 ----
    r2d.setBlend(BlendMode::AlphaBlend);
    r2d.setColor(Color(10, 12, 24, 170));
    r2d.fillRoundedRect(30.0f, 470.0f, 360.0f, 110.0f, 12.0f);
    r2d.setColor(Color(255, 255, 255, 90));
    r2d.roundedRect(30.0f, 470.0f, 360.0f, 110.0f, 12.0f, 2.0f);

    // 标题文字（内置 5x7 字体，2 倍放大）
    r2d.setColor(Color(255, 220, 120));
    r2d.text(48.0f, 548.0f, "PLAYER 1", 2.0f);

    // 血条：底槽 + 当前值（渐变）
    r2d.setColor(Color(60, 20, 30));
    r2d.fillRoundedRect(48.0f, 512.0f, 320.0f, 20.0f, 8.0f);
    r2d.rectGradient(48.0f, 512.0f, 320.0f * 0.72f, 20.0f, Color(120, 220, 120), Color(60, 180, 90), true);
    r2d.setColor(Color(220, 230, 255));
    r2d.text(48.0f, 486.0f, "HP 72/100   SCORE 133700", 1.0f);

    // ---- 8. 位图：程序化棋盘图，缩放 + 旋转后贴到右侧 ----
    tga::Image checker = texgen::checkerboard(64, 8, Color(240, 240, 250), Color(70, 90, 160));
    r2d.setColor(colors::white);
    r2d.bilinear = true;
    {
        Mat3 xf = Mat3::translate(820.0f, 160.0f) * Mat3::rotate(0.4f) * Mat3::scale(1.6f) *
                  Mat3::translate(-32.0f, -32.0f);
        sr2d::blitImageTransformed(fb, checker, xf, colors::white, true, BlendMode::AlphaBlend);
    }
    r2d.bilinear = false;

    // ---- 9. scissor 裁剪：把内容限制在一个矩形窗口内 ----
    r2d.setScissor(Recti(600, 430, 320, 140));
    r2d.setColor(Color(20, 24, 40, 200));
    r2d.fillRect(600.0f, 430.0f, 320.0f, 140.0f);
    r2d.setColor(Color(90, 200, 255));
    for (int i = 0; i < 12; ++i)
        r2d.fillCircle(600.0f + i * 30.0f, 430.0f + (i % 2) * 100.0f, 26.0f); // 超出部分被裁掉
    r2d.setColor(Color(255, 255, 255));
    r2d.text(612.0f, 548.0f, "SCISSOR CLIP", 2.0f);
    r2d.clearScissor();

    // ---- 10. 描边几何：圆/椭圆/三角形/粗线 ----
    r2d.setBlend(BlendMode::AlphaBlend);
    r2d.setColor(Color(255, 255, 255, 200));
    r2d.circle(150.0f, 380.0f, 26.0f, 2.0f);
    r2d.fillEllipse(230.0f, 380.0f, 34.0f, 20.0f);
    r2d.setColor(Color(255, 120, 120, 220));
    r2d.fillTriangle(300.0f, 356.0f, 340.0f, 356.0f, 320.0f, 400.0f);
    r2d.setColor(Color(120, 220, 255, 220));
    r2d.line(380.0f, 360.0f, 520.0f, 400.0f, 4.0f);

    r2d.end();
}

// ------------------------------------------------------------
// 场景 B：2D HUD 叠加在 3D 场景之上
// ------------------------------------------------------------
void renderHudOver3D(Framebuffer& fb)
{
    const int W = fb.width(), H = fb.height();

    // ===== 3D 部分 =====
    Renderer renderer(W, H);
    renderer.clear(fb, Color(24, 26, 34));

    Camera cam;
    cam.position = {0, 2.2f, 7.0f};
    cam.pitch = -0.22f;
    cam.aspect = (float)W / H;

    Texture checker(texgen::checkerboard(256, 8, Color(210, 210, 220), Color(60, 64, 80)));

    PhongShader shader;
    shader.view = cam.viewMatrix();
    shader.proj = cam.projMatrix();
    shader.lightDir = Vec3f{0.4f, 0.9f, 0.5f}.normalized();
    shader.cameraPos = cam.position;
    shader.ambient = {0.18f, 0.2f, 0.26f};

    struct Obj
    {
        Mesh mesh;
        Vec3f pos;
        Vec3f color;
        float spin;
        const Texture* tex;
    };
    std::vector<Obj> objs = {
        {meshgen::cube(1.6f), {-2.4f, 0.8f, 0}, {0.90f, 0.40f, 0.35f}, 0.6f, nullptr},
        {meshgen::sphere(1.1f, 40, 28), {0.4f, 1.1f, 0}, {1, 1, 1}, 0.0f, &checker},
        {meshgen::cube(1.2f), {2.8f, 0.6f, -0.5f}, {0.40f, 0.60f, 0.95f}, -0.5f, nullptr},
    };
    for (const auto& o : objs)
    {
        Mat4 model = Mat4::translate(o.pos.x, o.pos.y, o.pos.z) * Mat4::rotateY(o.spin);
        shader.model = model;
        shader.mvp = shader.proj * shader.view * model;
        shader.normalMatrix = model.inversed().transposed();
        shader.baseColor = o.color;
        shader.texture = o.tex;
        renderer.cullBackface = (o.tex != &checker);
        renderer.drawMesh(fb, o.mesh, shader);
    }

    // ===== 2D HUD 叠加（同一个 Framebuffer，alpha 混合）=====
    Renderer2D hud;
    hud.begin(fb);
    hud.setBlend(BlendMode::AlphaBlend);

    // 准星（屏幕中心）
    float cx = W * 0.5f, cy = H * 0.5f;
    hud.setColor(Color(120, 255, 160, 220));
    hud.line(cx - 14, cy, cx - 4, cy, 2.0f);
    hud.line(cx + 4, cy, cx + 14, cy, 2.0f);
    hud.line(cx, cy - 14, cx, cy - 4, 2.0f);
    hud.line(cx, cy + 4, cx, cy + 14, 2.0f);
    hud.circle(cx, cy, 2.0f, 1.0f);

    // 左上角血条
    hud.setColor(Color(0, 0, 0, 150));
    hud.fillRoundedRect(20.0f, (float)H - 54.0f, 260.0f, 34.0f, 8.0f);
    hud.setColor(Color(60, 20, 30));
    hud.fillRoundedRect(28.0f, (float)H - 46.0f, 244.0f, 18.0f, 6.0f);
    hud.rectGradient(28.0f, (float)H - 46.0f, 244.0f * 0.85f, 18.0f, Color(240, 90, 90), Color(250, 160, 90), true);
    hud.setColor(Color(255, 255, 255));
    hud.text(28.0f, (float)H - 24.0f, "HP 85", 1.0f);

    // 右上角小地图：圆形底 + 边界 + 几个光点
    float mx = (float)W - 90.0f, my = (float)H - 90.0f;
    hud.setColor(Color(10, 14, 24, 170));
    hud.fillCircle(mx, my, 60.0f);
    hud.setColor(Color(120, 200, 255, 200));
    hud.circle(mx, my, 60.0f, 2.0f);
    hud.setColor(Color(255, 90, 90));
    hud.fillCircle(mx - 24.0f, my + 12.0f, 4.0f);
    hud.fillCircle(mx + 30.0f, my - 18.0f, 4.0f);
    hud.setColor(Color(120, 255, 160));
    hud.fillCircle(mx, my, 4.0f); // 自己

    // 四角装饰括号
    hud.setColor(Color(255, 255, 255, 120));
    float m = 18.0f, L = 34.0f;
    hud.line(m, m, m + L, m, 2.0f);
    hud.line(m, m, m, m + L, 2.0f);
    hud.line((float)W - m, m, (float)W - m - L, m, 2.0f);
    hud.line((float)W - m, m, (float)W - m, m + L, 2.0f);
    hud.line(m, (float)H - m, m + L, (float)H - m, 2.0f);
    hud.line(m, (float)H - m, m, (float)H - m - L, 2.0f);
    hud.line((float)W - m, (float)H - m, (float)W - m - L, (float)H - m, 2.0f);
    hud.line((float)W - m, (float)H - m, (float)W - m, (float)H - m - L, 2.0f);

    // 底部提示文字
    hud.setColor(Color(200, 220, 255, 230));
    hud.text(20.0f, 20.0f, "SOFTRENDERER  2D HUD OVER 3D", 2.0f);

    hud.end();
}

} // namespace

int main()
{
    std::filesystem::create_directories("output");
    const int W = 960, H = 600;

    // 场景 A：纯 2D
    {
        Framebuffer fb(W, H);
        renderScene2D(fb);
        imagewriter::writeImage("output/demo_2d_scene.png", fb);
        tga::write("output/demo_2d_scene.tga", fb);
        std::printf("已生成 output/demo_2d_scene.png / .tga（纯 2D 场景）\n");
    }

    // 场景 B：2D HUD 叠加 3D
    {
        Framebuffer fb(W, H);
        renderHudOver3D(fb);
        imagewriter::writeImage("output/demo_2d_hud.png", fb);
        tga::write("output/demo_2d_hud.tga", fb);
        std::printf("已生成 output/demo_2d_hud.png / .tga（2D HUD + 3D 场景）\n");
    }

    return 0;
}
