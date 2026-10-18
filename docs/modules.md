# 模块职责

> 本文件描述 `src/` 各模块的职责边界、依赖方向、输入输出与扩展点。
> 模块间的完整依赖边与架构图见 [`architecture.md`](architecture.md)。
>
> 核心约定：**模块边界 = 目录边界**。禁止跨模块直接引用内部实现细节。

## 模块总览

| 目录 | 职责 | 文件数 | 依赖 |
| --- | --- | --- | --- |
| `src/math/` | 向量、矩阵、四元数、矩形、变换与投影 | 7 | 无（最底层） |
| `src/core/` | 帧缓冲、深度缓冲、颜色、混合、图像 I/O、后处理、基础设施 | 19 | `math/` |
| `src/scene/` | 网格、纹理、相机、骨骼、动画、粒子、模型加载、ECS 组件 | 14 | `math/`、`core/`、第三方加载器 |
| `src/shader/` | 可编程着色管线（vertex / geometry / fragment） | 4 | `math/`、`scene/`、`core/` |
| `src/raster/` | 光栅化、裁剪、3D 与 2D 渲染器 | 10 | 全部下层 |
| `src/test/` | 像素回归测试工具 | 1 | `core/` |
| `platform/` | 平台后端（Win32 GDI 窗口 + 输入） | 1 | `core/`；**不属于 `src/`** |

---

## src/math/ — 数学库

**职责**：提供渲染所需的全部线性代数原语。纯计算，无状态，无 I/O。

| 文件 | 关键类型 / 函数 | 说明 |
| --- | --- | --- |
| `vec.h` | `Vec2<T>` / `Vec3<T>` / `Vec4<T>`（别名 `Vec2f`/`Vec3f`/`Vec4f`）、`dot`、`cross`、`cross2`、`lerp`、`reflect`、`refract` | 唯一叶子模块。用 C++20 `requires` 约束实现泛型 `dot` |
| `mat4.h` | `Mat4`：`identity` / `translate` / `scale` / `rotate` / `inversed` / `transposed` / `transformPoint` / `transformDir` | 行主序、列向量约定 `p' = M * p` |
| `mat3.h` | `Mat3`：`translate` / `rotate` / `scale` / `shear` / `inversed` / `toMat4`；`perp` / `rotate2D` / `angleBetween` | 2D 仿射变换，与 `Mat4` 同约定 |
| `quat.h` | `Quat`：`fromAxisAngle` / `slerp` / `nlerp` / `toMat4` | 骨骼动画旋转插值 |
| `rect.h` | `Rect2<T>`（`Rectf` / `Recti`）：`contains` / `intersects` / `intersection` / `merged` / `center` | 构造签名为 `(x, y, w, h)` |
| `transform.h` | `namespace transform`：`lookAt` / `perspective` / `ortho` / `viewport` | **仅 3D**，只依赖 `mat4.h` |
| `transform2d.h` | `namespace transform`：`ortho2D` / `viewport2D`（返回 `Mat3`） | 独立成头，避免 3D 依赖面被 2D 污染 |

**坐标约定（全局统一，不可更改）**：右手系，y 向上，`Framebuffer` 的 `(0,0)` 在左下角。

**扩展点**：新增数学类型放本目录，只能依赖 `math/` 内部。禁止反向依赖 `core/` 及以上。

---

## src/core/ — 缓冲与基础设施

**职责**：像素存储、颜色运算、图像读写、后处理，以及与渲染无关的通用设施
（内存池、线程池、Profiler、日志）。

### 渲染目标

| 文件 | 关键类型 | 说明 |
| --- | --- | --- |
| `color.h` | `Color`（RGBA8）、`Color::fromFloat`、`namespace colors` 常量 | 叶子模块 |
| `framebuffer.h` | `Framebuffer`：`clear` / `setPixel` / `getPixel` / `row(y)` / `data()` | **核心渲染目标**。坐标约定见 `math/` |
| `depthbuffer.h` | `DepthBuffer`：`testAndSet` / `get` / `row(y)` / `clear`，`kFar = 1e9f` | 深度约定 `z ∈ [0,1]`，0 = 近平面 |
| `float_framebuffer.h` | `sr::FloatFramebuffer` | HDR 渲染中间目标 |
| `msaa_framebuffer.h` | `sr::MSAAFramebuffer`、`sr::kMSAA4Offsets` | 4x MSAA 多重采样 |
| `gbuffer.h` | `sr::GBuffer` | 延迟渲染的多渲染目标 |

