// ============================================================
// Demo：IBL + PBR 完整场景
//
// 用程序化 cubemap 作为环境光，渲染 3 个球体：
//   - 金属球（metallic=1, roughness=0.1）→ 应该反射天空
//   - 粗糙金属球（metallic=1, roughness=0.8）→ 模糊反射
//   - 非金属球（metallic=0, roughness=0.3）→ 漫反射+微弱镜面
//
// 展示 IBL 让金属材质"亮"起来的效果。
// ============================================================
#include <cstdio>
#include <filesystem>

#include "core/framebuffer.h"
#include "core/profiler.h"
#include "core/tga.h"
#include "math/transform.h"
#include "raster/renderer.h"
#include "scene/camera.h"
#include "scene/cubemap.h"
#include "scene/mesh.h"
#include "shader/pbr_shader.h"

int main()
{
    SR_PROFILE_SCOPE("demo_ibl_pbr");
    const int W = 900, H = 600;
    Framebuffer fb(W, H);
    Renderer renderer(W, H);

    // 生成程序化天空 cubemap
    Cubemap envMap;
    generateSkyCubemap(envMap, 64, Vec3f{0.1f, 0.3f, 0.8f}, Vec3f{0.8f, 0.6f, 0.4f});
    printf("已生成程序化天空 cubemap (64x64)\n");

    // 球体网格
    Mesh sphere = meshgen::sphere(1.0f, 48, 32);

    // 渲染
    {
        SR_PROFILE_SCOPE("render_ibl");
        renderer.clear(fb, Color(20, 20, 30));
        renderer.cullBackface = true;

        Camera cam;
        cam.position = {0, 1.5f, 5};
        cam.aspect = float(W) / H;

        PBRShader shader;
        shader.view = cam.viewMatrix();
        shader.proj = cam.projMatrix();
        shader.lightDir = Vec3f{0.3f, 0.8f, 0.5f}.normalized();
        shader.lightColor = {2.0f, 2.0f, 2.0f};
        shader.cameraPos = cam.position;
        shader.ambient = {0.01f, 0.01f, 0.02f};  // 极低环境光，让 IBL 主导
        shader.envMap = &envMap;

        // 球 1：光滑金属（金色）
        {
            shader.baseColor = {1.0f, 0.766f, 0.336f};  // 金
            shader.metallic = 1.0f;
            shader.roughness = 0.05f;
            Mat4 model = Mat4::translate(-2.5f, 0, 0);
            shader.model = model;
            shader.mvp = shader.proj * shader.view * model;
            shader.normalMatrix = model.inversed().transposed();
            renderer.drawMesh(fb, sphere, shader);
        }

        // 球 2：粗糙金属（铜色）
        {
            shader.baseColor = {0.95f, 0.64f, 0.54f};  // 铜
            shader.metallic = 1.0f;
            shader.roughness = 0.6f;
            Mat4 model = Mat4::translate(0, 0, 0);
            shader.model = model;
            shader.mvp = shader.proj * shader.view * model;
            shader.normalMatrix = model.inversed().transposed();
            renderer.drawMesh(fb, sphere, shader);
        }

        // 球 3：非金属（蓝色塑料）
        {
            shader.baseColor = {0.2f, 0.5f, 0.9f};
            shader.metallic = 0.0f;
            shader.roughness = 0.3f;
            Mat4 model = Mat4::translate(2.5f, 0, 0);
            shader.model = model;
            shader.mvp = shader.proj * shader.view * model;
            shader.normalMatrix = model.inversed().transposed();
            renderer.drawMesh(fb, sphere, shader);
        }
    }

    // 输出
    std::filesystem::create_directories("output");
    tga::write("output/demo_ibl_pbr.tga", fb);
    sr::Profiler::instance().flush("output/trace_ibl_pbr.json");
    printf("已生成 output/demo_ibl_pbr.tga\n");
    printf("  左=光滑金属(金) 中=粗糙金属(铜) 右=非金属(蓝塑料)\n");
    return 0;
}
