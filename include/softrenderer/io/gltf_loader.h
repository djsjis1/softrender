#pragma once
#include <string>

#include "softrenderer/resource/mesh.h"
#include "softrenderer/scene/scene_asset.h"

// ============================================================
// glTF / GLB 场景加载器
//
// 【输出】sr::SceneAsset：网格、材质、纹理、节点、蒙皮、动画片段。
// 全部是自有类型，公共接口不出现 tinygltf、nlohmann::json 等解析器符号。
//
// 【失败语义】返回 success=false 与给用户看的错误文本，scene 保持为空，
// 不发布半成品（见 ADR-002）。是否打印日志由调用方决定。
//
// 【实现】src/io/gltf_loader.cpp（解析 + 转换）与
// src/io/gltf_loader_impl.cpp（tinygltf 实现宏），文字加载器编入
// SoftRenderer::io 静态库。核心渲染层不需要链接它。
//
// 用法：
// @code
//   auto loaded = sr::io::loadGltfScene("output/model.glb");
//   if (!loaded.success)
//       SR_LOG_ERROR("加载失败：{}", loaded.error);
//   const sr::SceneAsset& scene = loaded.scene;
// @endcode
// ============================================================

namespace sr::io
{

// 场景加载结果：状态 + 错误文本 + 自有资源
struct SceneLoadResult
{
    SceneAsset scene;
    bool success = false;
    std::string error;

    const Mesh *firstMesh() const { return scene.meshes.empty() ? nullptr : &scene.meshes.front(); }
};

// 加载 glTF（.gltf）或 GLB（.glb）场景。
SceneLoadResult loadGltfScene(const std::string &path);

// 便捷接口：只要第一个网格（失败时 out 保持不变）
bool loadGltfMesh(const std::string &path, Mesh &out, std::string *error = nullptr);

} // namespace sr::io
