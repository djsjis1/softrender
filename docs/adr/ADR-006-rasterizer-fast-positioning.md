# ADR-006：`rasterizer_fast.h` 定位为验证工具，不接入主渲染链路

- 日期：2026-10-26
- 状态：已采纳

## 背景与选择

`include/softrenderer/render/raster/rasterizer_fast.h` 是完整的 SSE2 增量光栅化
实现（含标量降级路径），自诞生起就未接入 `Renderer` 主链路，长期处于
「要不要接」的悬置状态（此前列在待立 ADR 决策点）。

两条不接入的技术理由（architecture.md §6.1 已记录，至今成立）：

1. **写入路径反而更慢**：其深度写入走 `testAndSet` + `setPixel`，每个像素
   各做一次边界检查；主路径用 `zbuf_.row(y)` / `fb.row(y)` 行指针直写，
   边界检查在包围盒 clamp 时一次性完成。
2. **接口容量不足**：只接受单一 `Color` 输出，没有 `Varyings` 透视校正
   插值与 `Shader::fragment` 回调，接进来等于重写。

备选 A（立即接入主链路）：需要先设计 Shader 批量输出接口（48 号文路线 A），
属于架构级变更；且几何侧 1080p 仅约 31 ms，不是当前瓶颈（后处理占整帧 81%）。
按 ROI 排序后推迟。**拒绝（当前阶段）**。

备选 B（删除）：其标量/SSE2 一致性用例（`SSE2MatchesScalar`、
`fillTriangleFast_scalarAndSIMD_agreeAcrossTailsAndWinding`）与压测
`render_path_consistency` 是覆盖规则唯一的独立实现交叉验证，删除会失去
这层回归保险。**拒绝**。

备选 C（采用）：定位为**覆盖规则的独立验证实现 + SIMD 技术预研载体**，
维持现状不接入；未来若主链路做增量插值/SIMD 化（先拆阶段基准、再立项），
以其为起点另立 ADR。

## 影响

- 全仓引用保持：单元测试、压测 `render_path_consistency`、benchmark 支撑头注释。
- 「待立 ADR」清单移除本决策点。
- 若几何侧经阶段拆分基准被证明重新成为瓶颈，须新立 ADR 重新评估。
