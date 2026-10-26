// ============================================================
// Demo：内嵌 glTF 加载 + Profiler 输出
//
// 1. 程序化生成一个极简 glTF JSON（内嵌 base64 buffer）
// 2. 用 gltf_loader 加载
// 3. 渲染并输出 TGA
// 4. 输出 Chrome Trace JSON（用 Perfetto UI 查看）
//
// 验证：glTF 加载管线 + Profiler 管线
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
#include "softrenderer/render/shader/shaders.h"

using namespace sr;

// 生成一个内嵌 buffer 的极简 glTF JSON（一个三角形）
// buffer 数据：3 个顶点位置 (3*3*4=36 bytes) + 3 个索引 (3*2=6 bytes, padded to 8)
static std::string generateMinimalGltf()
{
    // 顶点位置：3 个 float3
    float positions[] = {
        0.0f,  1.0f,  0.0f,  // 顶
        -1.0f, -1.0f, 0.0f,  // 左下
        1.0f,  -1.0f, 0.0f   // 右下
    };
    uint16_t indices[] = {0, 1, 2};

    // 拼接 buffer：positions(36 bytes) + indices(6 bytes + 2 pad)
    std::vector<uint8_t> buffer(44, 0);
    std::memcpy(buffer.data(), positions, 36);
    std::memcpy(buffer.data() + 36, indices, 6);

    // Base64 编码
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    for (size_t i = 0; i < buffer.size(); i += 3)
    {
        uint32_t n = (uint32_t)buffer[i] << 16;
        if (i + 1 < buffer.size())
            n |= (uint32_t)buffer[i + 1] << 8;
        if (i + 2 < buffer.size())
            n |= (uint32_t)buffer[i + 2];
        encoded += b64[(n >> 18) & 63];
        encoded += b64[(n >> 12) & 63];
        encoded += (i + 1 < buffer.size()) ? b64[(n >> 6) & 63] : '=';
        encoded += (i + 2 < buffer.size()) ? b64[n & 63] : '=';
    }

    // 构造 glTF JSON
    std::string json = R"({
  "asset": {"version": "2.0", "generator": "SoftRenderer demo"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"mesh": 0}],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1}]}],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
     "max": [1.0, 1.0, 0.0], "min": [-1.0, -1.0, 0.0]},
    {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"}
  ],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36, "target": 34962},
    {"buffer": 0, "byteOffset": 36, "byteLength": 6, "target": 34963}
  ],
  "buffers": [{"uri": "data:application/octet-stream;base64,)" +
                       encoded + R"(", "byteLength": 44}]
})";
    return json;
}

int main()
{
    // Profiler 默认关闭（零开销），示例需要 trace 才显式打开。
    sr::Profiler::instance().setEnabled(true);
    SR_PROFILE_SCOPE("demo_gltf_main");

    const int W = 800, H = 600;
    Framebuffer fb(W, H);
    Renderer renderer(W, H);

    // ---- 1. 生成并保存 glTF 文件 ----
    {
        SR_PROFILE_SCOPE("generate_gltf");
        std::string json = generateMinimalGltf();
        std::filesystem::create_directories("output");
        std::ofstream out("output/test_triangle.gltf", std::ios::binary | std::ios::trunc);
        out.write(json.data(), std::streamsize(json.size()));
        printf("已生成 output/test_triangle.gltf (%zu bytes)\n", json.size());
    }

    // ---- 2. 用 gltf_loader 加载 ----
    Mesh mesh;
    {
        SR_PROFILE_SCOPE("load_gltf");
        if (!sr::io::loadGltfMesh("output/test_triangle.gltf", mesh))
        {
            // 不回落成程序化立方体：那会让“glTF 管线坏了”表现为一张正常的方块图，
            // CI 跑完仍是绿灯，等于把主功能失效伪装成成功。
            std::printf("glTF 加载失败\n");
            return 1;
        }
        printf("glTF 加载成功：%zu 顶点, %d 三角形\n", mesh.vertices.size(), mesh.triangleCount());
    }

    // ---- 3. 渲染 ----
    {
        SR_PROFILE_SCOPE("render");
        renderer.clear(fb, Color(20, 20, 30));

        Camera cam;
        cam.position = {0, 0, 3};
        cam.aspect = float(W) / H;

        PhongShader shader;
        shader.view = cam.viewMatrix();
        shader.proj = cam.projMatrix();
        shader.lightDir = Vec3f{0.5f, 0.8f, 0.3f}.normalized();
        shader.cameraPos = cam.position;
        shader.ambient = {0.15f, 0.15f, 0.2f};

        Mat4 model = Mat4::identity();
        shader.model = model;
        shader.mvp = shader.proj * shader.view * model;
        shader.normalMatrix = model.inversed().transposed();
        shader.baseColor = {0.3f, 0.7f, 1.0f};

        renderer.drawMesh(fb, mesh, shader);
    }

    // ---- 4. 输出 ----
    {
        SR_PROFILE_SCOPE("write_output");
        if (!sr::io::writeTga("output/demo_gltf.tga", fb))
        {
            std::printf("写文件失败: output/demo_gltf.tga\n");
            return 1;
        }
        printf("已生成 output/demo_gltf.tga\n");
    }

    // ---- 5. 输出 Chrome Trace ----
    sr::Profiler::instance().flush("output/trace_demo_gltf.json");
    printf("已生成 output/trace_demo_gltf.json（用 Perfetto UI 打开）\n");

    return 0;
}
