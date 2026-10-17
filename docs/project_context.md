# 项目上下文

> 当前事实入口，更新于 2026-10-06。历史任务中的数字与状态只描述当时情况。
> 架构见 [architecture.md](architecture.md)，当前任务见 [ACTIVE](tasks/ACTIVE.md)。

SoftRenderer 是 C++23 教学型 CPU 渲染器。核心光栅化和数学计算不依赖图形 API；图像、模型和 ECS 依赖仓库内置第三方代码。完整 CMake 目标为头文件接口加资源实现静态库。

## 当前验证快照

| 项目 | 结果 |
| --- | --- |
| Windows 工具链 | VS Build Tools 2022，MSVC 19.44.35228，CMake 3.31.6 |
| 独立工程 Release | 22 个示例 + 1 个练习 + 2 个测试程序，25 个默认可执行目标；另有可选 renderer_benchmark |
| 单元测试 | 169 用例 / 43 套件，Release 全通过 |
| 像素回归 | 3 个场景，全通过，未更新基准图 |
| 源码与安装集成 | 两个独立消费工程均编译并运行通过；安装前缀迁移通过 |
| Debug ASAN | 167 单测与 3 场景像素回归通过；2 项 Release 哨兵测试按约定跳过 |
| Linux/macOS | 已有三平台 CI 配置；本轮未执行远端作业 |
| UBSAN / TSAN / 覆盖率 / clang-tidy | Ubuntu ASAN+UBSAN 与独立 TSAN 作业已补齐；本轮无远端运行和覆盖率数值，无本地静态分析结论 |

## 本轮完成

- TASK-008：按库、示例、练习、测试拆分 CMake；完善构建开关、版本宏、线程依赖和安装闭包；新增消费工程回归。
- TASK-009：discard 片元不再写入深度，补充独立的非对称深度与绘制顺序测试。
- TASK-010：Arena 校验对齐与溢出，RAII 管理块，reset 复用峰值容量；增加 capacity() 诊断。
- TASK-011：共享 accessor/mesh 校验，修复三次样条、未初始化采样输出、纹理二次解码与逆绑定矩阵读取。
- TASK-012：waitAll 错误报告、submitFuture、部分构造失败清理、析构排空和自等待防护。
- TASK-013：统一整数子像素覆盖，修复 MSAA 逐样本提交与边界校验；建立可选的主管线性能基线。
- 文档：统一当前状态、配置选项和依赖说明；纠正自动 reset、纯头文件、平台守卫等过期表述。

## 下一步

下一轮扩充渲染负载与阶段耗时、运行远端质量门禁，随后推进拥有数据的动画缓存和完整场景/skin 求值。命名空间迁移、测试拆分、工具链质量门禁的历史任务继续保留，状态见[ACTIVE](tasks/ACTIVE.md)。

## 常用命令

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release -L "fast|regression" --output-on-failure
ctest --test-dir build -C Release --parallel 4 -LE fast --output-on-failure
```

工程移动目录后使用新构建目录。构建和安装的完整操作见[使用指南](engineering/46-构建与使用指南.md)。
