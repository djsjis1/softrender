# 项目上下文

> 当前事实入口，更新于 2026-10-25（全仓缺陷修复与压测扩展完成）。历史任务中的数字与状态只描述当时情况。
> 架构见 [architecture.md](architecture.md)，当前任务见 [ACTIVE](tasks/ACTIVE.md)。

SoftRenderer 是 C++23 教学型 CPU 渲染器，同时作为可被外部工程复用的渲染库发布（2.0.0）。
公共头唯一入口是 `include/softrenderer/`，全部符号位于 `sr`；文件加载实现集中在 `src/io/` 的
`softrenderer_io` 静态库，第三方实现宏只在 `stb_impl.cpp` / `tinygltf_impl.cpp` / `tinyobj_impl.cpp`
三处定义一次。CMake 按组件导出：`core` / `render` / `scene` / `io` / `runtime` / 可选 `entt`
加聚合目标 `softrenderer`。

## 当前验证快照

| 项目 | 结果 |
| --- | --- |
| Windows 工具链 | VS Build Tools 2022，MSVC 19.44，CMake 3.31.6 |
| 独立工程 Release | 20 个离屏示例 + 3 个窗口示例 + 1 个练习 + 2 个测试程序 = 25 个默认可执行目标；另有两个可选基准/压测目标（`SR_BUILD_BENCHMARKS=ON`） |
| 单元测试 | 262 用例 / 60 套件，Release 全通过（全量构建 0 错误 0 警告） |
| 集成测试 | 3 场景像素回归通过（PNG 基准图，比较含 alpha）；下游消费工程 2 项通过 |
| 压测探针 | `renderer_stress` 105 个场景（敌意输入/并发/性能）全通过；Release 全量约 8.2 s、`--quick` 85 个约 1.8 s；Debug+ASAN 同样干净 |
| 架构门禁 | 逐头独立编译 + 命名空间哨兵（全局声明 60 个同名类型）+ include 白名单 + 循环依赖检查 |
| 外部消费 | 聚合 / core-only / render-only 三类工程 × `add_subdirectory` 与安装后迁移 `find_package` 均通过；断言安装产物只含公共头 |
| Debug ASAN | 单元测试与压测探针在 Debug+ASAN 下无断言、无 ASAN 报告（Debug 下两个越界用例按约定 GTEST_SKIP） |
| Linux/macOS | 三平台 CI 配置已同步（stage 目标名、覆盖率范围 `include/softrenderer` + `src`）；本轮未执行远端作业 |
| 覆盖率 / clang-tidy | 本机无 gcov/lcov/clang 工具链（已实测）；无覆盖率数值与静态分析结论 |

## 2026-10-24 缺陷修复与压测扩展

- 修复 12 类真实缺陷：空纹理越界、OBJ/glTF 越界索引、TGA 尺寸放大分配、AlphaBlend alpha、
  球体绕序、PBR 反射、FXAA 无输出、透明排序、MSAA 大三角形丢弃、Profiler 开关失效、
  2D 图集串色、退化向量断言。
- 并发：`ResourceCache` 改 64 分片锁 single-flight，同键首次加载重复构建 452 → 1。
- 性能：MSAA 4x 364→110 ms、全屏 blit 335→94 ms、Profiler flush 946→368 ms、
  SSAO 727→589 ms（半分辨率 218 ms）、FXAA 213→156 ms、PBR 1024²×30 253→186 ms、
  文字 5k 字形 78→37 ms、OBJ 50k 面 190→158 ms。
- 压测：注册表驱动（`--filter` / `--quick` / `--json` / `--baseline` / `--strict-baseline`），场景 49 → 105。
- 压测代码分层：`stress_main.cpp` 只留 CLI，场景在 `benchmarks/stress/` 下按文件分编译，
  `SR_SCENARIO()` 静态登记，新增场景不再修改注册表。

## 2026-10-23 分层解耦（TASK-014~018）

- TASK-014~018（阶段 1–5）：公共头迁入 `include/softrenderer/`，`src/` 只剩 `src/io/` 私有实现；
  Image 与纹理/文件读写解耦、后处理效果外移、Shader/Varyings/SurfaceShader 拆分、相机 2D/3D 分头、
  生成器独立头、Renderer 去宿主计时；场景改为自有 `SceneAsset`，动画在加载期解码；I/O 结果返回状态与错误文本。
- TASK-007：60 个全局类型全部迁入 `sr`（含 `Color`、`raster_fast` → `sr::raster`），不保留别名桥与旧头转发。
- TASK-006：`test_core.cpp`（1468 行）拆成按模块分组的 10 个文件；命名空间哨兵独立编译。
- 组件化 CMake：`SR_BUILD_IO` / `SR_BUILD_RUNTIME` / `SR_ENABLE_ENTT`；`find_package(... COMPONENTS core)`
  不查找 Threads；版本 2.0.0。
- 第三方实现单元统一到 `src/io/{stb,tinygltf,tinyobj}_impl.cpp`，文件名即实现的库。

## 下一步

覆盖率实测（llvm-cov/gcov 任一）、三平台远端作业、clang-tidy 基线；渲染能力上阴影贴图与
IBL 预计算尚未收敛进库。未完成的验收依赖真实运行，不得据文档宣称通过。

## 常用命令

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release -L "fast|regression" --output-on-failure
ctest --test-dir build -C Release --parallel 4 -LE fast --output-on-failure
```

工程移动目录后使用新构建目录。构建和安装的完整操作见[使用指南](engineering/46-构建与使用指南.md)。
