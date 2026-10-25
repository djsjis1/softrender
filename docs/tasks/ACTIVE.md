# 当前活跃任务

> 更新于 2026-10-25。当前验证数值见 [项目上下文](../project_context.md)。
> 本页只描述当前进度；旧任务文件的早期分析不能当作当前阻塞项。
> 逐轮细节见 [变更日志](../changelog.md)，本页只保留结论。

## 任务总览

| 编号 | 状态 | 内容 | 档案 |
| --- | --- | --- | --- |
| TASK-001 | 基础设施已落地 | 工程基线、Git 与文档 | [任务](TASK-001-工程基线建设.md) |
| TASK-002 | 已完成 | 快速光栅化深度权重与顺时针修复 | [档案](DONE/TASK-002-光栅化器缺陷修复.md) |
| TASK-003 | 已完成 | 边界安全、图像输入、混合和日志 | [档案](DONE/TASK-003-输入校验与边界安全.md) |
| TASK-004 | 本地完成，远端待验收 | 跨平台目标守卫与像素回归 | [任务](TASK-004-CI跨平台与测试基础设施.md) |
| TASK-005 | 已完成（clang-format 未跑） | CMake、Sanitizer、零警告、clang-tidy 全仓扫描已落地 | [任务](TASK-005-构建系统现代化.md) |
| TASK-006 | 已完成 | 测试目录分层 + `test_core.cpp` 拆成 10 个文件；覆盖率待工具链 | [任务](TASK-006-测试目录重构与覆盖率.md) |
| TASK-007 | 已完成 | 60 个全局类型全部迁入 sr，无别名桥；命名空间哨兵独立编译 | [任务](TASK-007-全局命名空间收敛.md) |
| TASK-008 | 已完成 | 构建职责拆分、安装和真实消费工程验证 | [档案](DONE/TASK-008-library-integration.md) |
| TASK-009 | 已完成 | discard 片元的深度提交修复 | [档案](DONE/TASK-009-render-discard.md) |
| TASK-010 | 已完成 | Arena 安全校验与跨帧复用 | [档案](DONE/TASK-010-arena-safety.md) |
| TASK-011 | 已完成 | glTF/动画数据校验与正确插值 | [档案](DONE/TASK-011-gltf-validation.md) |
| TASK-012 | 已完成 | 任务异常与生命周期 | [档案](DONE/TASK-012-thread-failures.md) |
| TASK-013 | 已完成 | 子像素覆盖、MSAA 逐样本提交、主管线性能基线 | [档案](DONE/TASK-013-raster-coverage.md) |
| TASK-014 | 已完成（覆盖率/远端 CI 待真实运行） | 分层解耦重构 | [任务](TASK-014-分层解耦重构.md) |
| TASK-015 | 已完成 | 基础层与公共入口 | [任务](TASK-015-基础层与公共入口.md) |
| TASK-016 | 已完成 | 渲染职责拆分 | [任务](TASK-016-渲染职责拆分.md) |
| TASK-017 | 已完成 | 场景与加载解耦 | [任务](TASK-017-场景与加载解耦.md) |
| TASK-018 | 已完成（覆盖率实测与远端 CI 待真实运行） | 组件发布与验收 | [任务](TASK-018-组件发布与验收.md) |
| TASK-019 | 已完成 | 存储像素格式抽象（ADR-005）：RGBA8888 / RGB565 / Gray8 | [任务](TASK-019-像素格式抽象.md) |

## 2026-10-24 压测与缺陷修复（十～二十一轮汇总）

压测程序改为注册表驱动（`--filter` / `--quick` / `--json` / `--baseline` / `--strict-baseline`），
场景 49 → **105**（`--quick` 85 个 / 1.8 s）。2026-10-25 起场景源码按文件分在 `benchmarks/stress/`，`SR_SCENARIO()` 静态登记。

**修复的真实缺陷**

| 缺陷 | 后果 |
| --- | --- |
| `ResourceCache` 并发首次加载重复构建 | 同一资源被构建 452 次（64 分片锁 single-flight 后为 1） |
| MSAA 超大三角形整块丢失 | 视口外包围盒未做 2D 裁剪 |
| TGA 头部尺寸放大分配 | 损坏文件可触发超大分配（后加字节校验） |
| `Profiler::setEnabled(false)` 无效 | 关闭后仍记录百万事件；默认改为关闭 |
| 零法线 / NaN 单位化 | Debug 构建断言（`normalizedOr` 兜底） |
| 2D 贴图 Repeat 寻址 | 图集边缘串色 37.5%（2D 改 ClampToEdge） |

**性能（1080p 或单场景实测）**

