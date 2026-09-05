# 24 · CMake 工程化：安装、导出与版本

> 企业级库必须能被其他项目 `find_package` 使用。本篇讲清本项目的 CMake 工程化：INTERFACE 库目标、版本头生成、安装规则、Config 文件导出。

## 一、Header-only INTERFACE 库

渲染器本体全是 `inline` / 模板，无需编译 `.cpp`。CMake 用 `add_library(softrenderer INTERFACE)` 声明一个纯头文件目标，下游只需：

```cmake
find_package(SoftRenderer REQUIRED)
target_link_libraries(myapp PRIVATE SoftRenderer::softrenderer)
```

即可自动获得 include 路径和 C++17 要求。

## 二、版本管理

`cmake/version.cmake` 定义 `SOFTRENDERER_VERSION_MAJOR/MINOR/PATCH`，通过 `configure_file` 生成 `version.h`，代码中可用 `SOFTRENDERER_VERSION` 宏。

## 三、安装规则

`cmake/install.cmake` 负责：

- 安装 `src/**/*.h` 到 `<prefix>/include/softrenderer/`
- 安装生成的 `version.h`
- 导出 `SoftRendererTargets.cmake`
- 生成 `SoftRendererConfig.cmake` + `SoftRendererConfigVersion.cmake`

## 四、可选依赖

| 选项 | 默认 | 说明 |
|---|---|---|
| `SR_ENABLE_SPDLOG` | ON | 启用 spdlog 日志（third/log） |
| `SR_ENABLE_TESTS` | ON（enable_testing） | 编译 Google Test 单元测试 |

关闭 spdlog：`cmake -DSR_ENABLE_SPDLOG=OFF ...`，渲染器本体回退到内置极简日志。

## 五、对应文件

| 文件 | 作用 |
|---|---|
| `CMakeLists.txt` | 主构建脚本 |
| `cmake/version.cmake` | 版本号定义 |
| `cmake/version.h.in` | 版本头模板 |
| `cmake/install.cmake` | 安装规则 |
| `cmake/SoftRendererConfig.cmake.in` | find_package 配置模板 |

---

上一篇：[23 · Alpha 混合与透明渲染](../advanced/23-Alpha混合与透明渲染.md)
下一篇：[25 · 日志系统：spdlog 集成](25-日志系统-spdlog集成.md)
