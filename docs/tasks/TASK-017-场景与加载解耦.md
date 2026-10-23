# TASK-017：场景与加载解耦

- 创建日期：2026-10-23
- 状态：待开始
- 父任务：[TASK-014](TASK-014-分层解耦重构.md)

## 目标

让运行时场景数据不依赖文件格式解析器：动画采样不再访问 tinygltf，加载细节只存在于 `src/io/`。

## 交付物

| 项 | 内容 |
|---|---|
| 自有结果 | glTF 公共结果改为 `SceneAsset`：网格、材质、纹理、节点、动画片段与蒙皮绑定 |
| 材质引用 | 材质以纹理索引引用资源，调用方绑定时解析为非拥有指针（结果移动/复制后不悬空） |
| 运行时类型 | `GltfMaterial` / `GltfNode` 的运行时内容迁为 `Material` / `SceneNode` |
| 动画数据 | `AnimationClip` 采样器持有已解码时间、值与三次插值切线；长度、类型、索引与有限值校验在加载阶段完成 |
| 蒙皮 | 蒙皮示例自行读取的关节与逆绑定矩阵搬入加载转换器 |
| 键帧动画 | skeleton 的简化欧拉角动画移到 `scene/keyframe_animation.h`，行为不变 |
| I/O 私有化 | `gltf_accessor` / `gltf_mesh` / `convertModel` 成为私有实现，实现移入 `src/io/*.cpp` |
| 实现单元 | 整理为 `stb_impl.cpp` / `tinygltf_impl.cpp` / `tinyobj_impl.cpp`，每个实现宏只定义一次 |
| 缓存 | `ResourceManager` → runtime 的 `ResourceCache`：`load<T>(key, factory)`，失败不入缓存 |
| 测试支持 | `src/test/pixel_regression.h` 迁入 tests/support，不再安装 |

## 验收

- 新增行为测试：加载器销毁后仍可采样动画
- 新增行为测试：资源结果移动/复制安全；缓存回调失败不入缓存
- 既有非法 glTF / 动画输入继续被拒绝
- 公共接口不出现 tinygltf / tinyobj / stbi / JSON 类型
- 保留 Linear / Step / CubicSpline、四元数处理、bind pose 与 channel 标志语义

## 边界

不扩展到完整 glTF 扩展或新动画功能；加载失败不发布半成品。