| 路径 | 变化 |
| --- | --- |
| MSAA 4x 光栅 | 364 → 110 ms（样本增量 + 组合写入） |
| 全屏 blit | 335 → 94 ms（`bilinearSpan` / 整数 LOD） |
| Profiler flush | 946 → 368 ms（手写数字 + 整数微秒） |
| SSAO | 727 → 589 ms；半分辨率 218 ms |
| FXAA | 213 → 156 ms（亮度图 + 延迟角落） |
| PBR 1024²×30 | 253 → 186 ms（`pow5` 替代 `std::pow`） |
| 文字 5k 字形 | 78 → 37 ms（轴对齐字符级路径） |
| OBJ 50k 面 | 190 → 158 ms（预留容量） |

**验证（Release）**：构建 0 错误 0 警告；`unit_tests` **281/281**；像素回归 **3/3**；
压测 **105/105**；19 个离屏示例加 1 个练习共 **20 个离屏程序**退出码 0。Debug + ASAN 全量 **105/105** 无断言无越界（171 s）。

**未完成**：覆盖率基线需可用的插桩工具链（BuildTools 无 `CodeCoverage.exe`；`dotnet-coverage` 对原生进程探查器不初始化；LLVM 组件仅含 clang-tidy/format）。
**已补充**：clang-tidy 19.1.5（VS BuildTools 自带）已扫全部 65 个公共头，139 条诊断→ 1 项真缺陷已修；窗口示例已用桌面自动化验证（WASD 引起 61.7% 像素变化）。

## 2026-10-23 分层解耦（阶段 1–5）

- 公共头迁入 `include/softrenderer/`，`src/` 只留 `src/io/` 私有实现；功能符号收敛到 `sr::*`。
- glTF/OBJ/TGA/图像加载改为 `sr::io` + STATIC 组件，第三方解析器仅在实现单元可见。
- CMake 组件化（core/render/scene/io/runtime + 可选 entt），版本 2.0.0。
- 门禁：`namespace_sentinel.cpp` 全局声明 60 个同名类型后包含全部公共头。
- 测试拆分：`test_core.cpp`（1468 行）拆为 10 个模块级文件。
- 消费验收：聚合 / core-only / render-only 三类工程在 `add_subdirectory` 与安装后 `find_package` 下均可用。
- 阶段 1 实测基线（develop @ 2adfbf6）：构建失败 3 示例、169 单测 3 失败；修复后 169/169、3/3 回归。

## 历史归档

- 2026-10-06 [TASK-013](DONE/TASK-013-raster-coverage.md)：统一覆盖规则、修复 MSAA 颜色提交、建立主渲染基线。
- 2026-10-05 [TASK-008](DONE/TASK-008-library-integration.md)：构建拆分、可选目标、安装闭包与消费工程。
- 2026-10-05 [TASK-009](DONE/TASK-009-render-discard.md)：discard 深度提交修复与非对称测试。
- 2026-10-05 [TASK-010](DONE/TASK-010-arena-safety.md)：分配边界安全、RAII 与跨帧复用。
- 2026-10-05 [TASK-011](DONE/TASK-011-gltf-validation.md)：glTF/动画共享读取、三次样条与解码纹理修复。
- 2026-10-05 [TASK-012](DONE/TASK-012-thread-failures.md)：线程任务错误结果与生命周期。

## 待继续的工程任务

| 任务 | 当前状态 | 下一步与验收 |
| --- | --- | --- |
| TASK-004 跨平台 CI | 本地验证与 CI 配置已有；远端待验收 | 执行 Windows/Linux/macOS 作业，记录真实结果 |
| TASK-005 构建现代化 | target-based、`/WX`、安装集成、Debug ASAN、clang-tidy 全仓扫描均已完成 | 仅剩 clang-format 全仓格式化（需在所有功能修改之后单独提交） |
| TASK-006 覆盖率 | 拆分完成；覆盖率未实测 | 接入 llvm-cov/gcov 后取基线 |
| 压测遗留 | 小三角形光栅 3.0 Mtris/s；OBJ 50k 面 155 ms | 包围盒建立开销、tinyobj 替换评估 |
| 性能路线 | 后处理占 1080p 整帧 81%（157/193 ms） | 两条路线见 [性能优化指南](../engineering/48-性能优化指南.md) §4：后处理降分辨率（参数级、可回滚）与主链路 SIMD（接口级、需先立 ADR）；均未实施 |
| 渲染能力 | 阴影贴图仍在示例侧；IBL 仅实时卷积 | 收敛进库需先定公共 API |

## 维护规则

- 已完成工作在 `DONE/` 留下设计、测试和结果，功能仍有剩余工作时保留任务状态。
- 本页只写结论与当前数值；逐轮过程写进 `changelog.md`，避免页面无限膨胀。
- 旧日志中的测试计数、工具路径、环境阻塞只适用于当时；不能直接复制到当前仪表盘。
- 命名空间大规模迁移与第三方升级单独实施，以便区分接口和行为变化。
- 每轮实现先补充缺陷回归或集成验收，再记录验证范围，不以文档存在代替实际验收。
