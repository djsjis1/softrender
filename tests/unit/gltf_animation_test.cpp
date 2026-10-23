// ============================================================
// 加载 → 采样 的端到端测试：加载器返回后仍可采样动画
//
// 【为什么需要】2.0.0 把动画"加载期解码"：解析器（tinygltf + accessor/bufferView）
// 只在 sr::io 内部存在，运行时 sr::animation::sampleAnimation 只吃
// AnimationClip（已解码的时间轴与关键帧值）。
//
// 本用例用真实的 glTF 文件走完整条链路：
//   1. 写一个内嵌 buffer 的最小 glTF（一个三角形 + 一条 translation 动画）；
//   2. loadGltfScene 加载，把 SceneAsset **移出**结果对象；
//   3. 结果对象析构（解析器状态全部消失）后再采样，断言数值正确。
// 若运行时仍偷偷依赖 accessor/解析器，第 3 步会直接崩或取到错误值。
// ============================================================
#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "softrenderer/io/gltf_loader.h"
#include "softrenderer/scene/animation_player.h"
#include "softrenderer/scene/scene_asset.h"

using namespace sr;

namespace
{

std::string base64Encode(const std::vector<uint8_t> &bytes)
{
    static const char *table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    for (size_t i = 0; i < bytes.size(); i += 3)
    {
        const uint32_t b0 = bytes[i];
        const uint32_t b1 = (i + 1 < bytes.size()) ? bytes[i + 1] : 0;
        const uint32_t b2 = (i + 2 < bytes.size()) ? bytes[i + 2] : 0;
        const uint32_t n = (b0 << 16) | (b1 << 8) | b2;
        out.push_back(table[(n >> 18) & 0x3F]);
        out.push_back(table[(n >> 12) & 0x3F]);
        out.push_back((i + 1 < bytes.size()) ? table[(n >> 6) & 0x3F] : '=');
        out.push_back((i + 2 < bytes.size()) ? table[n & 0x3F] : '=');
    }
    return out;
}

// 内嵌 buffer 的布局（每个 bufferView 需 4 字节对齐）：
//   [ 0, 36)  positions    3 × vec3   顶点位置
//   [36, 42)  indices      3 × uint16 三角形索引
//   [44, 52)  times        2 × float  动画时间轴（0, 2）
//   [52, 76)  translations 2 × vec3   关键帧值（(0,0,0) → (4,0,0)）
constexpr size_t kPositionOffset = 0;
constexpr size_t kIndexOffset = 36;
constexpr size_t kTimeOffset = 44;
constexpr size_t kTranslationOffset = 52;
constexpr size_t kBufferSize = 76;

std::string buildAnimatedGltfJson()
{
    std::vector<uint8_t> buffer(kBufferSize, 0);

    const float positions[9] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    std::memcpy(buffer.data() + kPositionOffset, positions, sizeof(positions));

    const uint16_t indices[3] = {0, 1, 2};
    std::memcpy(buffer.data() + kIndexOffset, indices, sizeof(indices));

    const float times[2] = {0.0f, 2.0f};
    std::memcpy(buffer.data() + kTimeOffset, times, sizeof(times));

    // 关键帧值刻意互不相等且非对称（0 → 4），避免"权重排列错误也能通过"
    const float translations[6] = {0.0f, 0.0f, 0.0f, 4.0f, 0.0f, 0.0f};
    std::memcpy(buffer.data() + kTranslationOffset, translations, sizeof(translations));

    std::string json = R"({
  "asset": {"version": "2.0"},
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [{"name": "mover", "mesh": 0}],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1}]}],
  "buffers": [{"byteLength": )";
    json += std::to_string(kBufferSize);
    json += R"(, "uri": "data:application/octet-stream;base64,)";
    json += base64Encode(buffer);
    json += R"("}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": )";
    json += std::to_string(kPositionOffset);
    json += R"(, "byteLength": 36},
    {"buffer": 0, "byteOffset": )";
    json += std::to_string(kIndexOffset);
    json += R"(, "byteLength": 6},
    {"buffer": 0, "byteOffset": )";
    json += std::to_string(kTimeOffset);
    json += R"(, "byteLength": 8},
    {"buffer": 0, "byteOffset": )";
    json += std::to_string(kTranslationOffset);
    json += R"(, "byteLength": 24}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
     "min": [0.0, 0.0, 0.0], "max": [1.0, 1.0, 0.0]},
    {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"},
    {"bufferView": 2, "componentType": 5126, "count": 2, "type": "SCALAR",
     "min": [0.0], "max": [2.0]},
    {"bufferView": 3, "componentType": 5126, "count": 2, "type": "VEC3"}
  ],
  "animations": [{
    "name": "slide",
    "samplers": [{"input": 2, "output": 3, "interpolation": "LINEAR"}],
    "channels": [{"sampler": 0, "target": {"node": 0, "path": "translation"}}]
  }]
})";
    return json;
}

} // namespace

