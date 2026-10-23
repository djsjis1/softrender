# 当前活跃任务

> 更新于 2026-10-23。当前验证数值见 [项目上下文](../project_context.md)。
> 本页只描述当前进度；旧任务文件的早期分析不能当作当前阻塞项。

## 任务总览

| 编号 | 状态 | 内容 | 档案 |
| --- | --- | --- | --- |
| TASK-001 | 基础设施已落地 | 工程基线、Git 与文档 | [任务](TASK-001-工程基线建设.md) |
| TASK-002 | 已完成 | 快速光栅化深度权重与顺时针修复 | [档案](DONE/TASK-002-光栅化器缺陷修复.md) |
| TASK-003 | 已完成 | 边界安全、图像输入、混合和日志 | [档案](DONE/TASK-003-输入校验与边界安全.md) |
| TASK-004 | 本地完成，远端待验收 | 跨平台目标守卫与像素回归 | [任务](TASK-004-CI跨平台与测试基础设施.md) |
| TASK-005 | 部分完成 | CMake、Sanitizer、零警告已落地；静态分析待实跑 | [任务](TASK-005-构建系统现代化.md) |
| TASK-006 | 已完成 | 测试目录分层 + 	est_core.cpp 按模块拆成 10 个文件；覆盖率待工具链 | [任务](TASK-006-测试目录重构与覆盖率.md) |
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

## 2026-10-23 阶段 2–5（分层解耦）

- 公共头迁入 `include/softrenderer/`，`src/` 只留 `src/io/` 私有实现；功能命名空间收敛到 `sr::*`。
- 缓冲/效果、Shader/Varyings/Vertex、2D 与光栅化边界、相机 2D/3D、程序化生成全部按职责拆开。
- glTF/OBJ/TGA/图像加载改为 `sr::io` + STATIC 组件，第三方解析器仅在实现单元可见；glTF 结果改为自有 `SceneAsset`，动画在加载阶段解码。
- CMake 组件化（core/render/scene/io/runtime/可选 entt）+ `SR_BUILD_IO` / `SR_BUILD_RUNTIME` / `SR_ENABLE_ENTT`；版本 2.0.0。
- 实测：构建 0/0；179/179 单测；3/3 像素回归；20 个离屏示例退出码 0；25 个可执行目标；packaging 2/2；core-only 配置与构建通过。
- 已补齐：全局类名迁移（TASK-007）、`test_core.cpp` 按模块拆分（TASK-006）。仍待真实运行：覆盖率实测、macOS/Linux 远端结果。

## 2026-10-23 阶段 5 收尾（全局类名 + 测试拆分）

- **命名空间收敛完成**：27 个仍含全局声明的公共头包进 `namespace sr`；`Color` 与
  `sr_log_init` 一并收敛；`namespace raster_fast` 改为 `sr::raster`。应用侧统一
  `using namespace sr;`，库头不用 using-directive。
- **门禁升级**：`namespace_sentinel.cpp` 在全局声明 60 个同名类型后包含全部公共头，
  并用 `static_assert` 锁定 `sr::Framebuffer`/`sr::Mesh`/`sr::Texture`。
- **测试拆分**：`tests/unit/test_core.cpp`（1468 行）拆成 10 个模块级文件，
  `tests/CMakeLists.txt` 显式列出源文件；ECS 哨兵改入 `namespace ecsCanary`。
- **外部消费**：新增 render-only 消费工程（`tests/integration/consumer_render`），
  `run_consumer.cmake` 同时验证聚合 / core-only / render-only 三类工程在
  `add_subdirectory` 与安装后移动前缀再 `find_package` 两种模式下可用，并断言安装产物
  只含公共头与组件。
- **实测**：构建 0/0；`unit_tests` 179/179；`render_regression` 3/3；`ctest -LE fast`
  175/175；20 个离屏示例退出码 0；25 个可执行目标；`ctest -L packaging` 3/3。
- **未完成**：覆盖率本地实测、macOS/Linux 远端 CI（需要真实运行，不在本轮宣称通过）。
## 2026-10-23 已完成

[TASK-014](TASK-014-分层解耦重构.md) 阶段 1：实测重构前基线，修复三项既有缺陷。

- 基线（develop @ 2adfbf6）：全量构建失败（3 个示例目标）；169 单测 166 通过 3 失败；3 像素回归通过。
- 修复：`Renderer` 共用 `sr::TriangleCoverage` 覆盖规则；深度改在片元接受后提交；示例补齐 `draw2d::` 限定。
- 结果：构建 0 错误 0 警告；169/169 单测；3/3 像素回归；20 个离屏程序退出码 0。基准图未更新。

## 2026-10-06 已完成

[TASK-013](DONE/TASK-013-raster-coverage.md)：统一覆盖规则、修复 MSAA 颜色提交、建立主渲染基线；本地验证完成。

## 待继续的工程任务

| 任务 | 当前状态 | 下一步与验收 |
| --- | --- | --- |
| TASK-004 跨平台 CI | 本地验证与 CI 配置已有；远端待验收 | 执行 Windows/Linux/macOS 作业，记录真实结果 |
| TASK-005 构建现代化 | target-based、/WX、安装集成、Debug ASAN 已落地；静态分析未实跑 | 本轮修改文件已格式化；继续运行 clang-tidy 并处理实际诊断 |
| TASK-006 测试拆分/覆盖率 | 拆分完成；覆盖率未实测 | 接入 llvm-cov/gcov 后取基线 |
| TASK-007 命名空间 | 已完成（无别名桥） | 维护哨兵即可 |
| TASK-014 分层解耦重构 | 阶段 1–5 完成 | 覆盖率与远端 CI 真实运行后结案 |

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
