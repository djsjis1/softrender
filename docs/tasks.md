# 进行中任务与遗留项

> 本页只记录**尚未完成**的工作与当前阻塞；已完成任务的过程留在 [changelog.md](changelog.md) 与 Git 历史里，不再单独建档。
> 当前验证数值以 [project_context.md](project_context.md) 为准。

## 待办

| 事项 | 状态 | 下一步与验收 |
| --- | --- | --- |
| clang-format 全仓格式化 | 未执行 | 需在所有功能修改结束后单独提交一次，避免与逻辑改动混在一起 |
| 覆盖率棘轮基线维护 | 已接入 CI | 门槛与实测持平（容差 0.05）；结构改动导致覆盖率上升时，单独提交上调基线 |
| 阴影贴图收敛进库 | 仍在示例侧 | 收敛需先定公共 API（着色器是否接收 light 矩阵、由谁管理 shadow map） |
| IBL 预滤波 | 实时采样 | 镜面按 mip 近似、漫反射 Fibonacci 采样；如需更接近离线效果，评估预计算 irradiance map |
| 性能路线：后处理降分辨率 | 未实施 | 见 [18 性能优化指南](engineering/16-性能优化指南.md)；参数级、可回滚 |
| 性能路线：主链路 SIMD | 未实施 | 接口级改动，需先立 ADR |
| 压测场景补充 | 持续 | 新增场景用 `SR_SCENARIO()` 登记在 `benchmarks/stress/`；小三角形与包围盒构建成本仍是候选方向 |

## 已关闭（近期）

| 事项 | 结论 |
| --- | --- |
| 跨平台 CI | Windows / ubuntu-24.04 / macOS 三平台 7 个 job 全绿 |
| 测试目录分层与覆盖率接入 | 单元测试拆分完成，覆盖率由 CI coverage job 产出并做棘轮门禁 |
| 全局命名空间收敛 | 类型全部迁入 `sr`，`namespace_sentinel.cpp` 作为门禁 |
| 分层解耦与组件发布 | 公共头迁入 `include/softrenderer/`，组件化 CMake，消费工程验收通过 |
| 像素格式抽象 | RGBA8888 / RGB565 / Gray8，见 [ADR-005](adr/ADR-005-pixel-formats.md) |
| 裸机契约 | 公共头禁 `throw`/RTTI，逐头 `-fno-exceptions` 编译 + 文本级 throw 扫描，见 [ADR-007](adr/ADR-007-baremetal-freestanding.md) |

## 维护规则

- 本页只写**当前**状态与下一步；逐轮过程写进 `changelog.md`，避免页面无限膨胀。
- 任务完成后从「待办」移到「已关闭」并给出结论，设计与验证细节留在 changelog 对应日期条目。
- 旧日志中的测试计数、工具路径、环境阻塞只适用于当时，不能直接复制到本页。
