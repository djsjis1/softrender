// ============================================================
// 阶段 08：着色器管线（Flat / Gouraud / Phong 对比）
//
// 【源码阅读】先读这些文件：
//   shader/shaders.h        — FlatShader / GouraudShader / PhongShader
//   raster/renderer.h       — Renderer::drawMesh + Shader 回调
//   scene/mesh.h            — Mesh：顶点/法线/UV
//   scene/camera.h          — Camera
//
// 【运行验证】生成三张图对比：
//   output/stage08_shader_flat.tga / _gouraud.tga / _phong.tga
// ============================================================
#include <cstdio>
#include <filesystem>
#include <vector>
#include "softrenderer/core/framebuffer.h"
#include "softrenderer/io/tga.h"
#include "softrenderer/render/pipeline/renderer.h"
#include "softrenderer/scene/camera.h"
#include "softrenderer/resource/mesh.h"
#include "softrenderer/resource/texture.h"
#include "softrenderer/render/shader/shaders.h"
#include "softrenderer/render/pipeline/surface_shader.h"
#include "softrenderer/resource/mesh_generator.h"
#include "softrenderer/resource/texture_generator.h"

using namespace sr;

struct Object
{
    Mesh mesh;
    Vec3f position{0, 0, 0};
    Vec3f baseColor{0.8f, 0.8f, 0.8f};
    const Texture *texture = nullptr;
    float angle = 0.0f;
};

int main()
{
    const int W = 800, H = 600;
    Framebuffer fb(W, H);
    Renderer renderer(W, H);

    Camera cam;
    cam.position = {0, 2.4f, 6.5f};
    cam.pitch = -0.28f;
    cam.aspect = float(W) / H;

    Texture checker(sr::texgen::checkerboard(256, 8, Color(220, 220, 220), Color(60, 60, 70)));
    Texture uvgrid(sr::texgen::uvGrid(256, 8));

    std::vector<Object> objects;
    {
        Object ground;
        ground.mesh = sr::meshgen::planeGrid(40.0f, 40, 20.0f);
        ground.texture = &checker;
        objects.push_back(ground);

        Object box;
        box.mesh = sr::meshgen::cube(1.6f);
        box.position = {-2.2f, 0.8f, 0};
        box.baseColor = {0.9f, 0.4f, 0.35f};
        box.angle = 0.5f;
        objects.push_back(box);

        Object ball;
        ball.mesh = sr::meshgen::sphere(1.0f, 40, 28);
        ball.position = {0.6f, 1.0f, 0};
        ball.texture = &uvgrid;
        objects.push_back(ball);

        Object box2;
        box2.mesh = sr::meshgen::cube(1.2f);
        box2.position = {2.6f, 0.6f, -0.5f};
        box2.baseColor = {0.4f, 0.6f, 0.95f};
        box2.angle = -0.6f;
        objects.push_back(box2);
    }

    FlatShader flat;
    GouraudShader gouraud;
    PhongShader phong;
    struct Pass
    {
        SurfaceShader *sh;
        const char *name;
    };
    Pass passes[3] = {{&flat, "flat"}, {&gouraud, "gouraud"}, {&phong, "phong"}};

    Vec3f lightDir = Vec3f{0.4f, 0.9f, 0.5f}.normalized();
    Mat4 view = cam.viewMatrix();
    Mat4 proj = cam.projMatrix();

    std::filesystem::create_directories("output");

    for (const auto &pass : passes)
    {
        renderer.clear(fb, Color(30, 34, 44));
        SurfaceShader *sh = pass.sh;
        sh->view = view;
        sh->proj = proj;
        sh->lightDir = lightDir;
        sh->cameraPos = cam.position;

        for (const auto &obj : objects)
        {
            Mat4 model = Mat4::translate(obj.position.x, obj.position.y, obj.position.z) *
                         Mat4::rotateY(obj.angle);
            sh->model = model;
            sh->mvp = proj * view * model;
            sh->normalMatrix = model.inversed().transposed();
            sh->baseColor = obj.baseColor;
            sh->texture = obj.texture;
            renderer.cullBackface = (obj.texture != &checker);
            renderer.drawMesh(fb, obj.mesh, *sh);
        }

        char path[128];
        std::snprintf(path, sizeof(path), "output/stage08_shader_%s.tga", pass.name);
        sr::io::writeTga(path, fb);
        std::printf("已生成 %s\n", path);
    }
    return 0;
}
