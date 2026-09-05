// ============================================================
// 阶段 8 离屏验证：Shadow Mapping + 法线贴图 的两趟渲染
//
// Pass 1（阴影图）：从光源视角用正交投影渲染场景深度到一张
//   1024x1024 的深度图。
// Pass 2（主渲染）：从相机视角渲染，每个片元换算到光源空间比较
//   深度，判断是否在阴影中；同时用法线贴图增加表面凹凸细节。
//
// 生成：
//   output/stage8_final.tga    完整效果（阴影 + 法线贴图）
//   output/stage8_noshadow.tga  关闭阴影对比
// ============================================================
#include <cstdio>
#include <filesystem>
#include <vector>
#include "core/framebuffer.h"
#include "core/tga.h"
#include "raster/renderer.h"
#include "scene/camera.h"
#include "scene/mesh.h"
#include "scene/texture.h"
#include "shader/advanced_shaders.h"

struct Object
{
    Mesh mesh;
    Mat4 model;
    Vec3f baseColor{0.8f, 0.8f, 0.8f};
    const Texture *albedo = nullptr;
    const Texture *normalMap = nullptr;
    bool doubleSided = false;
};

int main()
{
    const int W = 900, H = 640;
    const int SM = 1024; // 阴影贴图分辨率

    // 纹理
    Texture checker(texgen::checkerboard(512, 12, Color(210, 210, 210), Color(90, 100, 120)));
    Texture bumps(texgen::normalMapBumps(256, 6, 1.2f));

    // 场景
    std::vector<Object> objects;
    {
        Mesh ground = meshgen::planeGrid(24.0f, 24, 6.0f);
        computeTangents(ground);
        objects.push_back({ground, Mat4::identity(), {1, 1, 1}, &checker, &bumps, true});

        Mesh box = meshgen::cube(1.8f);
        computeTangents(box);
        objects.push_back({box, Mat4::translate(-2.6f, 0.9f, 0.5f) * Mat4::rotateY(0.5f), {0.9f, 0.45f, 0.4f}, nullptr, nullptr, false});

        Mesh ball = meshgen::sphere(1.1f, 48, 32);
        computeTangents(ball);
        objects.push_back({ball, Mat4::translate(0.6f, 1.1f, -0.3f), {0.55f, 0.75f, 0.95f}, nullptr, &bumps, false});

        Mesh box2 = meshgen::cube(1.2f);
        computeTangents(box2);
        objects.push_back({box2, Mat4::translate(2.8f, 0.6f, 1.0f) * Mat4::rotateY(-0.6f), {0.5f, 0.85f, 0.55f}, nullptr, nullptr, false});
    }

    // ---------- 光源（平行光）与其观察/投影矩阵 ----------
    // 太阳压得较低，让物体在地面投出明显拉长的影子
    Vec3f lightDir = Vec3f{0.5f, 0.5f, 0.32f}.normalized();
    Vec3f sceneCenter{0, 0.5f, 0};
    Vec3f lightPos = sceneCenter + lightDir * 12.0f;
    Mat4 lightView = transform::lookAt(lightPos, sceneCenter, {0, 1, 0});
    Mat4 lightProj = transform::ortho(-9, 9, -9, 9, 0.1f, 30.0f);
    Mat4 lightVP = lightProj * lightView;

    // ---------- Pass 1：渲染阴影贴图 ----------
    Framebuffer shadowDummy(SM, SM);
    Renderer shadowRenderer(SM, SM);
    shadowRenderer.clear(shadowDummy, Color(0, 0, 0));
    {
        DepthShader ds;
        for (const auto &obj : objects)
        {
            ds.model = obj.model;
            ds.mvp = lightVP * obj.model;
            shadowRenderer.cullBackface = false; // 阴影图渲双面，减少漏光
            shadowRenderer.drawMesh(shadowDummy, obj.mesh, ds);
        }
    }

    // ---------- Pass 2：主渲染 ----------
    Camera cam;
    cam.position = {4.5f, 4.2f, 7.5f};
    cam.aspect = float(W) / H;
    cam.yaw = -3.14159265f * 0.62f;
    cam.pitch = -0.42f;
    Mat4 view = cam.viewMatrix();
    Mat4 proj = cam.projMatrix();

    auto renderScene = [&](Framebuffer &fb, bool useShadow)
    {
        Renderer renderer(W, H);
        renderer.clear(fb, Color(135, 160, 200)); // 天空蓝
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

        for (const auto &obj : objects)
        {
            shader.model = obj.model;
            shader.mvp = proj * view * obj.model;
            shader.normalMatrix = obj.model.inversed().transposed();
            shader.baseColor = obj.baseColor;
            shader.texture = obj.albedo;
            shader.normalMap = obj.normalMap;
            renderer.cullBackface = !obj.doubleSided;
            renderer.drawMesh(fb, obj.mesh, shader);
        }
    };

    std::filesystem::create_directories("output");

    Framebuffer fbFinal(W, H), fbNoShadow(W, H);
    renderScene(fbFinal, true);
    renderScene(fbNoShadow, false);
    tga::write("output/stage8_final.tga", fbFinal);
    tga::write("output/stage8_noshadow.tga", fbNoShadow);
    std::printf("已生成 output/stage8_final.tga 与 output/stage8_noshadow.tga\n");
    return 0;
}
