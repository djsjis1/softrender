# TASK-002：光栅化器缺陷修复

## 基本信息

- 创建日期：2026-09-05
- 完成日期：2026-09-05
- 状态：**已完成**（commit `73122d3`，合并于 `develop`）
- 涉及模块：`src/raster/rasterizer_fast.h`、`tests/`、`docs/engineering/54`
- 变更级别：**M**（单模块，但涉及正确性缺陷 + 测试补强）

## 目标

修复 `raster_fast` 光栅化器中两个**已确认的真实缺陷**，并补上能真正抓到它们的测试。

## 缺陷一：深度插值的重心权重错位（严重）

### 根因

边函数与重心坐标是**错位映射**，不是同序映射。

`setupEdges`（`rasterizer_fast.h:47-74`）计算的三个边函数：

| 边函数 | 定义 | 对应的重心权重 |
|---|---|---|
| `e0` | `cross2(v1-v0, p-v0)`，边 v0→v1 | **v2** 的权重 |
| `e1` | `cross2(v2-v1, p-v1)`，边 v1→v2 | **v0** 的权重 |
| `e2` | `cross2(v0-v2, p-v2)`，边 v2→v0 | **v1** 的权重 |

所以正确公式是：

```
z      = (e1*z0 + e2*z1 + e0*z2) * invArea
dz/dx  = (de1_dx*z0 + de2_dx*z1 + de0_dx*z2) * invArea
dz/dy  = (de1_dy*z0 + de2_dy*z1 + de0_dy*z2) * invArea
```

### 正确性基准

`src/raster/renderer.h` 是**正确实现**，可逐行对照：

- 第 167 行：`Vec3f bc{e1_x * invArea, e2_x * invArea, e0_x * invArea};`
  （`bc.x` 是 v0 的权重，取自 `e1`）
- 第 142 行：`z_row = (e1 * s0.z + e2 * s1.z + e0 * s2.z) * invArea;`
- 第 135-136 行：`dz_dx = (de1_dx * s0.z + de2_dx * s1.z + de0_dx * s2.z) * invArea;`

### 错误位置

| 文件 | 行 | 当前（错误） |
|---|---|---|
| `rasterizer_fast.h` | 93-94 | `dz_dx = (es.de0_dx * z0 + es.de1_dx * z1 + es.de2_dx * z2) * es.invArea;` |
| `rasterizer_fast.h` | 97 | `z_row = (es.e0 * z0 + es.e1 * z1 + es.e2 * z2) * es.invArea;` |
| `rasterizer_fast.h` | 136-137 | SSE2 版同样的错误 |
| `rasterizer_fast.h` | 149 | SSE2 版 `z_row` 同样的错误 |

代码第 90 行的注释**自己已经承认**：
`// z = bc.x*z0 + ... = (e0*z0 + e1*z1 + e2*z2) * invArea (不完全对)`

这是一条留下却无人跟进的已知缺陷标记，违反 `engineering_rules.md`
「不留无跟进计划的 TODO / FIXME」。

### 为什么现有测试完全没抓到

`tests/test_core.cpp` 的两个 `RasterizerFast` 用例传入的深度**三个顶点全相等**：

| 用例 | 行 | 传入深度 |
|---|---|---|
| `RasterizerFast.IncrementalFillsTriangle` | 562 | `0.5f, 0.5f, 0.5f` |
| `RasterizerFast.SSE2MatchesScalar` | 577 / 579 / 581 | `0.5f, 0.5f, 0.5f` |

`z0 == z1 == z2` 时，**任何权重排列都算出同一个值** → 缺陷被 100% 掩盖。

这正是 `testing_rules.md`「禁止先写实现再『凑』出让它通过的测试」要防的情况。

## 缺陷二：不处理顺时针绕序（严重）

### 根因

`setupEdges` 第 73 行返回 `invArea = 1.0f / area2`。当三角形为顺时针（`area2 < 0`）时：
- `invArea` 为负
- `e0` / `e1` / `e2` 未翻转符号

于是第 105 行的内点判定 `e0 >= 0 && e1 >= 0 && e2 >= 0` **永不成立** →
CW 三角形被**静默丢弃**（无日志、无返回值提示）。

违反 `engineering_rules.md`「错误必须处理：禁止空 catch、静默失败」。

### 对照

`renderer.h:117-125` 有完整处理：

```cpp
float sign = 1.0f;
if (area2 < 0) { sign = -1.0f; e0 = -e0; e1 = -e1; e2 = -e2; }
```

并把 `sign` 乘进各增量（第 128-130 行），`invArea` 取 `1.0f / std::abs(area2)`（第 134 行）。

## 当前状态

两个缺陷均已修复并经测试验证。单元测试由 94 增至 **97**，全量通过（5 ms）；
全量构建零错误零警告。

**实测证据**（修复前）：

| 缺陷 | 采样点 | 期望值 | 修复前实际值 | 误差 |
|---|---|---|---|---|
| 权重错位 | 靠近 v0（w0=0.8646, w1=0.0729, w2=0.0625） | 0.17917 | **0.50417** | 0.325 |
| 权重错位 | 靠近 v1 | 0.49583 | **0.82083** | 0.325 |
| 权重错位 | 靠近 v2 | 0.82083 | **0.19583** | 0.625 |
| CW 绕序 | 全图 | 与 CCW 逐像素一致 | `fb` 全黑、`zb` 恒为 `1e9`（kFar） | 三角形完全丢弃 |

事前推算的错位值 0.50415 与实测 0.50417 吻合到小数点后 5 位，
确认根因分析与修复方向正确。

