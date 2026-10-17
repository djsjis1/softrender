# 31 · ECS 场景管理（EnTT）

> 本项目使用 **EnTT**（header-only，C++20，游戏行业广泛使用）作为 ECS 框架。自己写的 ECS 已删除——有成熟开源库就直接用。

## 一、为什么选 EnTT

- **header-only**：零编译依赖，`#include <entt/entt.hpp>` 即用
- **性能极好**：稀疏集存储，缓存友好，遍历速度接近手写数组
- **API 简洁**：`registry.create()`, `registry.emplace<T>()`, `registry.view<T1,T2>().each(...)`
- **生态成熟**：Unity/Unreal 之外最流行的 C++ ECS，文档完善

## 二、集成方式

源码在 `third/entt/`（v3.14.0），CMake 中声明为 INTERFACE 库：

```cmake
add_library(entt INTERFACE)
target_include_directories(entt INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/third/entt/src>
    $<INSTALL_INTERFACE:include/softrenderer/entt>
)
target_compile_features(entt INTERFACE cxx_std_23)
```

> ⚠️ `third/entt/` 下存在**两份版本不同的 EnTT**：`src/` 是 3.14.0（构建实际使用），
> `single_include/` 是陈旧的 3.13.2。升级时必须同步处理，详见 `third/README.md`。

渲染器本体通过 `softrenderer` INTERFACE 库传递 EnTT 依赖。

## 三、用法

> ⚠️ **组件类型位于 `namespace sr`**（ADR-001 第 1 批）。这 8 个名字是 ECS 领域
> 通用名，而本项目是 header-only 库，放在全局命名空间会让下游使用者
> 直接撞上重定义硬错误。必须写 `sr::` 前缀，或在应用代码里加
> `using namespace sr;`。

```cpp
#include <entt/entt.hpp>
#include "scene/components.h"  // sr::Position, sr::Velocity, sr::MeshRef 等

entt::registry registry;

// 创建 Entity + 组件
auto player = registry.create();
registry.emplace<sr::Position>(player, Vec3f{0, 1, 0});
registry.emplace<sr::Velocity>(player, Vec3f{0, 0, -5});
registry.emplace<sr::MeshRef>(player, 0);

// System：物理更新
registry.view<sr::Position, sr::Velocity>().each([](sr::Position& p, sr::Velocity& v) {
    p.pos = p.pos + v.vel * dt;
});

// System：渲染
registry.view<sr::Position, sr::MeshRef>().each([&](sr::Position& p, sr::MeshRef& m) {
    renderer.drawMesh(meshes[m.meshIndex], p.pos);
});

// 销毁
registry.destroy(player);
```

写完整程序时可在 include 之后加一行 `using namespace sr;`，就不需逐处写前缀
（`examples/` 中的 `demo_stars.cpp`、`stage11_msaa.cpp`、`stage13_postprocess.cpp`
就是这么做的）。

> ⚠️ **单元测试文件里不能用 `using namespace sr;`**：gtest 的 `TEST(GBuffer, ...)`
> 会创建名为 `GBuffer` 的命名空间，与 using-directive 引入的 `sr::GBuffer` 冲突；
> 且 `tests/unit/test_core.cpp` 故意在全局作用域声明了 8 个同名哨兵类型用来
> 验证迁移成果，与 directive 构成二义性（MSVC C2872）。测试统一写显式 `sr::`。

## 四、组件定义

组件是纯 struct，放在 `src/scene/components.h` 的 `namespace sr` 内：

```cpp
namespace sr
{

struct Position     { Vec3f pos{0, 0, 0}; };
struct Velocity     { Vec3f vel{0, 0, 0}; };
struct Rotation     { float yaw = 0, pitch = 0, roll = 0; };
struct Scale        { Vec3f scale{1, 1, 1}; };
struct MeshRef      { int meshIndex = -1; };   // -1 = 未设置（0 是合法下标）
struct MaterialRef  { int materialId = 0; };
struct Light        { Vec3f direction{0, -1, 0}; Vec3f color{1, 1, 1}; float intensity = 1.0f; };
struct CameraTag    {};                        // 空标记组件，仅用于 view 过滤

} // namespace sr
```

> `Vec3f` 本身仍在全局命名空间（属 ADR-001 后续批次），所以组件内部
> 与使用者都可直接写 `Vec3f`，无需限定。

### 回归防护：编译期哨兵

`tests/unit/test_core.cpp` 在**全局作用域**声明了 8 个同名哨兵类型：

```cpp
struct Position { int canary = 0; };
struct Light    { int canary = 0; };
// … 共 8 个
```

这能编译通过本身就证明了目标达成：下游使用者现在**可以**定义自己的
`Position` / `Light` 而不与本库冲突。若将来有人在 `components.h` 里加回
`using sr::Position;`，测试文件会立即报**重定义编译错误**。

## 五、对应文件

| 文件 | 作用 |
| --- | --- |
| `third/entt/src/entt/` | EnTT 源码（76 个头文件） |
| `src/scene/components.h` | 渲染器组件定义 |
| `tests/unit/test_core.cpp` | ECS 测试（EnTT API） |

## 六、EnTT 进阶用法

- **Observer**：监听组件添加/移除事件
- **Snapshot**：序列化/反序列化整个 registry
- **Storage 多态**：运行时按类型名访问组件
- **Group**：预过滤的高性能视图

---

上一篇：[30 · 性能基准框架](30-性能基准框架.md)
下一篇：[32 · 延迟渲染 G-Buffer](../advanced/32-延迟渲染G-Buffer.md)
