# 25 · 日志系统：spdlog 集成

> 企业级库必须有可控、高性能的日志。本项目通过条件编译支持两种模式：内置极简（零依赖）和 spdlog（完整功能）。

## 一、双模式设计

| 模式 | 触发条件 | 底层 | 特性 |
|---|---|---|---|
| 内置 | 默认（未定义 `SR_USE_SPDLOG`） | `fprintf(stderr)` | 零依赖，分级，线程安全 |
| spdlog | 定义 `SR_USE_SPDLOG` + 链接 `mylog` | spdlog 1.17 | 异步、文件滚动、彩色、fmt 格式化 |

渲染器本体（`src/`）的 `core/log.h` 通过 `#ifdef SR_USE_SPDLOG` 切换。

## 二、使用方式

```cpp
#include "core/log.h"

int main() {
    sr_log_init();  // 初始化（spdlog 模式下配置控制台+文件）
    SR_LOG_INFO("Renderer started, resolution {}x{}", w, h);
    SR_LOG_WARN("Texture not found: {}", path);
    // ...
    sr_log_shutdown();
}
```

## 三、CMake 集成

```cmake
option(SR_ENABLE_SPDLOG "Enable spdlog" ON)
if(SR_ENABLE_SPDLOG)
    add_subdirectory(third/log EXCLUDE_FROM_ALL)
endif()

# 为需要日志的目标启用：
sr_enable_logging(stage8_realtime)
```

`sr_enable_logging` 函数自动添加 `SR_USE_SPDLOG` 定义和 `mylog` 链接。

## 四、日志级别

Trace < Debug < Info < Warn < Error < Critical < Off

运行时可调：`sr::Log::setLevel(sr::LogLevel::Warn)` （内置模式）或 `LOGS_WARN` 以上才输出（spdlog 模式通过 `LogConfig::level` 配置）。

## 五、对应文件

| 文件 | 作用 |
|---|---|
| `src/core/log.h` | 条件编译日志接口 |
| `third/log/Logger.h` | spdlog 封装（mylog 命名空间） |
| `third/log/Logger.cpp` | 封装实现 |
| `third/log/spdlog-1.17.0/` | spdlog 源码 |

---

上一篇：[24 · CMake 工程化：安装、导出与版本](24-CMake工程化-安装导出与版本.md)
下一篇：[26 · 内存池分配器](26-内存池分配器.md)
