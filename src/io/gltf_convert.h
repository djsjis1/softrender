#pragma once
#include <string>

// ============================================================
// glTF → SceneAsset 转换（私有）
//
// 只供 src/io/ 的实现单元与需要针对转换逻辑做白盒测试的内部单测使用。
// 公共安装包里不包含本头，使用方拿不到 tinygltf 类型。
// ============================================================

namespace tinygltf
{
class Model;
}

namespace sr::io
{
struct SceneLoadResult;
}

namespace sr::gltf
{

// 把已解析的 tinygltf::Model 转成自有场景资源。
// 任一步校验失败都返回 success=false 且 scene 为空。
sr::io::SceneLoadResult convertModel(const tinygltf::Model &model);

} // namespace sr::gltf
