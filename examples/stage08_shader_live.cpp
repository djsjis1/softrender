// ============================================================
// 阶段 08 实时版：可编程着色管线 + Win32 窗口（仅 Windows）
//
// 【源码阅读】先读 stage08_shader.cpp 的离屏版，再来这里看实时交互
//
// 【运行验证】弹出实时窗口：
//   WASD 移动，Q/E 上下，鼠标拖拽转视角
//   1/2/3 切换 Flat / Gouraud / Phong
// ============================================================
#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>
#include "softrenderer/core/framebuffer.h"
#include "platform/win32_window.h"
#include "softrenderer/render/pipeline/renderer.h"
#include "softrenderer/scene/camera.h"
#include "softrenderer/resource/mesh.h"
#include "softrenderer/resource/texture.h"
#include "softrenderer/render/shader/shaders.h"
#include "softrenderer/resource/mesh_generator.h"
#include "softrenderer/resource/texture_generator.h"

using namespace sr;

struct Object
{
    Mesh mesh;
    Vec3f position{0, 0, 0};
    Vec3f baseColor{0.8f, 0.8f, 0.8f};
    const Texture *texture = nullptr;
    float spin = 0.0f; // 自转速度（弧度/秒）
};

int main()
{
    const int W = 800, H = 600;
    Framebuffer fb(W, H);
    Renderer renderer(W, H);

    Win32Window window(W, H, "SoftRenderer Stage08");

    Camera cam;
    cam.position = {0, 2.0f, 6.0f};
    cam.aspect = float(W) / H;

    // 纹理
    Texture checker(sr::texgen::checkerboard(256, 8, Color(220, 220, 220), Color(60, 60, 70)));
    Texture uvgrid(sr::texgen::uvGrid(256, 8));

    // 场景物体
    std::vector<Object> objects;
    {
        Object ground;
        ground.mesh = sr::meshgen::planeGrid(40.0f, 40, 20.0f);
        ground.position = {0, 0, 0};
        ground.texture = &checker;
        objects.push_back(ground);

        Object box;
        box.mesh = sr::meshgen::cube(1.6f);
        box.position = {-2.2f, 0.9f, 0};
        box.baseColor = {0.9f, 0.4f, 0.35f};
        box.spin = 0.6f;
        objects.push_back(box);

        Object ball;
        ball.mesh = sr::meshgen::sphere(1.0f, 40, 28);
        ball.position = {0.6f, 1.0f, 0};
        ball.texture = &uvgrid;
        objects.push_back(ball);

        Object box2;
        box2.mesh = sr::meshgen::cube(1.2f);
        box2.position = {2.6f, 0.7f, -0.5f};
        box2.baseColor = {0.4f, 0.6f, 0.95f};
        box2.spin = -0.9f;
        objects.push_back(box2);
    }

    // 三种着色器实例，运行时切换
    FlatShader flat;
    GouraudShader gouraud;
    PhongShader phong;
    SurfaceShader *shaders[3] = {&flat, &gouraud, &phong};
    const char *shaderNames[3] = {"Flat", "Gouraud", "Phong"};
    int current = 2; // 默认 Phong

    Vec3f lightDir = Vec3f{0.4f, 0.9f, 0.5f}.normalized();

    using clock = std::chrono::high_resolution_clock;
    auto lastTime = clock::now();
    auto fpsTime = lastTime;
    int frames = 0;
    float totalTime = 0.0f;

    while (window.pumpMessages())
    {
        // ---- 时间步长 ----
        auto now = clock::now();
        float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;
        totalTime += dt;

        // ---- 输入：着色器切换 ----
        if (Win32Window::keyDown('1'))
            current = 0;
        if (Win32Window::keyDown('2'))
            current = 1;
        if (Win32Window::keyDown('3'))
            current = 2;
        if (Win32Window::keyDown(VK_ESCAPE))
            break;

        // ---- 输入：相机移动 ----
        float speed = 5.0f * dt;
        if (Win32Window::keyDown(VK_SHIFT))
            speed *= 3.0f;
        if (Win32Window::keyDown('W'))
            cam.moveForward(speed);
        if (Win32Window::keyDown('S'))
            cam.moveForward(-speed);
        if (Win32Window::keyDown('A'))
            cam.moveRight(-speed);
        if (Win32Window::keyDown('D'))
            cam.moveRight(speed);
        if (Win32Window::keyDown('E'))
            cam.moveUp(speed);
        if (Win32Window::keyDown('Q'))
            cam.moveUp(-speed);

        // ---- 输入：鼠标转向 ----
        int mdx = 0, mdy = 0;
        window.consumeMouseDelta(mdx, mdy);
        const float sens = 0.0035f;
        cam.addYawPitch(mdx * sens, -mdy * sens);

        // ---- 渲染 ----
        renderer.clear(fb, Color(30, 34, 44));
        Mat4 view = cam.viewMatrix();
        Mat4 proj = cam.projMatrix();
        SurfaceShader *sh = shaders[current];
        sh->view = view;
        sh->proj = proj;
        sh->lightDir = lightDir;
        sh->cameraPos = cam.position;

        for (const auto &obj : objects)
        {
            float angle = obj.spin * totalTime;
            Mat4 model = Mat4::translate(obj.position.x, obj.position.y, obj.position.z) *
                         Mat4::rotateY(angle);
            sh->model = model;
            sh->mvp = proj * view * model;
            sh->normalMatrix = model.inversed().transposed();
            sh->baseColor = obj.baseColor;
            sh->texture = obj.texture;
            // 地面是单面平面，关掉背面剔除以免朝向问题被剔除
            renderer.cullBackface = (obj.texture != &checker);
            renderer.drawMesh(fb, obj.mesh, *sh);
        }

        window.present(fb);

        // ---- FPS 统计 ----
        frames++;
        if (std::chrono::duration<float>(now - fpsTime).count() >= 0.5f)
        {
            float fps = frames / std::chrono::duration<float>(now - fpsTime).count();
            char title[128];
            std::snprintf(title, sizeof(title),
                          "SoftRenderer Stage08 | %s | %.1f FPS | WASD+Mouse, 1/2/3 shader",
                          shaderNames[current], fps);
            window.setTitle(title);
            frames = 0;
            fpsTime = now;
        }
    }
    return 0;
}