> ⚠️ **边界检查契约**：`setPixel` 做运行时边界检查（越界静默忽略，因为光栅化时
> 三角形常部分在屏幕外）；`getPixel` / `DepthBuffer::get` 在 Debug 有 assert，
> Release 越界返回黑色 / 最远深度。`row(y)` 只提供断言，调用前**必须**自行保证合法行列。
> `renderer.h` 通过把包围盒 clamp 到帧缓冲尺寸来满足此契约。

### 像素运算与图像 I/O

| 文件 | 关键内容 |
| --- | --- |
| `blend.h` | `enum class BlendMode`（Opaque / AlphaBlend / Additive / Multiply / Premultiplied）、`blendPixels(src, dst, mode)` |
| `tga.h` | `namespace tga`：`Image`、`read` / `write`。按 y-up 约定设置 TGA 原点标志位 |
| `image_write.h` | `namespace imagewriter`：`writeImage`（PNG/JPG/BMP/TGA，基于 stb_image_write） |
| `post_processing.h` | `namespace postprocess`：统一后处理入口——FXAA 抗锯齿、DOF 景深、径向模糊、暗角、色差、SSAO 环境光遮蔽 |

### 通用设施

| 文件 | 关键内容 |
| --- | --- |
| `arena.h` | `sr::ArenaAllocator`：安全对齐与长度校验，reset 保留并复用块；used/capacity 分别观测当前使用和保留容量 |
| `command_buffer.h` | `sr::CommandBuffer` / `sr::ThreadPool`：任务分发、waitAll 异常报告和 submitFuture 结果 |
| `profiler.h` | `sr::Profiler`（Meyers 单例）、`SR_PROFILE_SCOPE` 宏，输出 Chrome Tracing JSON |
| `resource_manager.h` | `sr::ResourceManager`：引用计数缓存，用 C++20 `concepts`（`has_loadFromFile`）约束资源类型 |
| `benchmark.h` | `sr::` 基准测试工具，用于对比标量 / SSE2 光栅化吞吐 |
| `log.h` | `SR_LOG_*` 宏 + `sr_log_init` / `sr_log_shutdown`。双模式条件编译，见 `architecture.md` §6.2 |

> ⚠️ `sr_log_init` / `sr_log_shutdown` 位于**全局命名空间**，而 `LogLevel` / `Log`
> 在 `namespace sr` 内。这是历史遗留的不一致。

**扩展点**：新的缓冲类型放本目录，只能依赖 `math/` 与 `core/` 内部。
通用设施（内存 / 线程 / 性能分析）也归本目录，但**禁止**依赖 `scene/` 及以上。

---

## src/scene/ — 场景资源与加载

**职责**：描述「要画什么」——几何、材质、纹理、相机、动画、粒子，以及从磁盘
格式（OBJ / glTF / 图像）到内存结构的加载。

### 核心资源

| 文件 | 关键类型 | 说明 |
| --- | --- | --- |
| `mesh.h` | `Vertex`（position / normal / uv / tangent）、`Mesh`（vertices + indices，`triangleCount()`）、`namespace meshgen` 程序化生成 | 全局命名空间 |
| `texture.h` | `Texture`：`sampleNearest` / `sampleBilinear` / `sampleTrilinear`、mipmap 链、Repeat 环绕、`valid()`；`namespace texgen` 程序化生成 | |
| `cubemap.h` | `Cubemap`、`enum class CubeFace : int` | IBL 环境光照 |
| `camera.h` | `Camera`（FPS 自由相机 + orbit 轨道模式）、`Camera2D`（正交 2D 相机） | 见下 |
| `gltf_types.h` | `GltfMaterial` / `GltfTexture` / `GltfImage` / `GltfNode` / `GltfSkin` / `GltfAnimation`、`enum class AlphaMode` | glTF 常用 PBR 参数与场景元数据 |

