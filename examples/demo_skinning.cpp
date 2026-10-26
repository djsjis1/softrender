// ============================================================
// Demo：手工单关节变换（不是完整的 glTF 蒙皮管线）
//
// 程序化生成一个带 2 根骨骼 + 旋转动画的 glTF，逐帧采样动画 →
// 计算蒙皮矩阵 → 手动用一个关节矩阵变换整块网格 → 渲染。
//
// 本示例**不**读取顶点的 JOINTS_0 / WEIGHTS_0，也不按 skin.joints
// 建立索引映射与父子全局变换：Vertex 目前不携带骨骼权重，真实逐顶点
// 蒙皮尚未接入管线。它验证的是“动画采样 + 矩阵链 + 渲染”这条链路。
//
// 输出：output/skin_frame_XX.tga（首帧 + 尾帧 + 中间帧）
//       output/trace_skin.json（Perfetto 火焰图）
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
#include "softrenderer/scene/animation_player.h"
#include "softrenderer/scene/camera.h"
#include "softrenderer/io/gltf_loader.h"
#include "softrenderer/render/shader/shaders.h"

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

// 生成一个带骨骼蒙皮 + 动画的 glTF：
// - 2 根骨骼（root + child），child 绕 Y 轴旋转
// - 一个三角形，顶点绑定到 child 骨骼
// - 一段动画：child 从 0° 旋转到 90°（绕 Y），再回到 0°
static std::string generateSkinGltf()
{
    // 三角形顶点（3 × float3 = 36 bytes）
    float positions[] = {0, 1.5f, 0, -1, 0, 0, 1, 0, 0};
    float normals[] = {0, 0, 1, 0, 0, 1, 0, 0, 1};
    // 关节权重（3 × vec4 = 48 bytes）：全绑定到 joint 1（child）
    float joints[] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    float weights[] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
    // 逆绑定矩阵（16 floats × 2 joints = 128 bytes）：identity
    float ibm[32] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    // 动画时间戳（4 floats）：0, 1.0, 2.0, 3.0
    float times[] = {0, 1.0f, 2.0f, 3.0f};
    // 动画旋转值（4 × vec4 = 64 bytes）：绕 Y 转 0°, 90°, 0°, 0°
    float rots[] = {0, 0, 0, 1, 0, 0.707f, 0, 0.707f, 0, 0, 0, 1, 0, 0, 0, 1};
    uint16_t indices[] = {0, 1, 2};

    size_t bufSize = sizeof(positions) + sizeof(normals) + sizeof(joints) + sizeof(weights) + sizeof(ibm) +
                     sizeof(times) + sizeof(rots) + sizeof(indices);
    std::vector<uint8_t> buffer(bufSize + 2, 0);
    size_t off = 0;
    std::memcpy(buffer.data() + off, positions, sizeof(positions));
    off += sizeof(positions);
    std::memcpy(buffer.data() + off, normals, sizeof(normals));
    off += sizeof(normals);
    std::memcpy(buffer.data() + off, joints, sizeof(joints));
    off += sizeof(joints);
    std::memcpy(buffer.data() + off, weights, sizeof(weights));
    off += sizeof(weights);
    std::memcpy(buffer.data() + off, ibm, sizeof(ibm));
    off += sizeof(ibm);
    std::memcpy(buffer.data() + off, times, sizeof(times));
    off += sizeof(times);
    std::memcpy(buffer.data() + off, rots, sizeof(rots));
    off += sizeof(rots);
    std::memcpy(buffer.data() + off, indices, sizeof(indices));

    std::string b64 = base64Encode(buffer.data(), bufSize);

    return R"({
  "asset": {"version": "2.0", "generator": "SoftRenderer Skinning Demo"},
  "scene": 0,
  "scenes": [{"nodes": [0, 1]}],
  "nodes": [
    {"name": "root"},
    {"name": "child", "translation": [0, -0.5, 0], "skin": 0}
  ],
  "meshes": [{"primitives": [{
    "attributes": {"POSITION": 0, "NORMAL": 1, "JOINTS_0": 2, "WEIGHTS_0": 3},
    "indices": 7
  }]}],
  "skins": [{"joints": [0, 1], "inverseBindMatrices": 4}],
  "animations": [{
    "channels": [{"sampler": 0, "target": {"node": 1, "path": "rotation"}}],
    "samplers": [{"input": 5, "output": 6, "interpolation": "LINEAR"}]
  }],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
     "max": [1.0, 1.5, 0.0], "min": [-1.0, 0.0, 0.0]},
    {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC4"},
    {"bufferView": 3, "componentType": 5126, "count": 3, "type": "VEC4"},
    {"bufferView": 4, "componentType": 5126, "count": 2, "type": "MAT4"},
    {"bufferView": 5, "componentType": 5126, "count": 4, "type": "SCALAR", "min": [0], "max": [3]},
    {"bufferView": 6, "componentType": 5126, "count": 4, "type": "VEC4"},
    {"bufferView": 7, "componentType": 5123, "count": 3, "type": "SCALAR"}
  ],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 36},
    {"buffer": 0, "byteOffset": 72, "byteLength": 48},
    {"buffer": 0, "byteOffset": 120, "byteLength": 48},
    {"buffer": 0, "byteOffset": 168, "byteLength": 128},
    {"buffer": 0, "byteOffset": 296, "byteLength": 16},
    {"buffer": 0, "byteOffset": 312, "byteLength": 64},
    {"buffer": 0, "byteOffset": 376, "byteLength": 6}
  ],
  "buffers": [{"uri": "data:application/octet-stream;base64,)" +
           b64 + R"(", "byteLength": )" + std::to_string(bufSize) + R"(}]
})";
}

