# 41 · glTF 模型加载（tinygltf）

> glTF 是 Khronos 定义的 3D 场景传输格式（"3D 的 JPEG"）。tinygltf 是单头文件 C++ 解析器，支持 .gltf（JSON）和 .glb（二进制）两种格式。

## 一、为什么用 glTF

- **行业标准**：Blender/Maya/Unity/Unreal 都支持导出
- **完整场景**：网格+材质+骨骼+动画+相机+灯光
- **二进制格式**：.glb 单文件，加载快
- **PBR 材质**：metallic-roughness 工作流原生支持

## 二、用法

```cpp
#include <softrenderer/scene/gltf_loader.h>

// 加载所有 mesh
auto result = gltfloader::load("model.glb");
if (result.success) {
    for (auto& mesh : result.meshes) { /* 渲染 */ }
}

// 或只加载第一个 mesh
Mesh mesh;
gltfloader::loadFirstMesh("model.glb", mesh);
```

支持范围和数据错误处理以[加载管线](50-glTF加载管线.md)为准。glTF 格式能够表达完整场景，不代表本项目已经实现全部特性。

## 三、依赖

tinygltf 依赖 nlohmann/json（JSON 解析）和 stb_image（纹理加载）。三者统一编译在 `third_party_impl` 库中。

## 四、对应文件

| 文件 | 作用 |
|---|---|
| `third/tinygltf/tiny_gltf.h` | tinygltf 头文件 |
| `third/nlohmann/json.hpp` | JSON 解析器 |
| `src/io/stb_impl.cpp` | stb_image / stb_image_write 实现 |
| `src/io/tinygltf_impl.cpp` | tinygltf 实现 |
| `include/softrenderer/io/gltf_loader.h` | 加载器包装 |

---

上一篇：[40 · CI/CD 配置](40-CI-CD配置.md)
下一篇：[42 · 粒子系统](../advanced/42-粒子系统.md)
