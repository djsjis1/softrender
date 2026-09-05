# 50 · glTF 加载管线

> glTF 是 Khronos 的 3D 场景传输标准。本项目用 tinygltf 解析 .gltf/.glb 文件，转换为内部 Mesh 结构。

## 一、架构

```
.gltf/.glb 文件
       │
       ▼
  tinygltf 解析（third/tinygltf/）
       │
       ▼
  gltfloader::load() → LoadResult{meshes[], error}
       │
       ▼
  内部 Mesh 结构（vertices + indices）
       │
       ▼
  Renderer::drawMesh()
```

## 二、用法

```cpp
#include "scene/gltf_loader.h"

// 加载所有 mesh
auto result = gltfloader::load("model.glb");
if (result.success) {
    for (auto& mesh : result.meshes) {
        renderer.drawMesh(fb, mesh, shader);
    }
}

// 或只加载第一个
Mesh mesh;
gltfloader::loadFirstMesh("model.glb", mesh);
```

## 三、支持的 glTF 特性

| 特性 | 状态 |
|---|---|
| POSITION | ✅ |
| NORMAL | ✅ |
| TEXCOORD_0 | ✅ |
| 索引（u8/u16/u32） | ✅ |
| .gltf (JSON) | ✅ |
| .glb (二进制) | ✅ |
| Base64 内嵌 buffer | ✅ |
| 外部 .bin buffer | ✅ |
| 多 primitive | ✅ |
| 无法线时自动计算 | ✅ |
| PBR 材质解析 | 🔜 待实现 |
| 骨骼/动画 | 🔜 待实现 |

## 四、编译组织

tinygltf 的 IMPLEMENTATION 宏在 `src/scene/gltf_loader_impl.cpp` 中定义，编译为 `third_party_impl` 静态库的一部分。头文件 `gltf_loader.h` 只包含声明，可安全被多个编译单元 include。

## 五、对应文件

| 文件 | 作用 |
|---|---|
| `src/scene/gltf_loader.h` | 加载器接口（header-only） |
| `src/scene/gltf_loader_impl.cpp` | tinygltf IMPLEMENTATION |
| `third/tinygltf/tiny_gltf.h` | tinygltf 头文件 |
| `third/nlohmann/json.hpp` | JSON 解析（tinygltf 依赖） |
| `examples/demo_gltf.cpp` | 完整 demo（生成+加载+渲染+profile） |

---

上一篇：[49 · Chrome Tracing Profiler](49-Chrome-Tracing-Profiler.md)
下一篇：[51 · 代码规范与 clang-format](51-代码规范与clang-format.md)
