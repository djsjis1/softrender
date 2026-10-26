// ============================================================
// Demo：glTF PBR 材质渲染
//
// 生成一个带 PBR 材质的 glTF（3 个不同材质的三角形），
// 用 PBR shader 渲染，展示 metallic-roughness 工作流。
//
// 输出：output/demo_pbr_material.tga
// ============================================================
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "softrenderer/core/framebuffer.h"
#include "softrenderer/runtime/profiler.h"
#include "softrenderer/io/tga.h"
#include "softrenderer/math/transform.h"
#include "softrenderer/render/pipeline/renderer.h"
#include "softrenderer/scene/camera.h"
#include "softrenderer/io/gltf_loader.h"
#include "softrenderer/resource/mesh.h"
#include "softrenderer/render/shader/pbr_shader.h"

using namespace sr;

// Base64 编码
static std::string base64Encode(const uint8_t* data, size_t len)
{
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < len; i += 3)
    {
        uint32_t n = (uint32_t)data[i] << 16;
        if (i + 1 < len)
            n |= (uint32_t)data[i + 1] << 8;
        if (i + 2 < len)
            n |= (uint32_t)data[i + 2];
        out += b64[(n >> 18) & 63];
        out += b64[(n >> 12) & 63];
        out += (i + 1 < len) ? b64[(n >> 6) & 63] : '=';
        out += (i + 2 < len) ? b64[n & 63] : '=';
    }
    return out;
}

// 生成带 3 种 PBR 材质的 glTF（3 个三角形：金属、粗糙非金属、光滑非金属）
static std::string generatePbrGltf()
{
    // 3 个三角形 = 9 个顶点
    float positions[] = {
        // 三角形 0（左）：金属
        -2.0f,
        -1.0f,
        0.0f,
        -1.0f,
        1.0f,
        0.0f,
        0.0f,
        -1.0f,
        0.0f,
        // 三角形 1（中）：粗糙非金属
        -0.5f,
        -1.0f,
        0.0f,
        0.5f,
        1.0f,
        0.0f,
        1.0f,
        -1.0f,
        0.0f,
        // 三角形 2（右）：光滑非金属
        1.5f,
        -1.0f,
        0.0f,
        2.5f,
        1.0f,
        0.0f,
        3.0f,
        -1.0f,
        0.0f,
    };
    float normals[] = {
        0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1,
    };
    uint16_t indices0[] = {0, 1, 2};
    uint16_t indices1[] = {3, 4, 5};
    uint16_t indices2[] = {6, 7, 8};

    // 拼接 buffer: pos(108) + norm(108) + idx0(6) + idx1(6) + idx2(6) + pad(2) = 236
    std::vector<uint8_t> buffer(236, 0);
    std::memcpy(buffer.data(), positions, 108);
    std::memcpy(buffer.data() + 108, normals, 108);
    std::memcpy(buffer.data() + 216, indices0, 6);
    std::memcpy(buffer.data() + 222, indices1, 6);
    std::memcpy(buffer.data() + 228, indices2, 6);

    std::string b64 = base64Encode(buffer.data(), buffer.size());

    // glTF JSON with 3 materials
    std::string json = R"({
  "asset": {"version": "2.0", "generator": "SoftRenderer PBR demo"},
  "scene": 0,
  "scenes": [{"nodes": [0, 1, 2]}],
  "nodes": [{"mesh": 0}, {"mesh": 1}, {"mesh": 2}],
  "meshes": [
    {"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1}, "indices": 2, "material": 0}]},
    {"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1}, "indices": 3, "material": 1}]},
    {"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1}, "indices": 4, "material": 2}]}
  ],
  "materials": [
    {"name": "Gold_Metal", "pbrMetallicRoughness": {
      "baseColorFactor": [1.0, 0.766, 0.336, 1.0],
      "metallicFactor": 1.0, "roughnessFactor": 0.2}},
    {"name": "Rough_Plastic", "pbrMetallicRoughness": {
      "baseColorFactor": [0.8, 0.2, 0.2, 1.0],
      "metallicFactor": 0.0, "roughnessFactor": 0.9}},
    {"name": "Smooth_Plastic", "pbrMetallicRoughness": {
      "baseColorFactor": [0.2, 0.6, 0.9, 1.0],
      "metallicFactor": 0.0, "roughnessFactor": 0.1}}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 9, "type": "VEC3",
     "max": [3.0, 1.0, 0.0], "min": [-2.0, -1.0, 0.0]},
    {"bufferView": 1, "componentType": 5126, "count": 9, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5123, "count": 3, "type": "SCALAR"},
    {"bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR"},
    {"bufferView": 4, "componentType": 5123, "count": 3, "type": "SCALAR"}
  ],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 108},
    {"buffer": 0, "byteOffset": 108, "byteLength": 108},
    {"buffer": 0, "byteOffset": 216, "byteLength": 6},
    {"buffer": 0, "byteOffset": 222, "byteLength": 6},
    {"buffer": 0, "byteOffset": 228, "byteLength": 6}
  ],
  "buffers": [{"uri": "data:application/octet-stream;base64,)" +
                       b64 + R"(", "byteLength": 236}]
})";
    return json;
}

