# 56 · glTF 纹理加载

引用链是 material → texture → image。默认 tinygltf 图像回调已经通过 stb_image 解码 image.image，
其中是像素而不是 PNG/JPEG 文件字节，不能再次传入 stbi_load_from_memory。

## 转换与所有权

1. tinygltf 从文件或内嵌内容解码图像。
2. convertModel 校验 source、尺寸、通道、分量位数及像素长度。
3. 将 8-bit 灰度、灰度+alpha、RGB 或 RGBA 转为内部 Color；Texture 创建 mipmap。
4. 在完整纹理数组建立后，将材质指针连接到 LoadResult 自有纹理。

目前不支持 16-bit/HDR 图像输入到该 glTF 路径，也没有应用 glTF sampler 的 wrap/filter 配置。
损坏或不支持的纹理使整体转换失败并提供 error，不返回带无效纹理的成功模型。
LoadResult 可移动、不可拷贝；材质引用和结果对象同寿命，调用方不能重分配它的 textures。

## 材质纹理槽

| 字段 | 当前连接 |
| --- | --- |
| baseColorTexture | 保存索引并设置 baseColorTexture 指针 |
| metallicRoughnessTexture | 保存索引并设置 metallicRoughnessTexture 指针 |
| normalTexture | 保存索引并设置 normalTexture 指针 |
| emissiveTexture | 仅保存索引，尚未接入纹理指针/着色链路 |

参数解析不代表 shader 自动应用全部材质特性；透明度、颜色空间及完整 PBR 材质整合仍需逐项验证。

实现见 `include/softrenderer/io/gltf_loader.h`，回归测试 `GltfTextureSafety` 验证解码像素数值、截断拒绝和移动后材质指针。

上一篇：[55 · IBL 环境反射](55-IBL环境反射.md)
下一篇：[57 · 骨骼动画播放](57-骨骼动画播放.md)
