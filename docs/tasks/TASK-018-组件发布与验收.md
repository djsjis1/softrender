# TASK-018：组件发布与验收

- 创建日期：2026-10-23
- 状态：待开始
- 父任务：[TASK-014](TASK-014-分层解耦重构.md)

## 目标

按组件导出与安装，完成对外集成验证与 CI / 示例适配，清理旧入口。

## 交付物

| 项 | 内容 |
|---|---|
| 组件目标 | `core`（math/core/resource）、`render`、`scene`、`io`(STATIC)、`runtime`、可选 `entt`；保留 `softrenderer` 聚合目标 |
| 构建开关 | `SR_BUILD_IO`、`SR_BUILD_RUNTIME`、`SR_ENABLE_ENTT`（默认 ON）；全关且关示例/测试时只配置核心库、不查找 Threads |
| 组件查找 | `find_package(SoftRenderer COMPONENTS core)` 只导入依赖闭包；请求未构建组件明确报错 |
| 安装 | 按依赖闭包导出；不安装 tests / benchmarks / 私有实现头与第三方头 |
| 版本 | `1.0.0` → `2.0.0`，版本头仍以 PROJECT_VERSION 为唯一来源 |
| CMake 组织 | 组件定义集中到 `cmake/targets.cmake`；顶层只负责选项与组合 |
| 测试拆分 | `tests/unit/test_core.cpp` 按模块拆分，公共命名空间哨兵独立编译 |
| 消费验证 | 两类外部工程（`add_subdirectory` 与安装后移动前缀 `find_package`）覆盖 core-only / render-only / scene / io / runtime / 聚合 |
| CI | 同步实际 stage 目标名；覆盖率范围改为自有 include 与 src |

## 验收

- 前后核对存量测试名与 25 个 exe 目标名，存量测试全部保留
- 安装产物不含 tests / benchmarks / 私有实现头
- Windows Release 零错误零警告；单测、3 像素回归、packaging 测试通过；19 个离屏示例逐一运行
- Debug ASAN 验证受影响代码；跨平台与远端结论只在真实运行后报告
- 基准图不自动更新

## 边界

不自动提交、推送、发布或触发远端 CI。