**`Camera`（3D）**：用 `yaw` / `pitch` 描述朝向，`pitch` 夹在 ±89° 防万向节翻转。
输出 `viewMatrix()` / `projMatrix()`。交互接口：`addYawPitch` / `moveForward` /
`moveRight` / `moveUp` / `orbit`。

**`Camera2D`**：`position`（屏幕中心对应的世界坐标）+ `zoom`（像素/世界单位）+
`rotation` + `yDown`。输出 `Mat3` 的 `viewMatrix()` / `projMatrix()` / `viewProj()`。
提供 `worldToScreen` / `screenToWorld`（鼠标拾取）、`visibleWorldRect`（2D 剔除）、
`zoomToFit`、`pan` / `zoomBy`。

### 动画与粒子

| 文件 | 关键类型 |
| --- | --- |
| `skeleton.h` | `Bone` / `Skeleton` / `VertexBoneWeights` / `Keyframe` / `AnimationChannel` / `Animation` |
| `animation_player.h` | `AnimSample`：按时间采样 glTF 动画，Linear / Step / CubicSpline；旋转 SLERP 或归一化 Hermite |
| `particle_system.h` | `Particle` / `EmitterConfig` / `ParticleSystem`：Billboard 发射器 + 重力 + 颜色渐变 |

### 加载器

| 文件 | 加载格式 | 命名空间 |
| --- | --- | --- |
| `image_loader.h` | PNG / JPG / BMP / TGA / PSD / GIF / HDR / PIC / PNM（stb_image） | `imageloader` |
| `obj_loader.h` | Wavefront OBJ（tinyobjloader） | `objloader` |
| `gltf_accessor.h` / `gltf_mesh.h` | 共享字节校验与 primitive 转换 | `sr::gltf` |
| `gltf_loader.h` + `gltf_loader_impl.cpp` | glTF 2.0 / glb（tinygltf） | `gltfloader` |

> ⚠️ `gltf_loader.h` 与 `animation_player.h` 在公共头中 include `tiny_gltf.h`，
> 下游用户需要 tinygltf 与 nlohmann/json 的头文件路径。这是**有意的接口泄漏**
> （`animation_player` 需直接访问 `tinygltf::Model` 的动画数据）。

### ECS

| 文件 | 内容 |
| --- | --- |
| `components.h` | `namespace sr`：`sr::Position` / `sr::Velocity` / `sr::Rotation` / `sr::Scale` / `sr::MeshRef` / `sr::MaterialRef` / `sr::Light` / `sr::CameraTag` |
| `render_queue.h` | `DrawItem` / `RenderQueue` / `enum class RenderLayer : uint16_t`：透明排序 |

ECS 容器本身来自 EnTT（`third/entt`），`components.h` 只定义组件数据结构。

> ✅ **这 8 个组件已迁入 `namespace sr`，且未保留全局别名**（ADR-001 第 1 批）。
> 它们曾是命名空间收敛的最高优先级目标：名字均为 ECS 领域通用名，
> 而本项目是 header-only 库，全局定义会让下游用户
> `#include "scene/components.h"` 时直接撞上重定义硬错误。
>
> 能不留别名直接迁完，是因为实测引用面极小：本文件只被 2 个文件引用
> （自身 + `tests/unit/test_core.cpp`），`Light` 除定义处外全仓零使用，
> 22 个示例无一使用 ECS。
>
> 调用时必须写 `sr::` 前缀：`registry.emplace<sr::Position>(e, Vec3f{...})`。
> 注意 `Vec3f` 本身仍在全局命名空间（属后续批次）。
>
> 回归防护：`tests/unit/test_core.cpp` 在全局作用域声明了 8 个同名哨兵类型。
> 若将来有人加回 `using sr::Position;`，测试文件会直接报重定义错误。

---

## src/shader/ — 可编程着色管线

