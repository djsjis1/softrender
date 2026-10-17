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

> ✅ 上面两行传 `w` / `h` / `path` 是安全的，**但这并不是一直如此**。
>
> `core/log.h` 的内置（非 spdlog）模式旧版是真正的 C 可变参函数
> （`const char* fmt, ...` + `va_list` + `vfprintf`），把 `std::string` 经 `...`
> 传递是**未定义行为**（MSVC 报 C4840）—— 而不是直觉上的「只是忽略参数」。
> 现已改为变参模板 + `sr::formatLog`，任何有 `operator<<` 的类型都合法。
>
> 旧版还有两个已修的缺陷：① 格式串按 printf 语义解析，但约定写的是 `{}`，
> 所以**占位符从不被替换**、参数静默丢失（而 `unit_tests` 与大部分示例都走
> 内置模式，它是**常用路径**而非兜底）；② 日志文本里的字面 `%`（如
> `"100% done"`）会被 `vfprintf` 当转换符解析 → UB。
>
> 完整设计与降级语义对照表见 `docs/architecture.md` §6.2。
> 约束已录入 `engineering_rules.md` 红线：**禁止把内置模式改回 C 可变参**。

## 三、CMake 集成

```cmake
option(SR_ENABLE_SPDLOG "Enable spdlog" ON)
if(SR_ENABLE_SPDLOG)
    add_subdirectory(third/log EXCLUDE_FROM_ALL)
endif()

# 为需要日志的目标启用：
sr_enable_logging(stage10_shadow_live)
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