> 附带确认：`fillTriangleSSE2_slopeDepthMatchesScalar` 在修复前就**通过**——
> 因为标量版与 SSE2 版犯了同样的权重错位，两版彼此一致。该测试只验证
> 「SSE2 == 标量」，正确性必须由重心坐标测试保证。这说明**一致性测试
> 不能替代正确性测试**。

## 进度清单

- [x] **先写测试**：`fillTriangleIncremental_slopeDepthMatchesBarycentric`
      —— 三顶点深度取 `0.1f / 0.5f / 0.9f`，采样点取靠近各顶点处
- [x] **确认新测试 FAIL**（实测 4 个采样点全部失败，误差 0.025~0.625）
- [x] 修 `rasterizer_fast.h` 标量版 `dz_dx` / `dz_dy` / `z_row` 的权重映射
- [x] 修 SSE2 版同样三处
- [x] 删除自我怀疑的「(不完全对)」注释，替换为解释「为什么错位」的注释
      （含 e_i 度量「p 离边 i 有多远」的几何直觉）
- [x] **确认新测试 PASS**
- [x] 在 `setupEdges` 内部做符号归一化（翻转 3 个 e 值 + 6 个增量，
      `invArea` 取 `1.0f / std::abs(area2)`）
- [x] 新增测试 `fillTriangleFast_clockwiseWinding_stillRasterizes`
- [x] 新增 `fillTriangleSSE2_slopeDepthMatchesScalar`（非平坦深度 + 补深度断言）
- [x] 跑全量测试确认无回归：94 → **97 全绿**
- [x] 改写 `rasterizer_fast.h` 文件头注释，标注实验性定位与两个不接入原因
- [x] 归档到 `docs/tasks/DONE/`
- [ ] 修 `docs/engineering/54-项目当前状态总览.md:35`（移入 TASK-001 的文档校正批次）

### 实现中发现的细节

几何重心 `(16,12)` 是**对称盲点**：该处 `w0=w1=w2=1/3`，正确映射与错位映射
都得到 `(0.1+0.5+0.9)/3 = 0.5`。但像素中心 `(16.5,12.5)` 并不精确落在几何重心上
（实测 w0=0.302, w1=0.344, w2=0.354），因此仍有 0.025 的可检测差异——
比顶点采样点小一个数量级。**这印证了「采样点必须取非对称位置」的必要性**：
若容差放宽到 0.05，重心采样点就会漏检。

期望值用面积法独立计算（测试内新增 `barycentricByArea` 辅助函数），
**不复用被测代码的边函数**——否则权重错位时期望值与实际值会一起错，
测试完全失去发现能力。

## 设计决策：`raster_fast` 不接入主渲染链路

**结论**：保持现状（标注为实验性 / 基准测试用），**不**接入 `Renderer`。

**理由**：

1. **像素写入路径反而更慢**：`rasterizer_fast.h` 逐像素调用
   `zb.testAndSet(x,y,z)` + `fb.setPixel(x,y,color)`，两者各做一次边界检查
   （`depthbuffer.h:31`、`framebuffer.h:36`）；而 `renderer.h:144-185` 用
   `fb.row(y)` / `zbuf_.row(y)` 行指针直写，完全免除边界检查。
   接入会造成**性能回退**。
2. **接口能力不足**：`fillTriangleFast` 只接受单一 `Color`（签名第 78 行），
   不支持 `Varyings` 插值与 `Shader::fragment` 回调，无法承载完整着色管线
   （`renderer.h:170-173`）。接入需大改接口，超出最小变更原则。

**被拒方案**：立即把 `Renderer::rasterTriangle` 替换为 `fillTriangleFast` 调用。
拒绝理由同上——会同时损失性能与功能。

**失效条件**：若将来把 `raster_fast` 改造为支持 `Varyings` 插值 + 行指针直写，
则应重新评估接入，且必须先立 ADR（架构级变更）。

**连带问题**：`core/benchmark.h → raster/rasterizer_fast.h` 构成 `core/ → raster/`
的层次倒置（`docs/architecture.md` §3 已记录）。修复方向：把 benchmark 的
光栅化对比用例移到 `raster/` 或 `tests/`，让 `core/` 回归纯基础设施。

## 风险

- **修完引入新 bug** → 缓解：以 `renderer.h:135-142, 167` 为**唯一正确性基准**
  逐行对照；先让新测试 FAIL 再改实现；改完跑全量 94 测试
- **符号归一化改错导致 CCW 也画不出来** → 缓解：CW 测试断言「两种绕序逐像素相同」，
  这样 CCW 若被改坏也会立刻暴露
- **浮点断言过严导致偶发失败** → 缓解：深度断言用 `EXPECT_NEAR(..., 1e-4f)`，
  不用 `EXPECT_EQ`

## 关联文档

- `docs/architecture.md` §3（依赖明细，含 `core/benchmark.h` 层次倒置）、§6.1（实验性定位）
- `docs/modules.md`「src/raster/」
- `.qoder/rules/testing_rules.md`「四类必测场景」「测试编写原则」

## 变更日志

| 日期 | 变更 |
|------|------|
| 2026-09-05 | 创建任务；完成两个缺陷的根因分析与正确性基准确认 |
| 2026-09-05 | 新增 3 个测试，实测确认 2 个 FAIL（误差 0.325 / 三角形完全丢弃） |
| 2026-09-05 | 修复 6 处权重映射 + `setupEdges` 符号归一化；97 测试全绿 |
| 2026-09-05 | commit `73122d3`，`--no-ff` 合入 `develop`，任务归档至 `DONE/` |