**职责**：定义 GPU 风格的两段式着色契约，并提供若干内置实现。

### 契约（`shader.h`）

```cpp
struct Varyings {          // 顶点着色器输出 → 光栅化器插值 → 片元着色器输入
    Vec3f worldPos;        // 世界空间位置
    Vec3f normal;          // 世界空间法线
    Vec2f uv;              // 纹理坐标
    Vec3f color{1,1,1};    // 顶点色（Gouraud 把逐顶点光照结果存这里）
    Vec3f tangent;         // 世界空间切线（法线贴图 TBN）
};

struct Shader {
    // uniforms：渲染前由外部设置，着色期间对所有顶点/片元只读
    Mat4 model, view, proj, mvp, normalMatrix;
    Vec3f lightDir, lightColor, ambient, cameraPos, baseColor;
    float shininess;
    const Texture* texture;

    virtual Vec4f vertex(const Vertex& in, Varyings& out) = 0;
    virtual void  geometry(const Varyings&, const Varyings&, const Varyings&) {}
    virtual bool  fragment(const Varyings& v, Vec3f& color) = 0;   // false = discard

    Vec3f toWorldNormal(const Vec3f&) const;   // 逆转置变换法线
    Vec3f sampleAlbedo(const Vec2f& uv) const; // 无贴图时返回 baseColor
};
```

`interpolateVaryings(a, b, c, bc, iw0, iw1, iw2)` 实现**透视校正插值**：
`A = Σ(bc_i · A_i / w_i) / Σ(bc_i / w_i)`。

### 内置实现

| 文件 | 着色器 |
| --- | --- |
| `shaders.h` | `FlatShader`（逐三角形面法线）、`GouraudShader`（逐顶点光照）、`PhongShader`（逐像素光照） |
| `pbr_shader.h` | `PBRShader`：Cook-Torrance BRDF（依赖 `scene/cubemap.h` 做 IBL） |
| `advanced_shaders.h` | `DepthShader`（阴影贴图深度pass）、`FinalShader`（后处理合成） |

**扩展点**：新增着色器 = 继承 `Shader` 并实现 `vertex` / `fragment`。
`geometry` 有默认空实现，只在需要逐三角形数据（如 Flat 的面法线）时覆写。
**禁止**修改 `Varyings` 的字段语义——它是 `Renderer` 与所有着色器的公共契约。

---

## src/raster/ — 光栅化与渲染器

**职责**：把几何图元转成像素。分 3D 与 2D 两条对等路径，共享底层图元。

### 共享底层（全局命名空间，3D 与 2D 共用）

| 文件 | 内容 |
| --- | --- |
| `line.h` | Bresenham 画线、`drawLineGradient` |
| `triangle.h` | 边函数 + 重心坐标三角形填充 |

### 3D 路径

| 文件 | 内容 |
| --- | --- |
| `clip.h` | `ClipVertex`（裁剪空间坐标 + `Varyings`）、`enum class ClipPlane : uint8_t`、`kClipPlaneCount`、`clipTriangleFullFrustum`：完整 6 面 Sutherland-Hodgman，含顺时针绕序修复 |
| `renderer.h` | `Renderer`：`clear` / `drawMesh` / `depth()`，开关 `cullBackface` / `nearClip` |
| `rasterizer_fast.h` | `namespace raster_fast`：增量边函数 + SSE2 四像素并行。**实验性，未接入主链路**，见 `architecture.md` §6.1 |

`Renderer::drawMesh` 的固定流水线（8 步）：顶点着色 → 视锥裁剪 → 透视除法 →
视口变换 → 背面剔除 → 光栅化 → 深度测试 → 透视校正插值 + 片元着色。
详见 `architecture.md` §4 的时序图。

### 2D 路径（`namespace draw2d`）