// 用蒙皮矩阵变换所有顶点（简化：全绑定到 joint 1）
// 完整逻辑见 skeleton.h：Σ weight_j × skinMatrix[bone_j] × position
static Mesh applySkinning(const Mesh& src, const std::vector<Mat4>& skinMatrices)
{
    Mesh out = src;
    for (auto& v : out.vertices)
    {
        v.position = skinMatrices[1].transformPoint(v.position);
        v.normal = skinMatrices[1].transformDir(v.normal).normalized();
    }
    return out;
}

int main()
{
    // Profiler 默认关闭（零开销），示例需要 trace 才显式打开。
    sr::Profiler::instance().setEnabled(true);
    SR_PROFILE_SCOPE("demo_skinning");
    const int W = 800, H = 600;

    // ---- 1. 生成并加载 glTF ----
    std::filesystem::create_directories("output");
    std::string json = generateSkinGltf();
    std::ofstream out("output/skin_test.gltf", std::ios::binary | std::ios::trunc);
    out.write(json.data(), std::streamsize(json.size()));
    // ofstream 与 fopen+fclose 不同：write 只落流缓冲。这里接着就要读回同一文件，
    // 必须显式 flush 落盘，否则加载器读到空文件（demo_gltf 靠块作用域析构侥幸避开）。
    out.flush();
    if (!out.good())
    {
        printf("写入 output/skin_test.gltf 失败\n");
        return 1;
    }
    printf("已生成 output/skin_test.gltf (%zu bytes)\n", json.size());

    // ---- 2. 用自有加载器一次拿全：网格 / 节点 / 蒙皮 / 动画片段 ----
    auto loaded = sr::io::loadGltfScene("output/skin_test.gltf");
    if (!loaded.success || loaded.scene.meshes.empty() || loaded.scene.animations.empty())
    {
        printf("加载失败: %s\n", loaded.error.c_str());
        return 1;
    }
    const sr::SceneAsset& scene = loaded.scene;
    printf("加载成功：%zu mesh, %zu animation, %zu node, %zu skin\n", scene.meshes.size(),
           scene.animations.size(), scene.nodes.size(), scene.skins.size());
    Mesh baseMesh = scene.meshes[0];

    // 逆绑定矩阵已由加载阶段解码，这里直接用
    const std::vector<Mat4> inverseBindMatrices =
        scene.skins.empty() ? std::vector<Mat4>{} : scene.skins[0].inverseBindMatrices;

    // ---- 3. 逐帧渲染 ----
    Framebuffer fb(W, H);
    Renderer renderer(W, H);
    const sr::AnimationClip& clip = scene.animations[0];
    float duration = clip.duration(); // 动画总时长
    int totalFrames = 10;

    for (int frameIdx = 0; frameIdx < totalFrames; ++frameIdx)
    {
        float t = (duration * frameIdx) / (totalFrames - 1);

        // 采样动画（自有数据，不再需要解析器对象）
        auto poses = sr::animation::sampleAnimation(scene.nodes, clip, t);

        // 构建蒙皮矩阵：jointTransform * inverseBind
        std::vector<Mat4> skinMats(poses.size(), Mat4::identity());
        for (size_t i = 0; i < poses.size(); ++i)
        {
            Mat4 local = sr::animation::poseToMatrix(poses[i]);
            if (i < inverseBindMatrices.size())
                skinMats[i] = local * inverseBindMatrices[i];
            else
                skinMats[i] = local;
        }

        // 应用蒙皮
        Mesh skinned = applySkinning(baseMesh, skinMats);

        // 渲染
        char frameName[64];
        std::snprintf(frameName, sizeof(frameName), "frame %d t=%.1f", frameIdx, t);

        sr::ProfileScope ps(frameName);
        renderer.clear(fb, Color(20, 20, 30));

        Camera cam;
        cam.position = {0, 0.5f, 3};
        cam.aspect = float(W) / H;

        PhongShader shader;
        shader.view = cam.viewMatrix();
        shader.proj = cam.projMatrix();
        shader.lightDir = Vec3f{0.5f, 0.8f, 0.3f}.normalized();
        shader.cameraPos = cam.position;
        shader.ambient = {0.15f, 0.15f, 0.2f};
        shader.baseColor = {0.9f, 0.6f, 0.3f};

        Mat4 modelMat = Mat4::identity();
        shader.model = modelMat;
        shader.mvp = shader.proj * shader.view * modelMat;
        shader.normalMatrix = modelMat.inversed().transposed();

        renderer.drawMesh(fb, skinned, shader);

        char path[64];
        std::snprintf(path, sizeof(path), "output/skin_frame_%02d.tga", frameIdx);
        sr::io::writeTga(path, fb);
    }

    printf("已生成 output/skin_frame_00~%02d.tga（共 %d 帧）\n", totalFrames - 1, totalFrames);

    // ---- 5. Profiler 输出 ----
    sr::Profiler::instance().flush("output/trace_skin.json");
    printf("已生成 output/trace_skin.json\n");
    return 0;
}
