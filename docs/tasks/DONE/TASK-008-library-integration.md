# TASK-008：构建与安装集成完善

- 日期：2026-10-05
- 状态：已完成
- 范围：CMake、安装包、下游集成测试、README

## 分析与设计

当前 CMAKE_SOURCE_DIR / CMAKE_BINARY_DIR 在 add_subdirectory 中指向父工程；
安装规则漏装 tinygltf/json，EnTT 与版本头的安装搜索路径不一致，版本整数宏模板无效。
示例和测试始终参与配置，给库使用方带来额外目标与依赖。

方案 A：局部修补路径、补全依赖、增加独立构建开关（采用）。保留现有目标与头文件路径。
方案 B：拆分 core / assets / tooling 包。隔离更彻底，但会改变链接接口，留待独立迭代。

## 验收清单

- [x] 使用当前项目根路径，支持作为子目录引入
- [x] 示例、练习、测试可关闭，库传递线程依赖
- [x] 完整安装依赖，统一版本头路径和可重定位的 include 目录
- [x] 独立 consumer 验证 add_subdirectory 与安装后的 find_package
- [x] 全量构建与现有测试通过
- [x] 同步 README、changelog、项目分析报告

不改变渲染模块分层或引入新依赖；构建拓扑图同步 sr_entt 和 Threads::Threads。
新测试放在 tests/integration，验证真实公共头和链接符号。

## 验证结果

2026-10-05：Release 130 单测、3 场景像素回归、2 个消费工程通过；Debug ASAN 下 128 单测通过、2 项约定跳过，像素回归通过。详见 [项目分析](../../project_review.md)。

