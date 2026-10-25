// ============================================================
// 阶段 10 实时版：Shadow Mapping + 法线贴图（仅 Windows）
//
// 【源码阅读】先读 stage10_shadow.cpp 的离屏版，再来这里看实时交互
//
// 【运行验证】弹出实时窗口：
//   WASD 移动，鼠标拖拽转视角
//   B 开关阴影，N 开关法线贴图
// ============================================================
#include <chrono>
#include <cstdio>
#include <vector>
#include "softrenderer/core/framebuffer.h"
#include "softrenderer/runtime/log.h"
#include "platform/win32_window.h"
#include "softrenderer/render/pipeline/renderer.h"
#include "softrenderer/scene/camera.h"
#include "softrenderer/resource/mesh.h"
#include "softrenderer/resource/texture.h"
#include "softrenderer/render/shader/advanced_shaders.h"
#include "softrenderer/resource/mesh_generator.h"
#include "softrenderer/resource/texture_generator.h"

using namespace sr;

struct Object
{
    Mesh mesh;
    Vec3f position{0, 0, 0};
    float spin = 0.0f;
    Vec3f baseColor{0.8f, 0.8f, 0.8f};
    const Texture *albedo = nullptr;
    const Texture *normalMap = nullptr;
    bool doubleSided = false;
};

int main()
{
    sr::logInit();
    SR_LOG_INFO("Stage10 realtime starting");
    const int W = 900, H = 640;
    const int SM = 1024;

    Framebuffer fb(W, H);
    Renderer renderer(W, H);
    Framebuffer shadowDummy(SM, SM);
    Renderer shadowRenderer(SM, SM);

    Win32Window window(W, H, "SoftRenderer Stage10 - Shadow + NormalMap");

    Camera cam;
    cam.position = {4.5f, 4.0f, 7.5f};
    cam.yaw = -3.14159265f * 0.62f;
    cam.pitch = -0.4f;
    cam.aspect = float(W) / H;

    Texture checker(sr::texgen::checkerboard(512, 12, Color(210, 210, 210), Color(90, 100, 120)));
    Texture bumps(sr::texgen::normalMapBumps(256, 6, 1.2f));

    std::vector<Object> objects;
    {
        Mesh ground = sr::meshgen::planeGrid(24.0f, 24, 6.0f);
        computeTangents(ground);
        objects.push_back({ground, {0, 0, 0}, 0.0f, {1, 1, 1}, &checker, &bumps, true});

        Mesh box = sr::meshgen::cube(1.8f);
        computeTangents(box);
        objects.push_back({box, {-2.6f, 0.9f, 0.5f}, 0.5f, {0.9f, 0.45f, 0.4f}, nullptr, nullptr, false});

        Mesh ball = sr::meshgen::sphere(1.1f, 48, 32);
        computeTangents(ball);
        objects.push_back({ball, {0.6f, 1.1f, -0.3f}, 0.0f, {0.55f, 0.75f, 0.95f}, nullptr, &bumps, false});

        Mesh box2 = sr::meshgen::cube(1.2f);
        computeTangents(box2);
        objects.push_back({box2, {2.8f, 0.6f, 1.0f}, -0.7f, {0.5f, 0.85f, 0.55f}, nullptr, nullptr, false});
    }

    Vec3f lightDir = Vec3f{0.5f, 0.5f, 0.32f}.normalized();
    Vec3f sceneCenter{0, 0.5f, 0};
    Mat4 lightProj = sr::transform::ortho(-9, 9, -9, 9, 0.1f, 30.0f);

    bool useShadow = true, useNormalMap = true;
    bool prevB = false, prevN = false;

    using clock = std::chrono::high_resolution_clock;
    auto lastTime = clock::now();
    auto fpsTime = lastTime;
    int frames = 0;
    float totalTime = 0.0f;

    while (window.pumpMessages())
    {
        auto now = clock::now();
        float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;
        totalTime += dt;

        // ---- 输入 ----
        if (Win32Window::keyDown(VK_ESCAPE))
            break;
        // 边沿触发的开关（避免长按连续翻转）
        bool bNow = Win32Window::keyDown('B');
        if (bNow && !prevB)
            useShadow = !useShadow;
        prevB = bNow;
        bool nNow = Win32Window::keyDown('N');
        if (nNow && !prevN)
            useNormalMap = !useNormalMap;
        prevN = nNow;

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
        int mdx = 0, mdy = 0;
        window.consumeMouseDelta(mdx, mdy);
        cam.addYawPitch(mdx * 0.0035f, -mdy * 0.0035f);

        // 计算本帧每个物体的 model 矩阵
        std::vector<Mat4> models(objects.size());
        for (size_t i = 0; i < objects.size(); ++i)
        {
            const auto &o = objects[i];
            models[i] = Mat4::translate(o.position.x, o.position.y, o.position.z) *
                        Mat4::rotateY(o.spin * totalTime);
        }

        // ---- Pass 1：阴影图 ----
        Vec3f lightPos = sceneCenter + lightDir * 12.0f;
        Mat4 lightView = sr::transform::lookAt(lightPos, sceneCenter, {0, 1, 0});
        Mat4 lightVP = lightProj * lightView;
        if (useShadow)
        {
            shadowRenderer.clear(shadowDummy, Color(0, 0, 0));
            DepthShader ds;
            for (size_t i = 0; i < objects.size(); ++i)
            {
                ds.model = models[i];
                ds.mvp = lightVP * models[i];
                shadowRenderer.cullBackface = false;
                shadowRenderer.drawMesh(shadowDummy, objects[i].mesh, ds);
            }
        }

        // ---- Pass 2：主渲染 ----
        renderer.clear(fb, Color(135, 160, 200));
        Mat4 view = cam.viewMatrix();
        Mat4 proj = cam.projMatrix();
        FinalShader shader;
        shader.view = view;
        shader.proj = proj;
        shader.lightDir = lightDir;
        shader.cameraPos = cam.position;
        shader.ambient = {0.22f, 0.24f, 0.30f};
        shader.shininess = 48.0f;
        if (useShadow)
        {
            shader.shadowMap = &shadowRenderer.depth();
            shader.lightVP = lightVP;
            shader.shadowW = SM;
            shader.shadowH = SM;
        }

        for (size_t i = 0; i < objects.size(); ++i)
        {
            const auto &o = objects[i];
            shader.model = models[i];
            shader.mvp = proj * view * models[i];
            shader.normalMatrix = models[i].inversed().transposed();
            shader.baseColor = o.baseColor;
            shader.texture = o.albedo;
            shader.normalMap = useNormalMap ? o.normalMap : nullptr;
            renderer.cullBackface = !o.doubleSided;
            renderer.drawMesh(fb, o.mesh, shader);
        }

        window.present(fb);

        // ---- FPS ----
        frames++;
        if (std::chrono::duration<float>(now - fpsTime).count() >= 0.5f)
        {
            float fps = frames / std::chrono::duration<float>(now - fpsTime).count();
            char title[160];
            std::snprintf(title, sizeof(title),
                          "SoftRenderer Stage10 | %.1f FPS | Shadow:%s NormalMap:%s | B/N toggle, WASD+Mouse",
                          fps, useShadow ? "ON" : "OFF", useNormalMap ? "ON" : "OFF");
            window.setTitle(title);
            frames = 0;
            fpsTime = now;
        }
    }
    sr::logShutdown();
    return 0;
}
