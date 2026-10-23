# 49 · Chrome Tracing Profiler

> 零依赖性能分析工具，输出 Chrome Trace Event Format JSON，可在 Perfetto UI / chrome://tracing 中可视化。

## 一、用法

```cpp
#include <softrenderer/core/profiler.h>

void renderFrame() {
    SR_PROFILE_FUNCTION();  // 自动用函数名
    // 或
    SR_PROFILE_SCOPE("rasterize");  // 自定义名称
    SR_PROFILE_SCOPE_CAT("shadow_pass", "shadow");  // 带分类
}

int main() {
    renderFrame();
    sr::Profiler::instance().flush("output/trace.json");
}
```

## 二、查看方式

1. 打开 [ui.perfetto.dev](https://ui.perfetto.dev)
2. 拖入 `trace.json`
3. 看到时间线火焰图，每个 scope 的耗时一目了然

## 三、API

| 函数 | 说明 |
| --- | --- |
| `SR_PROFILE_SCOPE(name)` | RAII scope，自动记录进入/退出时间 |
| `SR_PROFILE_FUNCTION()` | 用 `__FUNCTION__` 作为名称 |
| `Profiler::instance().flush(path)` | 输出 JSON |
| `Profiler::instance().setEnabled(false)` | 禁用 profiling（零开销） |
| `Profiler::instance().clear()` | 清空事件 |

## 四、设计要点

- **零依赖**：纯 C++23 标准库
- **线程安全**：mutex 保护事件列表
- **可选开销**：`setEnabled(false)` 后 beginScope/endScope 为空操作
- **Chrome Trace Format**：工业标准，所有主流 profiler UI 都支持

## 五、对应代码

`include/softrenderer/runtime/profiler.h`

---

上一篇：[48 · 性能优化指南](48-性能优化指南.md)
下一篇：[50 · glTF 加载管线](50-glTF加载管线.md)