| 文件 | 内容 |
| --- | --- |
| `shape2d.h` | `ClipRect`；`fillRect` / `drawRect` / `fillRectGradient` / `fillCircle` / `drawCircle` / `fillEllipse` / `drawEllipse` / `fillRoundedRect` / `drawRoundedRect` / `fillPolygon` / `drawPolygon` / `drawThickLine`；底层 `plotBlend` / `drawLine` |
| `blit2d.h` | `SpritePalette` / `tintPixel`；`drawCharSprite` / `blitImage` / `blitImageRect` / `blitImageScaled` / `blitImageTransformed` / `blitImageRotated` / `blitFramebuffer` / `blitTexture` |
| `renderer2d.h` | `Renderer2D`：立即模式，封装 `Mat3` 变换栈（`push`/`pop`）、`Camera2D`、scissor、`BlendMode`；内置 5×7 点阵字体 `glyph5x7` |

**`plotBlend` 是 2D 子系统唯一的像素写入出口**，统一处理：边界裁剪 → `BlendMode`
混合 → 可选 `ClipRect` scissor。短路规则：仅 `Opaque`，或（`a == 255` 且模式为
`AlphaBlend` / `Premultiplied`）时直接覆盖；`Additive` / `Multiply` 不短路。

> **为什么 2D 符号都在 `draw2d` 而底层 `triangle.h`/`line.h` 在全局**：
> 2D 图元函数名（`fillRect` / `fillCircle` / `SpritePalette`）曾与 `examples/` 中
> 已有的全局同名符号冲突，故整体收进 `draw2d`。而 `triangle.h` / `line.h` 是
> 3D 与 2D 共享的最底层图元，保持全局以避免双重限定。
>
> **调用 2D 图元层时必须加 `draw2d::` 前缀**（单元测试中同样如此）。

**扩展点**：新增 2D 图元 → `shape2d.h`；新增图像合成 → `blit2d.h`；
两者都必须通过 `plotBlend` 写像素，禁止直接调 `Framebuffer::setPixel`
（否则会绕过 scissor 与混合模式）。

---

## src/test/ — 测试工具

| 文件 | 内容 | 状态 |
|---|---|---|
| `pixel_regression.h` | `namespace regression`：`CompareResult`、`compare`、`saveGolden`、`compareWithGolden` | ✅ 已接线（TASK-004），3 个回归场景 + 7 个单元用例 |

`pixel_regression` 的通过判据：不匹配像素占比 < 0.1% **且** 平均差值 < 0.5。
基准图存放于 `tests/fixtures/golden/<name>.tga`。

> `test_framework.h`（自研 minitest）已删除（TASK-004），项目统一使用 GoogleTest。

---

## platform/ — 平台后端

**不属于渲染器本体**（不在 `src/` 下），是「如何把 `Framebuffer` 显示出来」的
使用层实现。

`win32_window.h` 提供 `Win32Window`：构造 / `pumpMessages` / `present` /
`setTitle` / `keyDown` / `consumeMouseDelta`。整个文件包在 `#ifdef _WIN32` 内。

**移植方式**：照此公开接口另写 `SDL2Window` / `GLFWWindow` / `X11Window`，
渲染器本体一行不改。

> ⚠️ 当前 `examples/` 中三个实时窗口示例**无条件**引用 `Win32Window`，
> 非 Windows 平台会编译失败。见 `architecture.md` §8 与 `docs/tasks/`。

---

## 依赖方向速查（红线）

```
math/  ←  core/  ←  scene/  ←  shader/  ←  raster/  ←  examples/ + platform/
```

- 只能向左依赖，**禁止反向**
- `src/` 内**禁止** include `platform/` 或 `<windows.h>`
- 已知违例：`core/benchmark.h → raster/rasterizer_fast.h`（层次倒置，待修）

## 2026-10-06 光栅化新增模块

| 文件 | 职责 |
| --- | --- |
| src/raster/coverage.h | sr::TriangleCoverage / RasterBounds，整数子像素覆盖与有限包围盒 |
| src/raster/msaa_renderer.h | sr::MSAAFlatRenderer<1/4>，常量颜色的逐样本覆盖与深度/颜色提交 |
| benchmarks/renderer_benchmark.cpp | 可选主管线基线；配置和数据见 [性能记录](engineering/58-覆盖规则与性能基线.md) |
