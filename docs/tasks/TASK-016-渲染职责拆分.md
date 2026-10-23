# TASK-016：渲染职责拆分

- 创建日期：2026-10-23
- 状态：待开始
- 父任务：[TASK-014](TASK-014-分层解耦重构.md)

## 目标

把 `render/` 的五个子域职责拆清（raster / draw2d / pipeline / shader / postprocess），
并移除渲染头对宿主能力的依赖。

## 交付物

| 项 | 内容 |
|---|---|
| Shader 契约 | `Shader` 只保留 vertex / geometry / fragment；uniforms、法线变换与纹理采样移入 `SurfaceShader` 基类 |
| 缓冲与效果 | `FloatFramebuffer` 只保留存储与访问；ACES / gamma / LDR resolve / brightPass / gaussianBlur / addBlend 迁为后处理自由函数 |
| 后处理 | 单一入口 `render/postprocess/post_processing.h`，按 FXAA、HDR/Bloom、SSAO 分区；参数默认值、计算次序与数值公式不变 |
| 延迟光照 | `GBuffer` 存储留 core；`lightingPass` 移到 `render/shader/deferred_lighting.h` |
| 数学辅助 | `barycentric` 拆到纯数学辅助头；`wireTriangle` 归 draw2d |
| 2D 边界 | `Renderer2D` 改接收 `setViewTransform(const Mat3&)`，由应用传入 Camera2D 的计算结果 |
| 宿主解耦 | Renderer 内的 Profiler 计时移到示例/基准调用处；绘制头不再引入时钟、线程、锁或文件写出 |
| 归位 | 程序化生成归独立 generator 头；`RenderQueue` 归 `render/pipeline/` |

## 验收

- 纯 render 模块不可 include io / runtime / platform / 第三方头（新增 include 白名单测试）
- 新增行为测试：HDR 与 GBuffer 效果提取前后结果一致
- 169 存量单测与 3 像素回归结果不变；20 个离屏程序退出码 0
- 不接入或替换光栅化算法（`rasterizer_fast` 保持技术验证定位）

## 边界

本阶段只调整职责与依赖方向，不改渲染结果。`draw2d::plotBlend` 仍是 2D 唯一像素写入出口。