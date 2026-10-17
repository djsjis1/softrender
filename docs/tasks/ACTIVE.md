# 当前活跃任务

> 更新于 2026-10-06。当前验证数值见 [项目上下文](../project_context.md)。
> 本页只描述当前进度；旧任务文件的早期分析不能当作当前阻塞项。

## 任务总览

| 编号 | 状态 | 内容 | 档案 |
| --- | --- | --- | --- |
| TASK-001 | 基础设施已落地 | 工程基线、Git 与文档 | [任务](TASK-001-工程基线建设.md) |
| TASK-002 | 已完成 | 快速光栅化深度权重与顺时针修复 | [档案](DONE/TASK-002-光栅化器缺陷修复.md) |
| TASK-003 | 已完成 | 边界安全、图像输入、混合和日志 | [档案](DONE/TASK-003-输入校验与边界安全.md) |
| TASK-004 | 本地完成，远端待验收 | 跨平台目标守卫与像素回归 | [任务](TASK-004-CI跨平台与测试基础设施.md) |
| TASK-005 | 部分完成 | CMake、Sanitizer、零警告已落地；静态分析待实跑 | [任务](TASK-005-构建系统现代化.md) |
| TASK-006 | 部分完成 | 目录分层完成；旧单测继续拆分，覆盖率待实测 | [任务](TASK-006-测试目录重构与覆盖率.md) |
| TASK-007 | 第 1、2 批完成 | 命名空间迁移与编译哨兵 | [任务](TASK-007-全局命名空间收敛.md) |
| TASK-008 | 已完成 | 构建职责拆分、安装和真实消费工程验证 | [档案](DONE/TASK-008-library-integration.md) |
| TASK-009 | 已完成 | discard 片元的深度提交修复 | [档案](DONE/TASK-009-render-discard.md) |
| TASK-010 | 已完成 | Arena 安全校验与跨帧复用 | [档案](DONE/TASK-010-arena-safety.md) |
| TASK-011 | 已完成 | glTF/动画数据校验与正确插值 | [档案](DONE/TASK-011-gltf-validation.md) |
| TASK-012 | 已完成 | 任务异常与生命周期 | [档案](DONE/TASK-012-thread-failures.md) |
| TASK-013 | 已完成 | 子像素覆盖、MSAA 逐样本提交、主管线性能基线 | [档案](DONE/TASK-013-raster-coverage.md) |

## 2026-10-06 已完成

[TASK-013](DONE/TASK-013-raster-coverage.md)：统一覆盖规则、修复 MSAA 颜色提交、建立主渲染基线；本地验证完成。

## 待继续的工程任务

| 任务 | 当前状态 | 下一步与验收 |
| --- | --- | --- |
| TASK-004 跨平台 CI | 本地验证与 CI 配置已有；远端待验收 | 执行 Windows/Linux/macOS 作业，记录真实结果 |
| TASK-005 构建现代化 | target-based、/WX、安装集成、Debug ASAN 已落地；静态分析未实跑 | 本轮修改文件已格式化；继续运行 clang-tidy 并处理实际诊断 |
| TASK-006 测试拆分/覆盖率 | unit/integration/fixtures 分层和 tests/CMakeLists 已完成；Arena/Renderer 已独立 | 继续拆分旧 test_core.cpp 的依赖，保持用例集合；取得覆盖率基线 |
| TASK-007 命名空间 | 第 1、2 批完成，公共全局符号仍存在 | 按 ADR-001 分批迁移，保留迁移说明与编译哨兵 |

当前没有"必须先安装 LLVM 才能修改构建系统"或"删除工具不可用所以不能整理测试"的已验证阻塞。历史环境问题只保留在对应任务档案中。

## 2026-10-05 已完成

- [TASK-008](DONE/TASK-008-library-integration.md)：构建拆分、可选目标、安装闭包、版本和消费工程验证。
- [TASK-009](DONE/TASK-009-render-discard.md)：discard 深度提交修复与非对称测试。
- [TASK-010](DONE/TASK-010-arena-safety.md)：分配边界安全、RAII 与跨帧复用。

- [TASK-011](DONE/TASK-011-gltf-validation.md)：glTF/动画共享读取、三次样条与解码纹理修复。
- [TASK-012](DONE/TASK-012-thread-failures.md)：线程任务错误结果与生命周期；远端 TSAN 归 TASK-004 验收。

## 建议下一轮实现顺序

1. 扩充代表性渲染负载，建立阶段耗时和交错重复 A/B 比较。
2. 运行远端 Sanitizer/覆盖率，并处理静态分析诊断。
3. 动画预编译缓存、完整节点层级和 skin 导入。

## 维护规则

- 已完成工作在 `DONE/` 留下设计、测试和结果，功能仍有剩余工作时保留任务状态。
- 旧日志中的测试计数、工具路径、环境阻塞只适用于当时；不能直接复制到当前仪表盘。
- 命名空间大规模迁移与第三方升级单独实施，以便区分接口和行为变化。
- 每轮实现先补充缺陷回归或集成验收，再记录验证范围，不以文档存在代替实际验收。
