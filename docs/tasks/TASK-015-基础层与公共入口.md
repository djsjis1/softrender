# TASK-015：基础层与公共入口

- 创建日期：2026-10-23
- 状态：待开始
- 父任务：[TASK-014](TASK-014-分层解耦重构.md)
- 承接：[TASK-007](TASK-007-全局命名空间收敛.md) 第 3~5 批（进度只在本文件维护）

## 目标

建立唯一正式 include 入口 `include/softrenderer/`，完成 math / core / resource 三层迁移，
并把图像数据从文件读写中拆出。

## 交付物

| 项 | 内容 |
|---|---|
| 目录 | `include/softrenderer/{math,core,resource}/` |
| 图像拆分 | 新增 `core/image.h`（RGBA、行优先、第 0 行在底部）；`tga.h` 只保留 TGA 读写 |
| 纹理解耦 | `Texture` 只使用 `sr::Image`，移除文件加载成员；调用方经 io 加载后构造 |
| 资源归位 | 顶点、网格、纹理、Cubemap、材质与程序化生成归 `resource/` |
| 命名空间 | 公共类归 `sr`；`colors` / `transform` / `meshgen` / `texgen` 并入 `sr` 下的具名子命名空间 |
| 基类拆分 | 提取 `resource/vertex.h`、`render/pipeline/varyings.h`（字段与插值语义不变） |
| 线程设施 | `command_buffer.h` 的 `ThreadPool` 与 `CommandBuffer` 分文件 |
| 基准框架 | `core/benchmark.h` 迁往 benchmarks 支持目录 |

## 验收

- 全量构建零错误零警告；`unit_tests`、`render_regression` 结果与阶段 1 基线一致
- 公共头逐个独立编译通过
- 新增行为测试：无文件 I/O 的 `Image → Texture → 绘制` 可编译运行
- 旧路径与旧名称不保留转发或别名

## 退出条件

三层目录与命名空间稳定，`include/softrenderer/{math,core,resource}` 可被单独包含使用。

## 进度

### 2026-10-23 第 1 批：公共入口与基础层搬迁（已完成）

已搬迁 23 个头文件，改写 66 个文件中的 169 处 include 引用：

| 新位置 | 文件数 | 来源 |
|---|---|---|
| `include/softrenderer/math/` | 7 | `src/math/` |
| `include/softrenderer/core/` | 9 | `src/core/` 纯计算与容器 |
| `include/softrenderer/io/` | 2 | `src/core/tga.h`、`src/core/image_write.h` |
| `include/softrenderer/runtime/` | 4 | `src/core/log.h`、`profiler.h`、`command_buffer.h`、`resource_manager.h` |
| `include/softrenderer/render/postprocess/` | 1 | `src/core/post_processing.h` |

CMake：`softrenderer` 增加 `include/` 构建接口目录；`cmake/install.cmake` 安装 `include/`，
并保留 `src/` 的过渡段。迁移期间 `src/` 仍在 include 路径上，供尚未迁移的头使用。

验证：全量构建 0 错误 0 警告；`unit_tests` 169/169；`render_regression` 3/3；
`stage01_framebuffer` / `stage11_msaa` / `demo_skinning` 退出码 0。文档已同步 `architecture.md`、`modules.md`。

### 待做

- [ ] Image 拆分（`core/image.h`）与 `Texture` 去文件加载
- [ ] 命名空间统一（`colors` / `transform` / `meshgen` / `texgen` 并入 `sr`）
- [ ] 提取 `resource/vertex.h` 与 `render/pipeline/varyings.h`
- [ ] `ThreadPool` 与 `CommandBuffer` 分文件；`benchmark.h` 迁往 benchmarks 支持目录
- [ ] 迁移 `src/raster` / `src/scene` / `src/shader` / `src/test` 到各自最终位置
- [ ] 公共头逐个独立编译测试；无文件 I/O 的 Image→Texture→绘制测试
- [ ] 受影响教程与 `docs/engineering/45-项目文件结构.md` 的路径同步