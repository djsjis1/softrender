# 当前活跃任务

> 本文件是 `ai_workflow_rules.md` Step 0 要求开工前必读的入口。
> 全部任务索引见 [`README.md`](README.md)。

## TASK-001：工程基线建设

**快速状态**：Step 5 — 编码实现中（文档骨架与版本控制已完成，规则模板位待填写）

**已完成**：

- git 仓库初始化于 `develop` 分支，首次提交 `e12b16e`（891 文件）
- `.gitignore` + `output/.gitkeep`
- 根 `README.md`、`docs/architecture.md`、`docs/modules.md`、
  `docs/changelog.md`、`docs/troubleshooting.md`、`third/README.md`
- `docs/tasks/` 建档（7 个 TASK）

**下一步**：

1. 填写 `.qoder/rules/engineering_rules.md` 的「红线约束」与「命令」两节
   （当前仍是模板占位符，而 `.qoder/rules/README.md` 标注这是「**必须**先填写」项）
2. 校正 `docs/engineering/54-项目当前状态总览.md` 的数据漂移
3. 删除空目录 `src/platform/`

**阻塞项**：无

---

## 排队中的任务（按依赖顺序）

| 顺序 | 任务 | 关键内容 | 阻塞项 |
|---|---|---|---|
| 2 | [TASK-002](TASK-002-光栅化器缺陷修复.md) | `rasterizer_fast.h` 深度插值权重错位 + 顺时针绕序静默丢弃 | 无 |
| 3 | [TASK-003](TASK-003-输入校验与边界安全.md) | `image_loader.h` 整数溢出 UB + Release 越界读 + 混合精度截断 | 无 |
| 4 | [TASK-004](TASK-004-CI跨平台与测试基础设施.md) | CI 在 Linux/macOS 必然编译失败 + 死代码清理 + 像素回归接线 | 需 TASK-001 完成并推送远端 |
| 5 | [TASK-005](TASK-005-构建系统现代化.md) | CMake target-based 改造 + Sanitizer + clang-tidy + 全仓格式化 | **需用户确认是否安装 LLVM 工具链** |
| 6 | [TASK-006](TASK-006-测试目录重构与覆盖率.md) | 拆分 1079 行测试文件 + 覆盖率接入 | 需 TASK-002/003/004 完成 |
| 7 | [TASK-007](TASK-007-全局命名空间收敛.md) | 60 个全局类型收敛进 `namespace sr` | 需 ADR；**必须最后执行** |

## 最高优先级的待修缺陷

按风险排序，前两项是**已确认的真实 bug**：

1. **`rasterizer_fast.h` 深度插值重心权重错位**（TASK-002）
   —— 现有两个测试都用平坦深度 `0.5f/0.5f/0.5f`，缺陷被 100% 掩盖。
   代码第 90 行注释自己写着「(不完全对)」，却无人跟进。
2. **`rasterizer_fast.h` 顺时针三角形被静默丢弃**（TASK-002）
   —— 无日志、无返回值提示，违反「禁止静默失败」。
3. **`image_loader.h:38` 有符号整数溢出 UB**（TASK-003）
   —— `for (int i = 0; i < w * h; ++i)`，加载大图时溢出。
4. **CI 在 ubuntu / macos 上 100% 编译失败**（TASK-004）
   —— 三个 realtime 示例无条件使用 `Win32Window`，而该头文件整体包在
   `#ifdef _WIN32` 内。因仓库从未 push，此问题从未暴露。
5. **`Framebuffer::getPixel` 在 Release 下越界读 UB**（TASK-003）
   —— 写路径有运行时边界检查，读路径只有 `assert`，保护不对称。