int main()
{
    // Profiler 默认关闭（零开销），示例需要 trace 才显式打开。
    sr::Profiler::instance().setEnabled(true);
    SR_PROFILE_SCOPE("demo_pbr_material");
    const int W = 900, H = 600;
    Framebuffer fb(W, H);
    Renderer renderer(W, H);

    // 生成 glTF
    {
        SR_PROFILE_SCOPE("generate_gltf");
        std::string json = generatePbrGltf();
        std::filesystem::create_directories("output");
        std::ofstream out("output/pbr_materials.gltf", std::ios::binary | std::ios::trunc);
        out.write(json.data(), std::streamsize(json.size()));
        out.flush(); // 块作用域析构也会刷，这里显式刷掉并立刻进入读回
        if (!out.good())
        {
            printf("写入 output/pbr_materials.gltf 失败\n");
            return 1;
        }
        printf("已生成 output/pbr_materials.gltf\n");
    }

    // 加载
    auto result = sr::io::loadGltfScene("output/pbr_materials.gltf");
    if (!result.success)
    {
        printf("加载失败: %s\n", result.error.c_str());
        return 1;
    }
    const sr::SceneAsset& scene = result.scene;
    printf("加载成功：%zu mesh, %zu 材质\n", scene.meshes.size(), scene.materials.size());

    // 渲染
    {
        SR_PROFILE_SCOPE("render_pbr");
        renderer.clear(fb, Color(30, 30, 40));
        renderer.cullBackface = false;  // glTF 三角形绕序可能不统一

        Camera cam;
        cam.position = {0.5f, 0, 4};
        cam.aspect = float(W) / H;

        PBRShader shader;
        shader.view = cam.viewMatrix();
        shader.proj = cam.projMatrix();
        shader.lightDir = Vec3f{0.4f, 0.7f, 0.5f}.normalized();
        shader.lightColor = {3.0f, 3.0f, 3.0f};
        shader.cameraPos = cam.position;
        shader.ambient = {0.03f, 0.03f, 0.05f};

        for (size_t i = 0; i < scene.meshes.size(); ++i)
        {
            const sr::Material* mat = scene.materialForMesh(int(i));
            if (mat)
            {
                shader.baseColor = mat->baseColorFactor;
                shader.metallic = mat->metallicFactor;
                shader.roughness = mat->roughnessFactor;
                printf("  Mesh %zu: 材质='%s' metallic=%.2f roughness=%.2f color=(%.2f,%.2f,%.2f)\n", i,
                       mat->name.c_str(), mat->metallicFactor, mat->roughnessFactor, mat->baseColorFactor.x,
                       mat->baseColorFactor.y, mat->baseColorFactor.z);
            }

            Mat4 model = Mat4::identity();
            shader.model = model;
            shader.mvp = shader.proj * shader.view * model;
            shader.normalMatrix = model.inversed().transposed();
            renderer.drawMesh(fb, scene.meshes[i], shader);
        }
    }

    // 输出
    sr::io::writeTga("output/demo_pbr_material.tga", fb);
    sr::Profiler::instance().flush("output/trace_pbr_material.json");
    printf("已生成 output/demo_pbr_material.tga\n");
    return 0;
}
