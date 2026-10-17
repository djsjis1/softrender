# 24 · CMake 工程化：安装、导出与版本

本项目提供 `SoftRenderer::softrenderer` 公共目标。渲染算法主要是 inline/模板；资源加载需要编译 `third_party_impl` 静态库，因此完整库不等于“复制头文件即可使用”。

## 一、构建职责

| 文件 | 职责 |
| --- | --- |
| 根 `CMakeLists.txt` | 选项、公共库、接口依赖、开发标志和子目录接线 |
| `examples/CMakeLists.txt` | 阶段示例及完整的 Windows 目标守卫 |
| `practice/CMakeLists.txt` | 练习源码自动发现 |
| `tests/CMakeLists.txt` | 单元、像素与下游消费测试 |
| `cmake/version.cmake` | 从 `project(VERSION)` 生成版本头 |
| `cmake/install.cmake` | 头文件、实现库、依赖头和 CMake package |

项目内部路径使用 `PROJECT_SOURCE_DIR` / `PROJECT_BINARY_DIR`，不读取父工程根目录作为自己的源目录。库自身携带 C++23、线程和 include 要求；`sr_dev_flags` 中的 `/W4 /WX` 或 `-Wall -Wextra -Werror` 只作用于本仓目标。

Sanitizer 是目录级配置的有意例外：必须在创建第三方目标前设置，避免 MSVC STL 容器注解不一致。仅 Debug 启用，不导出为普通下游的编译要求。

## 二、版本头

版本唯一来源是根工程 `project(SoftRenderer VERSION ...)`。源码与安装消费者均使用：

```cpp
#include <softrenderer/version.h>
static_assert(SOFTRENDERER_VERSION_MAJOR >= 1);
```

`SOFTRENDERER_VERSION` 为字符串，`_MAJOR` / `_MINOR` / `_PATCH` 为整数宏。

## 三、安装闭包

安装包括项目公共头、生成的版本头、stb、tinyobjloader、tinygltf、json、EnTT 头，以及 `third_party_impl` 静态库。导出的 `SoftRendererConfig.cmake` 使用 `find_dependency(Threads)` 恢复线程目标。

所有 `INSTALL_INTERFACE` 目录与 `GNUInstallDirs` 一致，支持相对的自定义 `CMAKE_INSTALL_INCLUDEDIR` / `CMAKE_INSTALL_LIBDIR`。使用相对目录时，整个安装前缀可以迁移。非标准 libdir 需让下游的 `SoftRenderer_DIR` 指向其中的 `cmake/SoftRenderer`，不依赖默认包搜索规则。公共导出目标仍为：

- `SoftRenderer::softrenderer`
- `SoftRenderer::third_party_impl`
- `SoftRenderer::entt`（内部构建目标名为 `sr_entt`）

只需要链接第一个目标；其余由它传递。示例、练习、测试和日志示例依赖不安装。

## 四、回归验证

`consumer_subdirectory` 以另一个 CMake 工程作为父工程，验证默认关闭示例/测试且能编译链接公共头。
`consumer_installed` 安装后移动整个前缀，再由独立工程 `find_package`，验证版本宏、EnTT、glTF/JSON、OBJ、线程符号和程序运行。

```sh
ctest --test-dir build -C Release -L packaging --output-on-failure
```

这两个检查同时进入默认 Release CI。Debug Sanitizer 使用单元和像素测试，避免将带 ASAN 的实现库与未插桩下游混用。

配置命令及选项表统一见[构建与使用指南](46-构建与使用指南.md)。

上一篇：[23 · Alpha 混合](../advanced/23-Alpha混合与透明渲染.md)
下一篇：[25 · 日志系统](25-日志系统-spdlog集成.md)
