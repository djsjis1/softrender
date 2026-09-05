# 56 · glTF 纹理加载

> glTF 的纹理通过 images → textures → material 三级引用。本项目用 stb_image 从内存解码图像数据。

## 一、加载流程

```
glTF JSON
  └── images[]     → 原始字节（base64 或外部文件）
  └── textures[]   → 指向 image + sampler
  └── materials[]  → 指向 texture indices

加载时：
  1. tinygltf 解析 JSON，填充 model.images[i].image（字节数组）
  2. 用 stbi_load_from_memory 解码为 RGBA 像素
  3. 创建 Texture 对象（自动生成 mipmap）
  4. 填充 GltfMaterial 的纹理指针
```

## 二、支持的纹理槽

| 槽位 | glTF 字段 | 用途 |
|---|---|---|
| baseColor | `pbrMetallicRoughness.baseColorTexture` | 基色/漫反射 |
| metallicRoughness | `pbrMetallicRoughness.metallicRoughnessTexture` | G 通道=roughness, B 通道=metallic |
| normal | `normalTexture` | 切线空间法线扰动 |
| emissive | `emissiveTexture` | 自发光颜色 |

## 三、对应代码

| 文件 | 作用 |
|---|---|
| `src/scene/gltf_loader.h` | 纹理解析 + stb_image 解码 |
| `src/scene/gltf_types.h` | GltfTexture / GltfImage 类型 |

---

上一篇：[55 · IBL 环境反射](55-IBL环境反射.md)
下一篇：[57 · 骨骼动画播放](57-骨骼动画播放.md)
