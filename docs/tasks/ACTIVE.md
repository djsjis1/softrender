# 当前活跃任务

> 本文件是 `ai_workflow_rules.md` Step 0 要求开工前必读的入口。
> 全部任务索引见 [`README.md`](README.md)。

## TASK-003：外部输入校验与边界安全

**快速状态**：待开始（下一个执行目标）

**目标**：消除三类内存安全隐患——`image_loader.h` 有符号整数溢出 UB、
`Framebuffer::getPixel` 在 Release 下的越界读 UB、`blend.h` 混合截断而非四舍五入。

**下一步**：修 `image_loader.h:38` 的 `for (int i = 0; i < w * h; ++i)`

**阻塞项**：无

---

## 已完成：TASK-002 光栅化器缺陷修复

**完成于** 2026-09-05，commit `73122d3`，已 `--no-ff` 合入 `develop`。

修复了两个**被现有测试完全掩盖**的真实缺陷：

1. `rasterizer_fast.h` 深度插值的重心权重错位（同序映射 → 错位映射）
   实测误差：靠近 v0 处期望 0.17917、实际 **0.50417**
2. `setupEdges` 不做符号归一化 → 顺时针三角形被**静默丢弃**
   实测：`fb` 全黑、`zb` 恒为 `1e9`

新增 3 个回归测试，单元测试 94 → **97 全绿**。详见
[`DONE/TASK-002-光栅化器缺陷修复.md`](DONE/TASK-002-光栅化器缺陷修复.md)。

---

## 已完成：TASK-001 工程基线建设（部分）

- git 仓库初始化于 `develop`，首次提交 `e12b16e`（891 文件）
- `.gitignore` + `output/.gitkeep`
- 根 `README.md`、`docs/architecture.md`、`docs/modules.md`、
  `docs/changelog.md`、`docs/troubleshooting.md`、`third/README.md`
- `docs/tasks/` 建档（7 个 TASK）
- `.qoder/rules/engineering_rules.md` 的「红线约束」（11 条）与「命令」两节已填写

**剩余项**：

1. 校正 `docs/engineering/54-项目当前状态总览.md` 的数据漂移
   （含 TASK-002 要求的「SSE2 四像素并行」移至实验性）
2. 删除空目录 `src/platform/`（git 不跟踪空目录，仅本地工作区残留）
3. 处理 `.qoder/rules/1.md`（空的 always_on 规则文件，无意义命名）

---

## 排队中的任务（按依赖顺序）

| 顺序 | 任务 | 关键内容 | 阻塞项 |
|---|---|---|---|
| → | [TASK-003](TASK-003-输入校验与边界安全.md) | `image_loader.h` 整数溢出 UB + Release 越界读 + 混合精度截断 | 无 |
| 4 | [TASK-004](TASK-004-CI跨平台与测试基础设施.md) | CI 在 Linux/macOS 必然编译失败 + 死代码清理 + 像素回归接线 | 需推送远端才能验收 |
| 5 | [TASK-005](TASK-005-构建系统现代化.md) | CMake target-based 改造 + Sanitizer + clang-tidy + 全仓格式化 | **需用户确认是否安装 LLVM 工具链** |
| 6 | [TASK-006](TASK-006-测试目录重构与覆盖率.md) | 拆分 1079 行测试文件 + 覆盖率接入 | 需 TASK-003/004 完成 |
| 7 | [TASK-007](TASK-007-全局命名空间收敛.md) | 60 个全局类型收敛进 `namespace sr` | 需 ADR；**必须最后执行** |

## 最高优先级的待修缺陷

前两项已于 TASK-002 修复（保留作为历史记录）：

1. ~~**`rasterizer_fast.h` 深度插值重心权重错位**~~ → **已修复**（TASK-002）
2. ~~**`rasterizer_fast.h` 顺时针三角形被静默丢弃**~~ → **已修复**（TASK-002）
3. **`image_loader.h:38` 有符号整数溢出 UB**（TASK-003）
   —— `for (int i = 0; i < w * h; ++i)`，加载大图时溢出
4. **CI 在 ubuntu / macos 上 100% 编译失败**（TASK-004）
   —— 三个 realtime 示例无条件使用 `Win32Window`，而该头文件整体包在
   `#ifdef _WIN32` 内。因仓库从未 push，此问题从未暴露
5. **`Framebuffer::getPixel` 在 Release 下越界读 UB**（TASK-003）
   —— 写路径有运行时边界检查，读路径只有 `assert`，保护不对称
