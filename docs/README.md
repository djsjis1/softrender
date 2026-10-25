# SoftRenderer 文档

> 入口在这里：学原理去 `pipeline/`，改工程看 `engineering/`，查"现在到底什么状态"看 `project_context.md`。

## 目录职责

| 路径 | 内容 | 什么时候看 |
| --- | --- | --- |
| [00-学习路线与项目导读.md](00-学习路线与项目导读.md) | 14 个阶段的读码顺序、练习安排、示例对照 | 第一次读项目 |
| [pipeline/](pipeline/) | 渲染管线主干：全景、光栅化、变换、深度、纹理、光照、裁剪 | 从零理解管线 |
| [math/](math/) | 向量、点积、叉积、矩阵、齐次坐标、四元数、插值、反射折射 | 数学卡住时穿插读 |
| [advanced/](advanced/) | MSAA、PBR、后处理、SSAO、FXAA、IBL、骨骼动画、粒子、延迟渲染 | 主管线跑通之后 |
| [engineering/](engineering/) | 测试、CMake、日志、内存、第三方、CI、构建、代码规范 | 想改工程或提 PR |
| [demo/](demo/) | 综合演示程序说明（群星闪耀） | 看 `demo_stars` |
| [adr/](adr/README.md) | 架构决策记录：为什么这样设计、放弃了什么 | 质疑某个设计时 |
| [tasks/](tasks/README.md) | 任务档案、当前面板、历史归档 | 查"这件事做到哪了" |

## 数字只认这三处

用例数、场景数、性能数据在多个文档里出现过，冲突时按此顺序取：

1. [project_context.md](project_context.md) — 当前验证快照（含工具链与未完成项）
2. [tasks/ACTIVE.md](tasks/ACTIVE.md) — 当前进度与遗留任务
3. [changelog.md](changelog.md) — 逐轮变更与当时的实测数字

其余文档提到具体数字时，以上三处为准；教程正文不重复保存计数。

## 三条阅读路线

| 目标 | 路线 |
| --- | --- |
| 从零学渲染 | 学习路线 00 → `math/01~13` → `pipeline/01~09,15,16` → `advanced/11~57` |
| 想改工程 | `engineering/45` 目录结构 → `46` 构建 → `20` 测试 → `27` 像素回归 → `40` CI |
| 想查某项能力怎么实现 | `modules.md` 找模块 → 读 `include/softrenderer/` 对应目录 → 再看 `advanced/` 对应篇 |

## 顶层文档

| 文件 | 内容 |
| --- | --- |
| [architecture.md](architecture.md) | 分层、数据流、不变量与红线 |
| [modules.md](modules.md) | 每个模块的职责、依赖与扩展点 |
| [project_context.md](project_context.md) | 当前状态、验证快照、常用命令 |
| [troubleshooting.md](troubleshooting.md) | 已踩过的坑与排查步骤 |
| [changelog.md](changelog.md) | 按日期的变更记录 |
