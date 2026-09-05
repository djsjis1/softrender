# 31 · ECS 场景管理（EnTT）

> 本项目使用 **EnTT**（header-only，C++17，游戏行业广泛使用）作为 ECS 框架。自己写的 ECS 已删除——有成熟开源库就直接用。

## 一、为什么选 EnTT

- **header-only**：零编译依赖，`#include <entt/entt.hpp>` 即用
- **性能极好**：稀疏集存储，缓存友好，遍历速度接近手写数组
- **API 简洁**：`registry.create()`, `registry.emplace<T>()`, `registry.view<T1,T2>().each(...)`
- **生态成熟**：Unity/Unreal 之外最流行的 C++ ECS，文档完善

## 二、集成方式

源码在 `third/entt/`（v3.14.0），CMake 中声明为 INTERFACE 库：

```cmake
add_library(entt INTERFACE)
target_include_directories(entt INTERFACE third/entt/src)
```

渲染器本体通过 `softrenderer` INTERFACE 库传递 EnTT 依赖。

## 三、用法

```cpp
#include <entt/entt.hpp>
#include "scene/components.h"  // Position, Velocity, MeshRef 等

entt::registry registry;

// 创建 Entity + 组件
auto player = registry.create();
registry.emplace<Position>(player, Vec3f{0, 1, 0});
registry.emplace<Velocity>(player, Vec3f{0, 0, -5});
registry.emplace<MeshRef>(player, 0);

// System：物理更新
registry.view<Position, Velocity>().each([](Position& p, Velocity& v) {
    p.pos = p.pos + v.vel * dt;
});

// System：渲染
registry.view<Position, MeshRef>().each([&](Position& p, MeshRef& m) {
    renderer.drawMesh(meshes[m.meshIndex], p.pos);
});

// 销毁
registry.destroy(player);
```

## 四、组件定义

组件是纯 struct，放在 `src/scene/components.h`：

```cpp
struct Position { Vec3f pos{0,0,0}; };
struct Velocity { Vec3f vel{0,0,0}; };
struct Rotation { float yaw=0, pitch=0, roll=0; };
struct Scale { Vec3f scale{1,1,1}; };
struct MeshRef { int meshIndex = -1; };
struct MaterialRef { int materialId = 0; };
struct Light { Vec3f direction{0,-1,0}; Vec3f color{1,1,1}; float intensity=1.0f; };
struct CameraTag {};
```

## 五、对应文件

| 文件 | 作用 |
|---|---|
| `third/entt/src/entt/` | EnTT 源码（76 个头文件） |
| `src/scene/components.h` | 渲染器组件定义 |
| `tests/test_core.cpp` | ECS 测试（EnTT API） |

## 六、EnTT 进阶用法

- **Observer**：监听组件添加/移除事件
- **Snapshot**：序列化/反序列化整个 registry
- **Storage 多态**：运行时按类型名访问组件
- **Group**：预过滤的高性能视图

---

上一篇：[30 · 性能基准框架](30-性能基准框架.md)
下一篇：[32 · 延迟渲染 G-Buffer](../advanced/32-延迟渲染G-Buffer.md)
