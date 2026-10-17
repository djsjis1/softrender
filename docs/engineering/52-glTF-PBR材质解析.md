# 52 · glTF PBR 材质解析

> glTF 的 metallic-roughness 工作流是 PBR 的行业标准。本项目解析常用材质参数，由示例选择字段传递给 PBR shader；尚未整合全部透明度、纹理槽和颜色空间行为。

## 一、解析的字段

| glTF 字段 | 内部结构 | 说明 |
|---|---|---|
| `baseColorFactor` | `GltfMaterial::baseColorFactor` | RGB 基色乘数 |
| `metallicFactor` | `GltfMaterial::metallicFactor` | 金属度 [0,1] |
| `roughnessFactor` | `GltfMaterial::roughnessFactor` | 粗糙度 [0,1] |
| `baseColorTexture` | `baseColorTextureIndex` | 基色纹理 |
| `metallicRoughnessTexture` | `metallicRoughnessTextureIndex` | 金属度/粗糙度纹理 |
| `normalTexture` | `normalTextureIndex` | 法线贴图 |
| `emissiveFactor` | `GltfMaterial::emissiveFactor` | 自发光颜色 |
| `alphaMode` | `AlphaMode::Opaque/Mask/Blend` | 透明模式 |
| `doubleSided` | `GltfMaterial::doubleSided` | 双面渲染 |

## 二、场景图解析

| glTF 字段 | 内部结构 | 说明 |
|---|---|---|
| `nodes[]` | `GltfNode` | 场景节点（TRS + mesh 引用） |
| `node.translation` | `GltfNode::translation` | 平移 |
| `node.rotation` | `GltfNode::rotation[4]` | 四元数旋转 |
| `node.scale` | `GltfNode::scale` | 缩放 |
| `node.matrix` | `GltfNode::matrix` | 直接矩阵（覆盖 TRS） |

## 三、用法

```cpp
auto result = gltfloader::load("model.glb");
for (size_t i = 0; i < result.meshes.size(); ++i) {
    int matIdx = result.meshMaterialIndices[i];
    // -1 表示没有显式材质，应使用调用方默认材质。
    GltfMaterial fallback;
    const auto& mat = matIdx >= 0 ? result.materials[size_t(matIdx)] : fallback;
    pbrShader.baseColor = mat.baseColorFactor;
    pbrShader.metallic = mat.metallicFactor;
    pbrShader.roughness = mat.roughnessFactor;
    renderer.drawMesh(fb, result.meshes[i], pbrShader);
}
```

## 四、对应文件

| 文件 | 作用 |
|---|---|
| `src/scene/gltf_types.h` | GltfMaterial, GltfNode, GltfAnimation 等类型 |
| `src/scene/gltf_loader.h` | 解析逻辑（材质+节点） |
| `examples/demo_pbr_material.cpp` | PBR 材质渲染 demo |

---

上一篇：[51 · 代码规范与 clang-format](51-代码规范与clang-format.md)
下一篇：[53 · 增量边函数与顺时针修复](../advanced/53-增量边函数与顺时针修复.md)
