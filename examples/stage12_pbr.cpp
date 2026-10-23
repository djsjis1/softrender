// ============================================================
// 阶段 12：PBR 光照（Cook-Torrance BRDF）
//
// 【源码阅读】先读这些文件：
//   shader/pbr_shader.h  — PBR 着色器：微表面模型 + 金属度/粗糙度
//   scene/mesh.h          — 球体网格生成
//   scene/camera.h        — Camera
//
// 【运行验证】生成 output/stage12_pbr.tga：
//   5×5 球体阵列：行=粗糙度递增，列=金属度递增
// ============================================================
#include <cmath>
#include <cstdio>
#include <filesystem>
#include "softrenderer/core/framebuffer.h"
#include "softrenderer/io/tga.h"
#include "softrenderer/render/pipeline/renderer.h"
#include "softrenderer/scene/camera.h"
#include "softrenderer/resource/mesh.h"
#include "softrenderer/render/shader/pbr_shader.h"
#include "softrenderer/resource/mesh_generator.h"

using namespace sr;

int main() {
    const int W = 900, H = 700;
    Framebuffer fb(W, H);
    Renderer renderer(W, H);
    renderer.clear(fb, Color(20, 20, 25));

    Camera cam;
    cam.position = {0, 0, 14};
    cam.aspect = float(W) / H;
    cam.fovY = 3.14159265f / 3.0f;

    Mesh sphere = sr::meshgen::sphere(0.45f, 48, 32);

    PBRShader pbr;
    pbr.view = cam.viewMatrix();
    pbr.proj = cam.projMatrix();
    pbr.lightDir = Vec3f{0.5f, 0.8f, 0.6f}.normalized();
    pbr.lightColor = {3.0f, 3.0f, 3.0f}; // HDR 光强
    pbr.ambient = {0.03f, 0.03f, 0.04f};
    pbr.cameraPos = cam.position;

    const int rows = 5, cols = 5;
    for (int r = 0; r < rows; ++r) {
        float roughness = 0.05f + 0.95f * r / (rows - 1);
        for (int c = 0; c < cols; ++c) {
            float metallic = float(c) / (cols - 1);
            // 颜色：从红(非金属)到金(金属)
            Vec3f albedo = Vec3f{0.9f, 0.3f + 0.5f * metallic, 0.2f * (1.0f - metallic)};

            float x = (c - (cols - 1) * 0.5f) * 2.0f;
            float y = ((rows - 1) * 0.5f - r) * 2.0f;
            Mat4 model = Mat4::translate(x, y, 0);

            pbr.model = model;
            pbr.mvp = pbr.proj * pbr.view * model;
            pbr.normalMatrix = model.inversed().transposed();
            pbr.baseColor = albedo;
            pbr.metallic = metallic;
            pbr.roughness = roughness;
            pbr.texture = nullptr;
            renderer.cullBackface = true;
            renderer.drawMesh(fb, sphere, pbr);
        }
    }

    std::filesystem::create_directories("output");
    sr::io::writeTga("output/stage12_pbr.tga", fb);
    std::printf("已生成 output/stage12_pbr.tga（行=粗糙度, 列=金属度）\n");
    return 0;
}