TEST(GltfAnimation, samplesAfterLoaderReturned)
{
    namespace fs = std::filesystem;
    const fs::path file = fs::temp_directory_path() / "sr_anim_after_load.gltf";
    {
        std::FILE *fp = std::fopen(file.string().c_str(), "wb");
        ASSERT_NE(fp, nullptr) << "无法写入临时 glTF";
        const std::string json = buildAnimatedGltfJson();
        const size_t written = std::fwrite(json.data(), 1, json.size(), fp);
        std::fclose(fp);
        ASSERT_EQ(written, json.size());
    }

    SceneAsset asset;
    {
        io::SceneLoadResult result = io::loadGltfScene(file.string());
        std::error_code ec;
        fs::remove(file, ec); // 文件用完即删：后续采样不应再碰磁盘

        ASSERT_TRUE(result.success) << result.error;
        ASSERT_EQ(result.scene.animations.size(), 1u);
        ASSERT_EQ(result.scene.nodes.size(), 1u);
        ASSERT_FALSE(result.scene.meshes.empty());

        // 把自有数据移出结果对象；随后 result（含解析器产生的中间状态）析构
        asset = std::move(result.scene);
    }

    const AnimationClip &clip = asset.animations.front();
    EXPECT_EQ(clip.name, "slide");
    EXPECT_FLOAT_EQ(clip.duration(), 2.0f);
    ASSERT_EQ(clip.channels.size(), 1u);
    EXPECT_EQ(clip.channels.front().targetNode, 0);
    EXPECT_EQ(clip.channels.front().path, AnimationChannel::Path::Translation);
    ASSERT_EQ(clip.samplers.size(), 1u);
    EXPECT_EQ(clip.samplers.front().interpolation, AnimationSampler::Interpolation::Linear);

    // t = 1 位于两帧正中：线性插值必须是 (2,0,0)
    const std::vector<animation::NodePose> mid = animation::sampleAnimation(asset.nodes, clip, 1.0f);
    ASSERT_EQ(mid.size(), 1u);
    EXPECT_EQ(mid.front().nodeIndex, 0);
    EXPECT_TRUE(mid.front().hasTranslation);
    EXPECT_NEAR(mid.front().translation.x, 2.0f, 1e-5f);
    EXPECT_NEAR(mid.front().translation.y, 0.0f, 1e-5f);

    // 超出末帧钳到末帧，而不是外推
    const std::vector<animation::NodePose> tail = animation::sampleAnimation(asset.nodes, clip, 5.0f);
    ASSERT_EQ(tail.size(), 1u);
    EXPECT_NEAR(tail.front().translation.x, 4.0f, 1e-5f);

    // 首帧之前钳到首帧
    const std::vector<animation::NodePose> head = animation::sampleAnimation(asset.nodes, clip, -1.0f);
    EXPECT_NEAR(head.front().translation.x, 0.0f, 1e-5f);
}
