# 变更日志

> 格式：日期 | 版本 | 变更内容 | 影响范围
>
> 按 `documentation_rules.md` 要求，每次有意义的变更都必须在此登记。
> 重大接口变更需同时在变更内容中标注版本号。

## 2026-10-25

### 四项验证全部闭环：CI 6/6 全绿，覆盖率 86.2%，并修掉一个只在远端暴露的缺陷

**首次推送就暴露了真问题**

`ac605e2` 的 CI（run `40184585824`）：Windows success，但 ubuntu / macOS / coverage /
sanitizers / thread-sanitizer 全在 Build 步骤失败。日志：

```
tests/unit/resource_test.cpp:97: error: missing initializer for member 'sr::Vertex::tangent'
                                  [-Werror=missing-field-initializers]
```

根因是 `Vertex` 有四个成员，测试里只用三个做聚合初始化。**MSVC 只警告，GCC/Clang 的
`-Werror` 直接失败**——本地 Release 全绿、远端必红的典型情形。

修复（第 18 项）给 `Vertex` 四个成员都加默认初始化器。它同时也消除了真实隐患：
`Vertex v;` 原本留下未初始化内存，`obj_loader` 等靠后续逐字段赋值弥补，漏赋即垃圾值进光栅化。
`normal` 默认取 `+Z` 避免零向量进光照。

**修复后 CI 全绿（run `40185625656`，commit `833fb32`）**

| job | 结论 |
|---|---|
| build-and-test (windows / ubuntu / macos) | success ×3 |
| coverage | success |
| sanitizers | success |
| thread-sanitizer | success |

**覆盖率拿到了真实数据**：CI 的 coverage job（ubuntu + gcov/lcov）报告
**行 86.2%（3588/4164）、函数 95.7%（465/486）**，超过 `testing_rules.md` 的 80% 行覆盖要求。
本机仍无法采集（五条路径已排除），但结论不再悬空。

至此四项验证全部闭环：clang-tidy（本机）→ 1 项缺陷；窗口示例 3/3；覆盖率 86.2%；CI 6/6。

### 覆盖率：给出一个可复现的替代指标

工具链五条路径全部排除后，改用一个**不依赖任何外部工具**的替代证据：从 `tests/` 与
`benchmarks/` 的全部 TU 出发，对 `#include` 图做 BFS，统计哪些公共头根本不会被测试代码编译到。

结果：**65 个公共头中 63 个可达**。不可达的 2 个均不包含功能代码：

- `core/namespace.h`——空 `namespace sr {}`，纯约定文档锚点；
- `scene/keyframe_animation.h`——已标注的遗留零引用头（见上文取舍表）。

结论：**不存在“有代码却完全没被测试编译过”的公共头**。但要明确：这只证明“被编译到了”，
**不等于行覆盖**——它不能告诉你 if 分支走没走到、异常路径有没有触发。

### 补齐：三个窗口示例全部实跑，覆盖率与 CI 再排除两条路径

**窗口示例（3/3 完成）**

| 示例 | 验证方式与结果 |
|---|---|
| `stage10_shadow_live` | WASD 引起 61.7% 像素变化（无输入基线 4.7%）；B 键切换阴影，帧率 5.2→7.3 |
| `stage03_sprite_anim` | 空格跳跃使铠甲上移 112px（源码峰值 90px，含窗口缩放换算），随后落地、脚下阴影恢复；相对路径写出 40 帧 PNG |
| `stage08_shader_live` | 1/2/3 切换使标题在 Phong→Flat→Gouraud→Phong 间变化，画面差异 34–37%，同模式往返基线仅 3.4%；W 前进也使画面明显变化 |

三者均用 **Esc** 退出（非 Alt+F4）。其中 stage08 在 15–55 FPS 下，60 ms 的瞬时敲击会被轮询漏掉，需按住 ≥300 ms——这是“按键看似无响应”类问题的常见诱因。

**覆盖率：新增一条排除证据**

`winget` 源更新失败，装不上 OpenCppCoverage。至此**五条**路径全部排除（CodeCoverage.exe 缺失、dotnet-coverage 对原生无效、LLVM 无 llvm-cov、winget 源不可用、MSVC bin 下无 `vsinstr`/`VSPerfCmd`）。

**远端 CI：定位到更准确的原因**

`git push` 三种方式（默认、`HTTP/1.1`+大缓冲、`sslBackend=schannel`）全部报 `Could not connect to github.com:443`。但同时刻：

- `Test-NetConnection github.com -Port 443` → **True**
- `Invoke-RestMethod api.github.com`（查 Actions runs）→ 成功

说明不是全链路断网，而是 **git.exe 进程无法建立到 github.com:443 的连接**，本机没有可配置的代理可绕。

### 把两项「未完成的验证」做完，并修掉它暴露的缺陷

先纠正一条错误结论：之前多份文档写「本机无 LLVM 工具链」，那是沿用旧记录、没有实际查找。
`VC/Tools/Llvm/x64/bin` 下有 **clang-tidy 19.1.5**（只是没装 clang 编译器）。

**clang-tidy 全仓扫描（已完成）**

- 65 个公共头逐个扫（`-x c++ -std=c++23`，启用 `clang-analyzer-*` 与 `bugprone-*`），139 条诊断。
- 发现并修复 **1 项真实缺陷**：`~ProfileScope` 调 `endScope()`，后者内部的字符串拷贝与
  `push_back` 可能抛 `bad_alloc`；析构是隐式 `noexcept`，异常逸出直接 `std::terminate`
  且无法被调用方捕获。现改为析构内捕获全部异常——剖析是辅助功能，不能因此终止进程。
- 其余 16 条逐条核实为**范围受控的误报**：8 条 `incorrect-roundings` 都是
  `int(x + 0.5f)` 且输入已被 clamp 到非负区间；4 条 `implicit-widening` 的乘积
  （`2*1024*1024`、`2*x` 且 x < 8192）都在 `int` 范围内；3 条 `misplaced-widening-cast`
  的 `size_t(i + radius)` 已保证非负；1 条 `branch-clone` 的两个分支体相同但语义不同。
- 122 条 `narrowing-conversions` 为刻意的 `float→int` 像素取整，不动。

**Windows 窗口示例交互（已完成）**

- 桌面自动化实跑 `stage10_shadow_live`：窗口正常创建，标题动态刷新帧率；
  WASD 后 **61.7%** 像素变化（无输入基线仅 4.7% 的自转），B 键切换阴影且帧率 5.2→7.3，
  Alt+F4 正常退出、无残留进程。

**仍不可得的两项（已把排除路径记录在案）**

| 项 | 具体证据 |
|---|---|
| 覆盖率 | BuildTools 无 `CodeCoverage.exe`；`dotnet-coverage 18.12` 已安装但对原生进程报「探查器未初始化」（补 PDB 后仍如此）；LLVM 组件无 `clang-cl` 与 `llvm-cov` |
| 远端 CI | GitHub API 只读可用（能查历史 run），但 `git push` 两次失败：`Recv failure: Connection was reset`、`Failed to connect to github.com:443` |

**验证**：构建 0/0；`unit_tests` **281/281**；像素回归 3/3；压测 --quick 85/105；
clang-tidy 对 `profiler.h` 的 `bugprone-exception-escape` 已消失。

### 文档数字校验：离屏示例的真实数量

把 `docs/` 里所有「N 个示例 / N 个用例 / N 个场景」类断言逐个与实测比对，
发现两处口径错误：

| 位置 | 原文 | 实测 |
|---|---|---|
| `project_context.md` | 20 个离屏示例 + 3 窗口 + 1 练习 + 2 测试 = 25 | **算术不成立**（20+3+1+2=26）。`examples/` 22 个 cpp 减 3 个窗口 = **19** 个离屏示例，加 `practice/` 才是 20 个离屏程序 |
| `00-学习路线` | 14 个阶段的参考实现（其中 2 个为实时窗口版） | 实际是 14 个阶段文件 **+ 2 个** `_live`，共 16 个 stage；原表述会被读成“14 个里含 2 个 live” |

其余断言核对无误：`architecture.md` 的「22 个示例」「其余 20 个示例」（= 22 减 2 个 live）、
`21` 与 `math/00` 的 281/64、`20` 的「3 场景 + 8 单元」、压测 105 场景、
默认可执行目标 25 个（实测 27 减去 2 个可选目标）。

本次只改现行文档；`changelog` 与本轮之前的历史条目保留原样——
它们记录的是当时的口径，不是当前事实。

### 新增：全仓缺陷审查记录

- 新增 [`engineering/59-全仓缺陷审查记录.md`](engineering/59-全仓缺陷审查记录.md)，把本轮的审查结论
  从 changelog 的流水记录沉淀为可查阅的一篇：方法（四种互补手段各自的盲区）、
  16 项修复清单（缺陷 / 表现 / 修复 / 验证）、3 处只文档化的取舍、4 项未完成的验证、
  以及可直接复制的复现命令。

### 完整回归（十六轮审查与修改之后）

| 检查项 | 结果 |
|---|---|
| Release 全量压测 | **105/105**（7.7 s，failures=0，regressions=0） |
| Debug+ASAN 全量压测 | **105/105**（159.5 s，无 ASAN 报告、无断言） |
| Release 单元测试 | **281/281**（64 套件） |
| Debug（ASAN）单元测试 | **279 通过 + 2 跳过** |
| 像素回归 | 3/3 |
| 离屏示例 | **20/20 退出码 0** |

一条观察（未修改）：`stage14_threaded` 在 800×600 上打印 `加速比 0.58x`——
16 个线程处理这么小的图，线程创建与同步开销超过并行收益。压测里的
`render_multithreaded`（同样分辨率但负载更重）测得 3.5x。这个数字容易被误读成
“多线程无效”，但它只是示例的演示输出，不是库的能力指标，本轮不动。

### 缺陷审查（十六）：两个参考组件的哨兵口径不一致

- `scene/components.h`：`MeshRef` 用 `-1` 表示未设置（注释解释了 0 是合法下标），
  但同为「资源下标」语义的 `MaterialRef` 默认值是 `0` —— 默认构造的组件会
  静默指向第一个材质。现统一为 `-1` 并补注释。
  两者目前都零使用（`MaterialRef` 只在哨兵测试里以同名局部结构出现），
  所以这是口径修正，不是行为回归。
- 新增 `ECS.ReferenceComponentsDefaultToUnset` 锁定两个组件的默认值。
- `scene/keyframe_animation.h` 确认**全仓零引用**：它定义了另一套手写关键帧
  动画（欧拉角 + 线性插值），与 `animation_clip.h` + `animation_player.h` 功能重叠。
  复查实现本身：二分查找的不变式保证了 `kf[lo].time != kf[hi].time`，
  所以那行除法的除零不可达，无功能性缺陷。给文件头补上“遗留、未接线”的状态标注，
  不删公共头（属接口级变更，需单独决策）。

**验证**：构建 0/0；`unit_tests` **281/281**（64 套件）；像素回归 3/3；压测 105/105；
Debug（ASAN）**279 通过 + 2 跳过**（实测）。

### 缺陷审查（十五）：computeTangents 的越界写

- `resource/mesh.h` 的 `computeTangents` 直接 `mesh.vertices[indices[i]]`，
  **不检查索引范围**。`drawMesh` 会丢弃越界三角形，但本函数是独立公共 API：
  对手写 Mesh 或解析异常的数据会越界写——Debug 下 MSVC 直接报
  `vector subscript out of range`，Release 下则是静默内存破坏。
  现逐三角形先校验三个索引，越界整体跳过。
- 退化兜底口径统一：原实现在“切向为零”时构造与法线正交的向量，但
  “切线与法线几乎平行（去完法线分量后接近零）”时兜底成固定 `(1,0,0)`——
  若法线也接近 +X，切线与法线仍然平行，TBN 依旧退化。现两处共用 `orthogonalTangent`。
- 新增 `Mesh.ComputeTangentsIgnoresOutOfRangeIndices`（Debug 下修前断言中止）。

**验证**：构建 0/0；`unit_tests` **280/280**（64 套件）；像素回归 3/3；压测 105/105；
Debug（ASAN）**278 通过 + 2 跳过**（实测）。

### 缺陷审查（十四）：法线贴图生成器的强度上溢

- `resource/texture_generator.h` 的 `normalMapBumps`：`strength` 只检查了 `isfinite`。
  极大值（如 `3e38`）会让 `dx * strength` 上溢成 inf，`normalized()` 得到 NaN，
  随后 `uint8_t(NaN)` 是未定义行为——实测部分像素解码后不再是单位法线。
  现把强度夹到 `[0, 100]`（凹凸强度的实用范围远小于此）。
- 补两条不变量用例：极端强度与默认强度下，把每个像素解码回向量都必须接近单位长度
  （极端那条修前为红，默认那条防止修复只覆盖极端值）。
- 复查 `src/io/image_write.cpp` 与 `src/io/tga.cpp`：TGA 写出已有 16 位尺寸上限检查
  与 `fclose` 返回值检查；stb 内部会把 JPG quality 夹到 `[1,100]`。只修了
  `image_write.cpp` 末尾的命名空间注释（写成 `imagewriter`，与文件头的 `sr::io` 不符）。

**验证**：构建 0/0；`unit_tests` **279/279**（64 套件）；像素回归 3/3；压测 105/105；
Debug（ASAN）**277 通过 + 2 跳过**（实测）。

### 缺陷审查（十三）：Profiler 的零覆盖与一条已知限制

- `runtime/resource_cache.h` 复查**未发现缺陷**：双检锁 + 64 分片锁保证同 key 串行构建、
  `purge` 用 `erase` 的返回值推进迭代器、失败结果不缓存、`weak_ptr` 到期自动失效。
  既有 3 个用例（含并发首次加载）已覆盖主要路径。
- `runtime/profiler.h` 在单元测试里**零覆盖**（此前只有压测场景）。补 5 条：
  启用时每个 scope 产生一条事件、关闭时不产生、嵌套两层各一条、未配对的
  `endScope` 不崩、`maxEvents` 截断并计数。新增 Profiler 套件（共 64 个）。
- 复查中发现一条**已知限制**，已写进注释：`setEnabled` 在运行期反复切换会让 scope 栈失衡
  （关闭期间 `beginScope` 不入栈，而 `endScope` 察觉不到，重新开启后会弹到外层的
  记录帧——外层时长偏大、内层事件丢失）。彻底修需要让关闭路径也入栈，
  会破坏“关闭时零分配”的设计目标，属有意取舍，因此只文档化不修改。

**验证**：构建 0/0；`unit_tests` **277/277**（64 套件）；像素回归 3/3；压测 105/105；
Debug（ASAN）**275 通过 + 2 跳过**（实测）。

### 缺陷审查（十二）：一次被既有测试拦住的「修复」

- 读 `runtime/command_buffer.h` 时发现：`executeSequential` 遇异常立即中止，
  而 `executeParallel` 会把剩余命令跑完再抛。初判为“串并行语义不一致”，
  于是改成“失败后停止领取新任务”。
- 改完跑全量，既有用例
  `ThreadFailures.executeParallel_failure_attemptsAllCommandsEvenWithOneThread`
  当场变红——它明确锁定“一个命令失败不阻止其他命令”是**有意设计**（并行命令
  通常互不相干，各自完成更有用）。实现已完整回退。
- 最终产出改为把差异写成显式契约：两个函数各自的异常语义写进注释，并补上此前
  缺失的 `CommandBuffer.SequentialStopsAtFirstFailure`（串行侧的异常行为
  之前没有任何用例）。两者对照，以后再改任一侧都会立刻被测试拦住。
- 教训：**改行为之前先跑既有测试**。本次能及时发现，正是因为改完立即跑了全量，
  而不是等到收尾批量验证。

**验证**：构建 0/0；`unit_tests` **272/272**；像素回归 3/3；压测 105/105。

### 缺陷审查（十一）：同一段 BRDF 被复制了两处

- `render/shader/deferred_lighting.h` 的 `lightingPass` 里有一段与 `PBRShader` 几乎逐行相同的
  Cook-Torrance BRDF。上轮修 PBR 的「roughness = 0 时 a2 = 0，D 恒为 0」时只改了
  `pbr_shader.h`，**漏了这个复制品**——延迟渲染的完美镜面同样没有高光。
  现同样取感知粗糙度下限 0.04，并补 `DeferredLighting.ZeroRoughnessStillProducesSpecular`
  （修前为红）。
- 这是典型的复制粘贴维护陷阱：修一处漏一处，而两者分别跑在不同的渲染路径上，
  Demo 里看不出差异。后续如果再改 BRDF，两处需同时改。
- 补 `Mesh.AllGeneratorsWindingMatchesNormals`：把原有的球体绕序用例推广到 cube /
  plane / planeGrid / sphere 四个生成器。逐三角形比较面法线（右手定则）与顶点法线同向，
  零面积面跳过。**四个生成器全部正确**。绕序错误会被背面剔除静默吃掉，是典型的“无报错但画面少一半”。

**验证**：构建 0/0；`unit_tests` **271/271**；像素回归 3/3；压测 105/105。

### 缺陷审查（十）：阴影采样用了错误的尺寸来源

- `render/shader/advanced_shaders.h` 的 `FinalShader::computeShadow`：边界检查用成员
  `shadowW/H`，但实际访问的是 `shadowMap`。两者不一致时（调用方只改了一个），
  检查“通过”、`get()` 拿到越界坐标：
  - Release：`DepthBuffer::get` 自身兜底返回 `kFar`，结果碰巧正确；
  - **Debug：`assert(x < width_)` 直接中止进程**（实测 exit=3）。
  现采样范围一律取 `shadowMap->width()/height()`，`shadowW/H` 仍保留给调用方
  构造 `lightVP` 的宽高比。
- 新增 `AdvancedShader.ShadowSamplingUsesMapOwnSize`（Debug 下修前断言中止，修后通过）。
- 本例的价值在于：它是一条**只在 Debug 暴露**的缺陷，Release 全量与像素回归
  永远发现不了。为此本轮把 `build/asan` 的 `SR_BUILD_TESTS` 打开，
  跑了一次 Debug 全量：**267 通过 + 2 按约定跳过**。

**验证**：Release 构建 0/0；`unit_tests` **269/269**；像素回归 3/3；压测 105/105；
Debug（ASAN）单测 267 通过 + 2 跳过。

### 缺陷审查（九）：场景节点与资产容器

逐个读了 `scene/scene_node.h`、`material.h`、`scene_asset.h` 与加载器的节点段，
**未发现缺陷**：

- `gltf_loader` 对 `node.mesh` 有负值与上界双重检查，`node.skin` 默认 -1（与 tinygltf 一致），
  且第 296 行有 `skinIndex` 范围校验。
- `SceneAsset::texture / material / materialForMesh` 三个索引入口都有边界检查。
- `Material` 的默认值（`metallic = 1`）与 glTF 2.0 规范一致；它和 `PBRShader` 的默认
  （非金属、粗糙度 0.5）不同属有意分层：数据层跟规范，着色层跟直觉。
- `SceneNode::meshIndex` 与 `meshIndices` 由加载器同步填充，不存在“单网格调用方
  拿到不一致值”的情况。

补上 `localMatrix()` 的用例（它是一切层级世界矩阵的乘基，却零测试）：
`SceneNode.LocalMatrixComposesTranslateRotateScale` 验证 T·R·S 顺序与
`hasMatrix` 覆盖 TRS 两个分支。用例数 **267 → 268**，套件 60 → 61。

**验证**：构建 0/0；`unit_tests` **268/268**；像素回归 3/3；压测 105/105。

### 缺陷审查（八）：粗线的宽度

- `render/draw2d/shape2d.h` 的 `drawLineThick`：用 `-half..half` 铺平行线，
  **偶数厚度会多画一条**（`thickness = 4` 画出 5 像素宽），与注释「thickness×thickness」
  不符。改成 `lo..hi` 后总行数恰为 `thickness`；同时把厚度夹到画布尺寸——
  本函数是 O(thickness²) 条 Bresenham，超过画布只会白烧时间。
- 至此上轮清出的 13 个零调用 API 已全部读完：**2 个有缺陷**
  （`drawLineDashed` 周期除零、`drawLineThick` 偶数宽度），其余 11 个实现正确。
- 新增 `Draw2D.LineThickMatchesRequestedWidth`（thickness 2–5 逐值验证，修前为红）。

**验证**：构建 0/0；`unit_tests` **267/267**；像素回归 3/3；压测 105/105。

### 缺陷审查（七）：虚线的除零与负周期

- `render/draw2d/shape2d.h` 的 `drawLineDashed`：`period = dash + gap` 没有下限。
  `dash = gap = 0` 时 `step % period` 是**整数除零**（UB）；`gap` 为负时周期变负，
  外段判断全乱。实测传入 `(0, 0)` 的结果是**一个像素都不画**（编译器对 UB 做了任意优化）。
  现 `dash <= 0` 退化为实线、`gap < 0` 视为 0，保证 `period >= 1`。
- 该函数与 `drawBezierCubic`、`blitImageScaled` 一样属于上轮清出的零调用 API，
  三个都已补用例：虚线两种退化输入、缩放 blit 的四象限映射、
  贝塞尔必须命中两个端点。用例数 262 → **266**。
- 同期抽样确认无问题的：`blitImageScaled`（源坐标下界由 `clipTargetRect` 保证，
  上界有 `min(h-1)` 夹取）、`drawBezierCubic`（`segments <= 0` 时循环不执行，不除零）。

**验证**：构建 0/0；`unit_tests` **266/266**；像素回归 3/3；压测 105/105。

### 缺陷审查（六）：零调用 API 清点

用「调用方清点」扫描全部公共头的自由函数：87 个导出函数里有 **13 个**除定义外
没有任何调用方（示例、测试、压测都没用过）。抽样逐个读实现：

| 函数 | 结论 |
|---|---|
| `perp` / `rotate2D` / `angleBetween` | 数学正确：`atan2(cross, dot)` 带符号，零向量不产生 NaN |
| `drawRect` | 四条边填充，厚度夹到 `min(w, h)`，不会越界 |
| `clipTriangleNearPlane` | 输出上限 4 与单平面裁剪的顶点数一致；已被完整 6 面版本替代 |
| `blitImageRotated` / `blitFramebuffer` / `clipTargetRect` | 变换链与 `long long` 裁剪均正确 |

抽查未发现新缺陷；补 3 条用例锁定最常用的三个：
`Vec2.PerpRotateAndSignedAngle`、`Draw2D.DrawRectOutlinesWithoutFillingCenter`、
`Draw2D.BlitFramebufferRespectsOffset`。

**验证**：构建 0/0；`unit_tests` **262/262**；像素回归 3/3；压测 105/105。

### 缺陷审查（五）：两个从未被执行的公共效果

- 清点后处理的所有导出函数发现：`vignette` 与 `chromaticAberration` 除了定义之外
  **没有任何调用方**（示例、单元测试、压测都没有）——属于从未被执行过的公共 API。
- 给两者补上用例后，`chromaticAberration` 当场变红：它用调用方传入的 `W/H` 去索引
  `src.getPixel`，与实际尺寸不一致时会**越界读**；而且不重建输出，只写出左上角一块。
  现以 `src` 的实际尺寸为准并按要求重建 `out`，同时拒绝非正的 W/H。
- 新增 `PostProcess.VignetteDarkensEdgesAndKeepsCenter` 与
  `PostProcess.ChromaticAberrationRebuildsMismatchedOutput`。
- 同期复查未发现问题的：`src/io/obj_loader.cpp`（索引范围、去重、非有限值、
  索引数非 3 倍数的检查链）、`runtime/thread_pool.h`（构造失败回滚、析构不丢弃
  已提交任务、自等检测、首个异常的归属）。

**验证**：构建 0/0；`unit_tests` **259/259**；像素回归 3/3；压测 105/105。

### 缺陷审查（四）：主光栅循环与输入解析

本轮逐函数读了以下模块，**未发现新缺陷**，结论记录如下以免重复审查：

- `render/pipeline/renderer.h`：包围盒取渲染器与帧缓冲尺寸的较小者，行指针直写安全；
  `z` 为 NaN 时深度比较为假自然丢弃；`discard` 不提交深度。
- `render/pipeline/varyings.h`：`interpolateVaryings` 覆盖 `Varyings` 全部 5 个字段。
- `core/color.h`：`fromFloat` / `lerp` 对 NaN 均因三目比较而落到 0，无未定义行为。
- `render/postprocess/post_processing.h` 的 `gaussianBlur`：半径夹取、核归一化、
  边界 clamp 与垂直 pass 的累加初值（`dst.clear()`）均正确。
- `src/io/gltf_accessor.h`：索引、sparse、形状、对齐、stride、accessor 越界六层校验齐全。
- `scene/particle_system.h`：容量跟随运行期配置、发射累加上限、`freeScan_` 续扫。

补上 `Rect2` 四类此前没有断言的契约（`contains` / 空矩形已存在覆盖）：
`IntersectionReturnsEmptyInsteadOfNegativeSize`、`MergedSkipsEmptyOperand`、
`FromMinMaxNormalizesReversedInput`、`ExpandedWithNegativeMarginShrinks`。

**验证**：构建 0/0；`unit_tests` **257/257**；像素回归 3/3；压测 105/105。

### 缺陷审查（三）：PBR 零粗糙度

- `render/shader/pbr_shader.h`：`roughness = 0` 时 `a = 0`、`a2 = 0`，GGX 的 D 恒为 0，
  镜面项完全消失——**完美镜面反而比粗糙表面更暗**，与成员注释「0=镜面」相反。
  实测 `roughness = 0` 的镜面项为 0，而 `0.8` 为 0.0078。
  现取感知粗糙度下限 0.04（Filament 同款），并让 `k`、反射采样层与粗糙度衰减
  都用同一个夹取值，避免几何遮蔽与分布对不上。
- PBR 着色器此前在单元测试里零覆盖：新增
  `PBRShader.ZeroRoughnessStillProducesSpecularHighlight`（修前为红）
  与 `PBRShader.SmootherSurfaceHasStrongerPeakSpecular`（粗糙度单调性）。
- 复查未发现问题的：`core/blend.h`（五种模式的公式与 alpha 合成）、
  `render/shader/shaders.h`（Blinn-Phong）、`resource/cubemap.h`（反射/辐照度的退化输入）、
  `core/image.h`（`pixelCount` 溢出与 `valid` 口径）。

**验证**：构建 0/0；`unit_tests` **253/253**；像素回归 3/3；
压测 Release **105/105**、Debug+ASAN 85/85（quick）。

### 缺陷审查（续）：四元数退化轴与压测判据

- `math/quat.h`：`fromAxisAngle` 对零/非有限轴无防护，而 `Vec3::normalized()` 只有
  `assert` 守卫，Release 下是除零——产出的 NaN 四元数会沿蒙皮链污染整帧矩阵。
  现退化输入返回单位四元数（与 `Quat::normalized()` 的零长度兜底同口径），
  非有限角度同样按无旋转处理。
- 压测场景 `draw_call_overhead` 用固定 200 μs/draw 判定，而 Debug+ASAN 下实测
  217 μs（Release 1.3 μs），慢构建下必然误报。改为验证“绘制确实发生”
  （屏幕中心像素被覆盖），性能回退交给 `--baseline`——与 `profiler_disabled_overhead`
  按构建类型分阈值是两种正确的写法，这里选前者因为无可断言的语义差异。
- 新增 `Quat.FromAxisAngleWithDegenerateAxisStaysFinite`。
- 同期复查但未发现问题的模块：`skeleton.h`（越界/零权重/零向量均有兜底）、
  `arena.h`（对齐与溢出检查完整）、`msaa_framebuffer.h`（写入与 resolve 边界完整）、
  `animation_player.h`（targetNode 有上界检查、零时长 alpha=0、结果有限性校验）、
  `barycentric.h`、`rect.h`、`transform2d.h`、`surface_shader.h`、
  以及 8 处 `assert` 均已确认用于“调用方保证”的约定而非唯一校验。

**验证**：构建 0/0；`unit_tests` **251/251**；像素回归 3/3；
压测 Release **105/105**、Debug+ASAN **105/105**（198 s）。

### 缺陷审查：相机退化参数与排序键一致性

- `scene/camera.h`：`projMatrix()` 对退化参数无防护（窗口最小化时 `aspect = width/height = 0`、
  远近平被推到重合），会产出 inf/NaN 投影矩阵，让整帧几何被管线静默丢弃——
  画面全黑但不报错。现夹到可用范围，`math::perspective` 保持纯计算不变。
  （修之前仅编译测试就会触发 MSVC C4723「潜在被 0 除」，常量传播已能算出来。）
- `scene/camera.h`：`right()` 在 `pitch = ±90°` 时叉积为零向量，归一化得 NaN；
  改用 `normalizedOr` 回退世界 X 轴。
- `render/pipeline/render_queue.h`：`sortKey()` 按 uint16 截断 `materialId`（-1 → 65535），
  而比较器用有符号比较（-1 最小），两个入口对同一队列给出相反顺序。
  比较器改为同口径（uint16），并补上注释说明为何不能用有符号比较。
- 新增 4 个单元测试：`RenderQueue.SortKeyAgreesWithComparatorForNegativeMaterial`、
  `Camera.DegenerateAspectKeepsProjectionFinite`、`Camera.CoincidentNearFarKeepsProjectionFinite`、
  `Camera.RightVectorFiniteWhenLookingStraightUp`。
- `engineering/48` 新增「当前瓶颈」与「两条提速路线」（SIMD 接主链路 / 后处理降分辨率，均未实施）；
  `ACTIVE.md` 待继续表补性能路线行。

**验证**：构建 0/0；`unit_tests` **250/250**（246 → 250）；像素回归 3/3；压测 105/105。

### 压测体系重构与第二十二轮扩展

**压测代码结构**

- `stress_main.cpp` 从 4661 行单文件拆为“框架 + 场景”：CLI / 汇总 / JSON / 基线留在入口，
  98 个既有场景迁到 `benchmarks/stress/stress_scenarios.cpp`，新增场景进 `stress_ext.cpp`。
- 注册表从手写 98 行数组改为 `SR_SCENARIO()` 静态登记（`stress/stress_common.h`）：
  场景顺序即执行顺序，新增场景不再需要改注册表，也不会漏登记。
- 场景数 98 → **105**。新增：`log_format_contract`、`gbuffer_write_contract`、
  `camera2d_roundtrip`、`render_queue_order`、`buffer_size_contract`、
  `clip_boundary_contract`、`polygon_fill_robustness`。

**修复**

- `core/depthbuffer.h` / `core/float_framebuffer.h`：构造器对非法尺寸无校验，
  `size_t(width) * height` 把负数转成 ~2^64，在容器分配处抛 `length_error`；
  而 `resize()` 对同类输入只是忽略——两条路径口径不一致。现统一为“任一边非正 → 0×0”。
- `render/draw2d/shape2d.h`：`fillPolygon` 缺少 `fillCircle` / `fillRoundedRect` 都有的
  `allFinite` 前置校验，NaN 顶点会让 `(int)std::floor(NaN)` 进入 UB；屏幕外 ±1e30 的顶点
  同样有 float→int 溢出风险。补校验并把扫描线范围夹到屏幕范围（最初夹到 1e7，
  导致 64×64 场景跑 1.2 s；收紧后 0.12 ms）。

**验证**

- Release：构建 0/0；`unit_tests` 246/246；像素回归 3/3；压测 **105/105**（8.2 s，`--quick` 85 个 / 1.8 s）。
- Debug + ASAN：压测 **105/105** 无断言无越界（171 s）。

### 文档体系整理（仅 docs/ 与根 README，无代码变更）

- 新增 `docs/README.md` 作为唯一文档入口：目录职责表、权威数字优先级（project_context → ACTIVE → changelog）、三条阅读路线。
- 删除 `docs/math/09-13-综合.md`：09~13 的旧合并版，内容已被拆分的五篇覆盖且无任何引用。
- `docs/tasks/README.md` 改为纯导航：去掉与 ACTIVE 重复且已大面积错误的状态列（TASK-014~018 曾标「待开始」）；`project_review.md` 死链改为备注说明。
- `docs/modules.md` 按实际 `include/softrenderer/` 重组：拆出 resource / scene / io，render 下分 raster / pipeline / draw2d；删除已不存在的 `gltf_types.h`、`line.h`、`core/benchmark.h` 条目与 2.0 前的依赖链。
- `docs/00-学习路线与项目导读.md`：源码路径从 `src/` 改为 `include/softrenderer/`，用例数 169→246，命名空间 `draw2d`→`sr::draw2d`，文档结构表改为指向 docs/README。
- `docs/adr/README.md`：修被空行打断的表格与 ADR-001 缺失链接。
- 教程正文不再保存用例数等易漂移数字（`engineering/20`、`math/00`、`engineering/58`），统一指向 project_context。
- 根 `README.md`：目录表改 2.0 布局，移除 `docs/project_review.md` 死链，补充文档索引入口。

## 2026-10-24

### 全仓缺陷修复（审查驱动，未升版本）

**正确性**

- `resource/texture.h`：默认构造 / 无效图像的采样不再访问空 `mips_`；新增 `sampleLevel`、带 alpha 的 `sampleColor`。
- `core/image.h`：`valid()` 同时要求存储长度与尺寸一致；新增 `pixelCount()`（含溢出保护）。
- `resource/cubemap.h`：`valid()` 校验六面；零方向向量返回 0；`sampleReflection` 按 roughness 选 mip 层（此前完全忽略该参数）。
- `core/blend.h`：`div255` 改为精确四舍五入；AlphaBlend 分开计算颜色与 alpha（原实现算出 As×As + Ad×(1−As)）；Premultiplied 先把目标预乘并保留合成 alpha。
- `resource/mesh_generator.h`：球体三角形绕序改为与向外法线一致，并跳过两极退化三角形（此前默认背面剔除会剔掉正面）。
- `render/shader/pbr_shader.h`：环境反射方向改用 `reflect(-V, N)`（原式把 V 与 N 写反）。
- `render/postprocess/post_processing.h`：FXAA 改真正的亚像素双线性采样；SSAO `configure()` 重建噪声、参数收敛、每帧重置输出；DOF / 径向模糊 / 高斯 / 亮部提取补尺寸与别名校验。
- `render/pipeline/render_queue.h`：透明层改为距离优先、材质次级；`sortKey` 同步；非有限距离归零。
- `render/raster/coverage.h` + `render/pipeline/renderer.h`：覆盖判定与属性插值分离，重心坐标改用无偏边函数与真实面积。
- `render/draw2d/*`：AA 边缘 alpha 乘源 alpha；`blitTexture` 取像素中心、传导数、保留纹理 alpha。
- `scene/skeleton.h`：全局变换与骨骼数组顺序无关，父索引越界 / 成环返回空；蒙皮越界索引被忽略，零权重不回退成零向量或 NaN。
- `scene/particle_system.h`：`sizeMin` 生效（新增 `Particle::sizeStart`），运行期改 `maxParticles` 真实扩容，非有限 dt 被忽略，零方向向量有兜底。
- `scene/scene_node.h` + `src/io/gltf_loader.cpp`：新增 `meshIndices`，多 primitive 展平后节点引用全部网格；越界 mesh 索引在转换期失败。
- `src/io/obj_loader.cpp`：独立校验位置/法线/UV 索引上界，失败不修改输出。
- `src/io/image_loader.cpp`：不再使用 stb 全局垂直翻转开关（会污染 tinygltf 贴图方向且存在并发竞态），改为逐行拷贝翻转。
- `src/io/tga.cpp`：检查 `fwrite`/`fclose` 返回值，宽高超出 16 位直接失败。
- `math/rect.h`：空矩形不再判为与任何矩形相交。
- `core/float_framebuffer.h`、`core/gbuffer.h`：`resize` 先构造后发布，异常时不再留下尺寸与数据失配。

**运行时与平台**

- `runtime/resource_cache.h`：并发同 key 发布前二次检查，保证对外只存在一个活跃实例。
- `runtime/profiler.h`：JSON 字符串转义、写盘移出锁、开关改原子、`ProfileScope` 禁复制、时钟改 `steady_clock`。
- `third/log/Logger.cpp`：异步线程池只初始化一次（不再替换全局池致旧 logger 失效）；补 `std::exception` 捕获；移动语义清理源对象；单例未初始化时返回空实现。
- `platform/win32_window.h`：禁止复制/移动，`present` 校验源尺寸，创建失败即关闭，UTF-8 标题正确转 UTF-16。

**测试与 CI**

- 单元测试 179 → **218 用例 / 52 套件**，新增覆盖上述缺陷的回归用例。
- 像素回归基准图改为 **PNG（RGBA）**，比较纳入 alpha；`SR_UPDATE_GOLDEN` 改为覆写已有基准并检查写失败。
- 示例去重：`stage02/03` 改用 `draw2d::drawCharSprite`，`demo_stars` 改用 `postprocess::resolveToLdr`，`demo_gltf` 加载失败不再回退伪造成功。
- `tests/CMakeLists.txt`：架构门禁移到组件开关之前，裁剪构建不再丢失校验。
- CI：缺失示例目标即失败、离屏示例必须跑满 20 个、新增 benchmark 实际运行、TSAN 覆盖 `ResourceCache`。
- `.clang-tidy`：`HeaderFilterRegex` 补上 `include/softrenderer`。

### 压测发现并修复（同日）

用 `build/stress` 独立消费工程跑了 28 个高负载/敌意输入场景，修复：

- `resource/texture.h`：UV 或导数为 NaN/Inf 时不再进入 float→int（UB）与 NaN 插值；非有限 UV 返回兜底色、非有限导数按第 0 层；mipmap 生成把 alpha 一起平均（此前半透明纹理一旦采到低层 mip 就变不透明）。
- `scene/particle_system.h`：发射改为游标扫描 + 存活计数，且发射量受剩余空槽限制；10 万粒子 20 帧从 27.5 s 降到约 36 ms。
- `render/draw2d/blit2d.h`：所有 blit 先与帧缓冲、裁剪区求交，超屏目标不再逐像素空转；4000×4000 目标从 3.5 s 降到约 0.3 s。
- `render/pipeline/renderer.h`：`drawMesh` 丢弃索引越界或为负的三角形（`Mesh` 是公共结构，越界读是 UB）。
- `src/io/obj_loader.cpp`：拒绝 nan/inf 顶点（文本可被解析成 NaN，会让包围盒、平滑法线、蒙皮全变 NaN）。
- `render/postprocess/post_processing.h`：DOF 的 `maxBlur` 夹到画面尺寸量级，避免 `maxBlur=1e6` 时的 (2e6)² 循环。
- 新增 9 个回归用例（219 → 228）：纹理非有限输入、mip alpha、blit 裁剪、粒子容量上限、Mesh 越界索引、OBJ 非有限顶点、DOF 语义锁与半径上限。
- 已否决的优化：把 DOF 二维窗口改成可分离两趟。半径逐像素不同时两者并不等价，被新增的语义锁用例当场抓住，已回退。

### 压测二轮：性能与容量（同日）

- `render/postprocess/post_processing.h`：DOF 改为**按半径分桶 + 增量列窗口 + 行内前缀和**。对每个出现的半径 r 逐行维护列窗口（增/减端点行），水平窗口用前缀和，两层 clamp 可交换所以与逐像素二维窗口**严格相等**（由语义锁用例坐实）。128²、maxBlur=48：122 ms → 0.4 ms；1920×1080、maxBlur=48：1.58 s → 0.60 s。
- `resource/texture.h`：mip 生成改用行指针 + 预计算列索引。**实测无可测收益**（2048² 仍约 38–42 ms/张，best-of-3 与改前差异在噪声内）；瓶颈在分配与 box filter 本身而不在索引计算，故此次只作为可读性整理保留，不计作性能提升。
- `runtime/profiler.h`：新增事件上限 `setMaxEvents()`（默认 100 万）与 `droppedEvents()`，溢出计数写入 trace JSON 的 `droppedEvents` 字段；`clear()` 一并清零。
- 压测从 28 个场景扩到 **35 个**：新增 `dof_1080p_r48`、`profiler_event_cap`、`draw2d_scissor_edges`、`math_singular_inverse`、`msaa_resolve_size_mismatch`、`threadpool_64_threads`、`gltf_empty_model`。全部通过，无新缺陷。
- 已验证安全、不需修改：`MSAAFlatRenderer::resolveTo` 已对目标尺寸取 min；奇异矩阵 `inversed()` 返回有限值（不产生 NaN）；反向 scissor 不绘制任何像素。

### 压测三轮：2D 静默降级与长时稳定性（同日）

- `render/draw2d/blit2d.h`：新增 `blitTextureTransformed()`——纹理四边形支持任意仿射（逆映射 + 包围盒预裁剪 + 图空间越界不绕回）。
- `render/draw2d/renderer2d.h`：
  - `Renderer2D::texture()` 在旋转/斜切下不再 `return` 静默不画，改走上面的四边形路径（与 `image()` 行为一致）。
  - `Renderer2D::rectGradient()` 在旋转下不再退化为纯色，改为逐像素逆映射取渐变参数（`c1` 不再被丢弃）。
- 压测场景 35 → **38**：新增 `soak_1200_frames`（进程私有内存采样）、`render_path_consistency`（主 Renderer vs `rasterizer_fast` 覆盖集合对比）、`renderer2d_rotated_texture`。
- 实测：soak 1200 帧后私有内存 **0 KB 增长**（无逐帧泄漏）；两条光栅路径在相同三角形下覆盖像素 **完全一致**（`diff=0`）。
- 压测自身修正：一致性用例最初用自写视口公式换算屏幕坐标，造成 2 像素假差异；改用库的 `transform::viewport` 后归零，避免把压测误差报成库内缺陷。

### 压测四轮：动画采样与大尺寸后处理（同日）

- `scene/animation_player.h`：关键帧区间定位由**逐帧扫描 O(n)** 改为 `std::upper_bound` 二分；`samplerIsValid` 新增 `trusted` 参数。
- `scene/animation_clip.h`：新增 `validated` 标记（加载期已校验）。`src/io/gltf_loader.cpp` 在 `decodeSampler` 完成后置 true；手工构造的片段保持 false，仍走完整校验（现有非法时间轴用例全部保留）。
- 实测（1 万关键帧、2 万次采样）：**1 426 ms → 18.8 ms**。
- 压测场景 38 → **43**：新增 `command_buffer_50k`、`animation_10k_keys`、`msaa4x_1080p`、`renderer2d_transforms_2k`、`sceneasset_deep_copy`。
- 实测：`CommandBuffer` 5 万命令并行/顺序/重复执行均为 50000 且异常正常传播；`MSAA 4x 1920×1080` 388 ms；`Renderer2D` 2000 组随机变换+渐变+贴图 281 ms；`SceneAsset` 含 4 张 256² 带 mip 纹理的深拷贝 40 次 58.6 ms（值语义代价，非缺陷）。

### 压测五轮：DOF 二维积分图（同日）

- `render/postprocess/post_processing.h`：DOF 新增 **SAT（二维积分图）快路径**，每像素窗口和 = 四个角点相减，与半径数无关。边界 clamp 拆成「中间矩形 + 左右列带 + 上下行带 + 四角」；内部像素走无补齐分支。半径 < 4 或图像过大（> 160MB 临时内存）时仍用分桶路径。
- 实测 1920×1080、maxBlur=48：**573 ms → 240 ms**（相对最初逐像素二维窗口为数十倍）；128² 小图仍走分桶路径，约 1 ms。
- 实现期被测试抓住的两个公式错误（均已修，保留注释作警示）：右列带写成 `at(W,·)-at(W,·)`（实际需减 `at(W-1,·)`，结果偏大 2.4 倍）；上行带写成 `at(·,H)-at(·,0)`（实际需减 `at(·,H-1)`，偏大 2.6 倍）。
- 新增测试 `DepthOfFieldPreservesUniformImage`：均匀色图经任意模糊必须等于原值——上面两个错误正是被它一眼定位。

### 压测六轮：压测入库与纹理生成（同日）

- **压测程序入库**：`benchmarks/stress_main.cpp` + 新目标 `renderer_stress`（`SR_BUILD_BENCHMARKS=ON` 时构建）。之前它只存在于 `build/`（被 gitignore），无法沉淀。现共 **46 个场景**，退出码即结果（`failures=0` 时返回 0），可直接接入 CI。
- `resource/texture.h`：mip 生成新增无边界分支的快路径（宽高 >1 时倍率索引必然在界内），并把 `/4` 改 `>>2`。
- `resource/texture_generator.h`：`checkerboard` / `uvGrid` 用行/列 LUT 消除百万级逐像素整数除法。
- 实测 2048²：程序化生成 **20 ms → 9 ms**；mip 链构建约 21 ms/张，图像拷贝 3 ms/张（拷贝属调用方传入方式，非库开销）。
- 新增 3 个场景：`render_4k_sphere`（3840×2160，171 ms，6016 三角形）、`renderer2d_text_5k`（5000 个不同字符串，78 ms）、`tga_1920x1080_roundtrip`（写+读 6.2 MB，42 ms）。均通过，无新缺陷。
- CI 新增 `Run stress probe` 步骤：三平台矩阵上构建并运行 `renderer_stress`，失败即红灯。

### 压测七轮：延迟渲染防 NaN（同日）

- `core/gbuffer.h`：`write()` 拒绝非有限深度。此前 NaN 深度能穿过 `z >= depth[idx]`（NaN 比较恒假），一旦写入，后续所有像素与 NaN 比较也是假，**深度测试整体失效**。
- `render/shader/deferred_lighting.h`：`lightingPass()` 先校验四路 RT（worldPos/normalRough/albedoMetal/depth）长度一致，不一致直接返回；深度非有限按背景处理。
- 压测场景 46 → **47**：新增 `deferred_1080p`（1920×1080、掺 1/1000 的 NaN 深度、637k 次写入 + 全屏光照）。实测 175 ms，NaN 被拒、深度次序保持、输出无 NaN。
- 新增 2 个单元用例：`GBuffer.RejectsNonFiniteDepth`、`GBuffer.LightingPassRejectsInconsistentBuffers`（230 → 232）。

### 压测八轮：颜色转换的最后一道闸门（同日）

- `core/color.h`：`fromFloat()` 的 `clamp01` 改为 NaN 安全写法（`v > 0 ? (v < 1 ? v : 1) : 0`）。旧写法两个比较对 NaN 都是假，NaN 原样传入 `uint8_t(NaN * 255 + 0.5f)` —— 未定义行为。现在着色器输出 NaN → 0、+Inf → 255、−Inf → 0。
- `core/color.h`：`lerp()` 的 t 同样夹到 [0,1]，避免 NaN 的 t 进入 `uint8_t` 转换。
- 压测场景 47 → **48**：新增 `nan_fragment_color`——着色器直接输出 (NaN, +Inf, −Inf)，断言屏幕像素恰为 (0,255,0,255)。
- 新增 2 个单元用例：`Color.FromFloatClampsNonFinite`、`Color.LerpClampsNonFiniteT`（232 → 234）。
- 顺带核实：`DepthBuffer::testAndSet` 与 `Renderer` 行内深度测试均用 `z < slot`，NaN 会被自然拒绝，无需修改。

### 压测九轮：2D / 后处理的浮转整闸门（同日）

- `render/draw2d/shape2d.h`：新增 `draw2d::allFinite({...})`，并在 `fillCircle` / `fillEllipse` / `drawEllipse` / `fillRoundedRect` / `drawRoundedRect` 入口统一拒绝非有限参数。旧实现的 `r <= 0` / `w <= 0` 对 NaN 恒为假，拦不住后面的 `int(std::floor(NaN))`（未定义行为）。`drawEllipse` 的细分段数也增加了上界（`rx` 极大时 span 可能是 inf）。
- `render/postprocess/post_processing.h`：`depthOfField` 拒绝非有限 `focusDist`；`radialMotionBlur` 拒绝非有限 `strength`；`chromaticAberration` 拒绝非有限 `offset`——三者都直接接到 `int(...)` 转换。
- 压测场景 48 → **49**：新增 `nonfinite_2d_api`，用 NaN/±Inf 调用上述 8 个 API，断言画面保持纯黑（全部被闸门拦住）。
- 新增单元用例 `Shape2D.NonFiniteParametersAreIgnored`（234 → 235）。

### 压测十轮：框架化 + 资产语义 / 并发 / IO / 吞吐（同日）

**压测程序框架**（`benchmarks/stress_main.cpp`）

- 改为**场景注册表**驱动：每行 `{id, fn, heavy}`，新增场景只加一行；支持 `--list` / `--filter=<substr>` / `--quick` / `--json=<path>` / `--baseline=<path>` / `--strict-baseline`。
- `--baseline` 读上一轮 JSON，超过 `1.5× + 5 ms` 打印 `[SLOW]`，把「性能回退」变成可机械判定的信号；`--quick` 跳过 11 个重型场景，1.8 s 跑完 64 个。
- 场景 49 → **75**，全绿，全量 7.4 s。新增分组：资产语义（glTF 多 primitive / 非三角 mode、骨骼层级、Cubemap 粗糙度）、并发（ResourceCache single-flight、纹理只读并发、Profiler 多线程、线程池析构清空队列）、数值边界（超大三角形、退化三角形、极端投影、深度次序、MSAA 边缘覆盖、PBR 极端材质、动画损坏数据、粒子极端配置、帧缓冲越界读）、IO（TGA RLE/16bit/调色板/顶部原点/超大头部、OBJ 2 MB 单行、PNG 解码）、吞吐（blit / fill / 采样 / 小三角形 / 图像解码）。

**压测发现的缺陷（已修）**

- `runtime/resource_cache.h`：同一 key 的并发首次加载会**各自构建一份**（16 线程 + 2 ms 工厂 = 92 次重复构建，实测 452 次）。改为 64 分片锁实现 single-flight：同 key 串行构建，不同 key 仅哈希冲突时短暂互斥；锁序固定为「分片 → 全局」。实测并发构建 **452 → 1**。
- `render/pipeline/msaa_renderer.h`：屏幕坐标超过 `TriangleCoverage` 的 ±2^20 上限时**整块几何被静默丢弃**（相机贴地的地板三角形会整块消失，16384 像素变成 0）。新增视口矩形 Sutherland-Hodgman 2D 裁剪，颜色与深度线性插值后逐三角扇光栅化。
- `src/io/tga.cpp`：先按头部声明的宽高分配缓冲再读文件，`8192×8192` 的 118 字节文件会触发 192 MB 分配（耗时 49 ms 的纯放大攻击）。改为先卡像素上限（8192²）并对未压缩类型校验「剩余文件长度 ≥ 声明长度」，通过后才分配。

**性能优化（均有压测数值支撑）**

- `resource/texture.h`：新增 `lodForPixel` / `sampleColorWithLod` 公开入口；`bilinearSpan` 用分支代替每像素 4 次整数取模；`floorFast` 在 |x| < 2^23 时用截断+修正代替 `std::floor`（x64 基线无 `roundsrd`）；lod 为整数时跳过层间插值（1:1 blit 的常见情形）。双线性采样 11.2 → 18.3 M/s，三线性 3.8 → 5.1 M/s。
- `render/draw2d/blit2d.h`：轴的 blit 屏幕空间导数是常量，mip 层改为循环外算一次。`blitTexture` 1024² 全屏 **335 ms → 94 ms**（3.1 → 11.2 Mpix/s）；`draw2d_20k_rects` 中的 4000×4000 目标 282 → 84 ms。
- `runtime/profiler.h`：`flush` 改为内存拼装 + 单次 `fwrite`，时间戳用整数微秒，其余字段手写十进制追加。200k 事件：记录 82 ms + 写盘 285 ms（原合计 946 ms）。
- `render/pipeline/msaa_renderer.h` + `render/raster/coverage.h`：`MSAAFlatRenderer` 复用覆盖计算的整数边函数算重心，不再每样本调一次浮点 `barycentric`。

**验证**

- 构建 0 错误 0 警告；`unit_tests` **239/239**；`render_regression` **3/3**；`renderer_stress` **75/75**（`--quick` 1.8 s）。
- 新增 4 个单元用例（235 → 239）：`ResourceCache.ConcurrentFirstLoadBuildsOnce`、`MSAACoverage.rasterize_hugeTriangleCoversViewport`、`TgaLoader.oversizedHeader_rejectedWithoutLargeAllocation`、`TgaLoader.rle24_decodesBgrToRgbaWithBottomOrigin`。
- `.gitignore` 新增 `stress-out/`（trace.json 单文件可达 25 MB）。

### 压测十一轮：默认零开销 + 大画面链路（同日）

**压测发现的缺陷（已修）**

- `runtime/profiler.h`：`setEnabled(false)` **无效**——`beginScope/endScope` 从不检查开关，关闭后仍在记录（实测 100 万次 scope 仍产出 100 万事件、耗时 480 ms）。现在两者入口都先读原子标志，关闭时只做一次原子读（12 ms / 100 万对）。
- `runtime/profiler.h`：默认值从**开启**改为**关闭**。库内部不自动打点，默认开启只会让使用方的 scope 白担锁与内存；四个依赖 trace 的示例（`demo_gltf` / `demo_ibl_pbr` / `demo_pbr_material` / `demo_skinning`）改为显式 `setEnabled(true)`。
- `endScope` 在关闭状态下仍会把 thread_local 栈弹干净，避免开关中途切换时留下错配条目。

**压测程序改进**

- 每个场景运行包 try/catch：单个场景抛 `bad_alloc` 不再终止整个探针进程（84 个场景共用一个进程，死在中间会让后面全部失去覆盖）。
- `--filter` 无匹配时返回码 2 并提示，避免拼错过滤词时“静默全绿”；帮助文本改为只描述场景 id（实际匹配的就是 id）。
- 修正压测自身失真：`obj_50k_faces` 的 434 ms 实际花在测试自己逐行 `ofstream <<` 写 20 万行上，改为内存拼装后一次写出（434 → 49 ms），并把写/解析分开报告。
- 场景 75 → **84**（`--quick` 68 个 / 1.8 s，全量 9.3 s），新增大类：场景图更新（1 万节点× 50 帧）、并发渲染（4 线程自拓 Framebuffer，speedup 3.17）、1080p 后处理链、纹理生命周期（1000 张带 mip 纹理，私有内存 0 MB 增长）、Profiler 关闭开销、MSAA resolve 吞吐、非法 Mesh、Arena 边界、粒子运行期扩缩容。

**性能优化（均有压测数值）**

- `render/pipeline/msaa_renderer.h`：样本偏移是像素内的固定 1/8 值，改为「像素中心边函数 + 每样本常量增量」——每样本从“2 次坐标量化 + 9 次整数乘法”降到 3 次加法，按行用 `advanceX/advanceY` 推进。`msaa4x_1080p` **364 → 133 ms**，`msaa4x_1024_sphere` **99 → 33 ms**；5 个 MSAA 用例的精确期望值不变（覆盖集合与深度逐位一致）。
- `render/postprocess/post_processing.h`：
  - `gaussianBlur` 垂直 pass 改为按核偏移逐行累加（内层循环在连续内存上），`231 → 185 ms`。
  - `resolveToLdr` 改用 sqrt 索引的 1024 段 gamma LUT（误差 < 1e-4，8 位下 0.03 LSB），`160 → 110 ms`；新增用例 `PostProcess.GammaCorrectFastMatchesPowWithinLsbTolerance` 锁定精度。
  - `brightPass` / `addBlend` 改行指针，不再逐像素做边界检查。
  - 1080p bloom 链 `366 → 313 ms`。
- `src/io/obj_loader.cpp`：`indexCache` / `vertices` / `indices` 按索引总数预留，避免 15 万次插入的多次 rehash，`50k 面解析 190 → 158 ms`。

**验证**

- 构建 0/0；`unit_tests` **240/240**（+1 gamma LUT 精度）；`render_regression` **3/3**；`renderer_stress` **84/84**。

### 压测十二轮：Debug/ASAN 构建下的退化向量（同日）

**用 Debug + ASAN 构建跑同一套压测**，暴露了 Release 下被静默掩盖的退化输入问题：

- `math/vec.h`：新增 `normalizedOr(fallback)`。`normalized()` 保留“调用方保证非零”的严格契约（Debug 有 `assert(len > 0)`），外部顶点属性必须用新函数。
- `render/pipeline/surface_shader.h`：`toWorldNormal` 改用 `normalizedOr({0,0,1})`。零法线/NaN 法线会让 Debug 构建直接断言中止（Release 则把 NaN 传进光照）。
- `render/shader/*`：`pbr_shader` / `shaders` / `advanced_shaders` / `deferred_lighting` 的法线、视向量、半程向量、切线统一改用 `normalizedOr`；零光向兑底为 (0,1,0)；`FlatShader` 的退化面法线给稳定方向。正常输入的计算路径（`x / len`）未变。
- `resource/mesh.h`：`computeTangents` 对零法线兑底。全退化三角形（共线/重复顶点）叠加无法线时，原实现会在 Debug 断言。
- 压测场景适配 Debug：`framebuffer_bounds` 的越界读写属“调用方错误”（库用 assert 强制契约），只在 `NDEBUG` 下验证；吞吐/内存阈值在 Debug 下另给一档，只用于发现数量级崩坏。

**验证**

- Debug + ASAN 全量 **84/84** 通过，无断言、无 AddressSanitizer 报告（耗时 147 s）。
- Release：`unit_tests` **242/242**（+2：`Vec.NormalizedOrFallsBackOnDegenerateInput`、`Mesh.ComputeTangentsHandlesDegenerateNormals`）；`render_regression` **3/3**；`renderer_stress` **84/84**；**20** 个离屏示例退出码 0（另 3 个窗口示例仅构建、不参与自动运行）。

### 压测十三轮：图集寻址 / 大场景 / SSAO 成本（同日）

**发现的缺陷（已修）**

- `render/draw2d/blit2d.h` + `resource/texture.h`：2D 贴图用 Repeat 寻址，放大小图时边缘列会混入对边颜色（2×2 放大到 8×8，右边缘混入 37.5% 的左边缘颜色，图集直接串色）。新增 `Texture::sampleNearestClamped` / `sampleColorClamped`（ClampToEdge），`blitTexture` 与 `blitTextureTransformed` 全面改用它；3D 表面纹理仍走 Repeat。修复后四角恢复纯色（实测 255/0、0/255、255/255）。

**性能**

- `render/postprocess/post_processing.h`：SSAO 每样本 3 次除法→ 1 次除法 + 乘法（1080p × 16 样本是 3300 万次），全分辨率 **727 → 589 ms**。
- 新增 `SSAO::computeHalfResolution`：半分辨率计算（取 2×2 代表像素，不平均法线）+ 双线性上采样。1080p **218 ms**（全分辨率的 1/3），与全分辨率平均偏差 4e-6。

**压测扩展（84 → 89）**

- `gltf_large_scene`：500 个 mesh + 500 个 node 的 glTF，验证展平与节点映射不错位（500 mesh / 22 ms 导入）。
- `draw_call_overhead`：2000 次“clear + 极小三角形”，1.45 μs/draw（含 clear）。
- `texture_atlas_edges`：2×2 四色放大到 8×8，锁定 Edge 寻址不被 Repeat 污染。
- `ssao_1080p` / `ssao_half_res_1080p`：用自洽透视矩阵 + 柱状遮挡构造真实遮蔽（minAo 0.94），并比较两条路径的速度与质量。
- 新增单元用例 `Blit2D.EdgeSamplingDoesNotWrapToOppositeSide`（242 → 243）。

**验证**

- 构建 0/0；`unit_tests` **243/243**；`render_regression` **3/3**；`renderer_stress` **89/89**（`--quick` 71 个 / 1.9 s，全量 9.9 s）。

### 压测十四轮：帧间隔离 / 图像格式 / 阴影成本（同日）

本轮主要补齐三条未覆盖的链路，并把测试自身的失真改正：

- `renderer_frame_isolation`：验证 `Renderer::clear()` **同时重置深度**（否则第二帧远端几何会被上一帧遮挡），以及两个 `Renderer` 实例的深度互不干涉。实测通过——这是回归保护，不是新缺陷。
- `image_formats_roundtrip`：PNG / BMP / TGA 写出再读回 **逐像素无损**（含坐标朝向），JPG 大色块主色正确，不可写路径返回 false。
- `shadow_map_pcf`：1024² 深度图渲染（4 个球 31 ms）+ 512² 全屏 3×3 PCF（9 ms），平均受光率 0.53，作为阴影链路的成本基线。
- 修正测试设计：JPG 用例最初用 16×12 图 + 单像素断言，而 16×12 只有 2×2 个 8×8 DCT 块，整图会被有损压缩糊掉——那是否式特性不是缺陷。改为 64×64 大色块 + 区域均值。

**验证**：构建 0/0；`unit_tests` **243/243**；`render_regression` **3/3**；`renderer_stress` **92/92**（`--quick` 73 个 / 1.6 s，全量 8.3 s）。

### 压测十五轮：样本写入合并 + 异步生命周期（同日）

**性能**

- `core/msaa_framebuffer.h`：新增 `testAndWriteSample(x, y, sample, z, color)`。原来 `testDepth()` + `setSample()` 会各自做一遍边界检查与一次索引乘法，而两者用的是同一个样本索引；合并后每样本只算一次，语义不变（仍然只有深度通过才写颜色）。
- 实测（多次取中位）：`msaa4x_1080p` **133 → 约 105–120 ms**；`bench_raster_small_tris` **2.0 → 约 2.8 Mtris/s**。5 个 MSAA 用例的精确期望值不变。

**压测扩展（92 → 93）**

- `async_lifecycle`：验证 CommandBuffer 析构时释放未执行命令捕获的 `shared_ptr`（`use_count` 回到 1）、执行后不消费命令、ThreadPool 析构时未取的 future 仍执行完、未观测异常不会让析构崩溃。实测全部通过。

**验证**：构建 0/0；`unit_tests` **243/243**；`render_regression` **3/3**；`renderer_stress` **93/93**（`--quick` 74 个 / 1.7 s，全量 9.1 s）。

### 压测十六轮：文字渲染快速路径（同日）

- `render/draw2d/renderer2d.h`：`text()` 在轴对齐且未翻转时走**字符级包围盒 + 逐像素查字模**，不再对每个亮点调一次 `fillUserRectMat`（一个字符 15~20 个亮点 → 15~20 次四角变换 + 裁剪）。旋转/翻转/镜像仍走原路径。
- 实测：`renderer2d_text_5k` **78 → 36–40 ms**，且 `lit` 计数与优化前**逐像素一致**（39744）。
- 新增用例 `Renderer2D.TextScaleAmplifiesGlyphPixels`：与字体内容无关的不变量——scale=2 的亮像素数必须恰好是 scale=1 的 4 倍（244）。

**验证**：构建 0/0；`unit_tests` **244/244**；`render_regression` **3/3**；`renderer_stress` **93/93**；**20** 个离屏示例退出码 0。

### 压测十七轮：动画→蒙皮→渲染端到端（同日）

- 新增 `animation_skinning_pipeline`：三骨骼链 + 四元数旋转动画 → `sampleAnimation` → 写回 `localTransform` → `computeSkinMatrices` → 逐顶点 `skinPosition/skinNormal` → 光栅化。两个时间点的画面哈希不同（lit 108/59），顶点无 NaN。此前采样、骨骼、渲染是分三段测的，这一轮把接缝也盖上了。
- 新增用例 `Skeleton.BindPoseLeavesVerticesUnchanged`：绑定姿态下 `global * inverseBind` 应为单位矩阵、顶点不动。矩阵乘法顺序写反会立即失败（245）。

**验证**：`unit_tests` **245/245**；`render_regression` **3/3**；`renderer_stress` **94/94**（`--quick` 75 个 / 1.8 s，全量 9.3 s）。

### 压测十八轮：Cubemap 面映射 / 2D 状态隔离（同日）

- **覆盖率工具链本地不可用**：`llvm-cov` / `llvm-profdata` / `gcov` / `OpenCppCoverage` 均不在 PATH，VS BuildTools 也未安装 LLVM 工具；本机无法取覆盖率基线，只能依靠 CI 的 coverage job。已记入 ACTIVE 作为环境限制。
- 新增 `cubemap_face_mapping`：六面各涂一色，验证 6 个轴向 + 24 个面内方向都采样到对应面，以及 `generateSkyCubemap` 的天顶/地平线公式。此前 Cubemap 只测过“中心方向不越界”，faceFor 的面选择与 u/v 翻转没有任何用例。实测通过。
- 新增 `renderer2d_state_reset`：`begin()` 必须清掉上一帧的 scissor（否则第二帧被默默截断），空栈 `pop()` / 重复 `end()` 不得崩。实测 scissor 正确清除（16 / 1024 像素）。
- 加固 `texture_lifecycle` 判据：改用“预热 200 张 + 两轮各 500 张、看第二轮增长”代替单次绝对增长。首轮包含分配器保留策略，不是泄漏，原先的绝对阈值在系统内存压力下会间歇误报。

**验证**：`unit_tests` **245/245**；`render_regression` **3/3**；`renderer_stress` **96/96**（`--quick` 77 个 / 1.6–2.0 s，全量 9.9 s）。

### 压测十九轮：节点层级 / 固定次幂 / 示例计数修正（同日）

**性能**

- 新增 `math/scalar.h`：`pow5(x)` = `((x²)²)·x`。PBR 的 Schlick 菲涅尔项（直接光与 IBL 两处）和延迟光照的同一项原来都调 `std::pow(x, 5.0f)`，逐片元两次库调用。
- 实测：`render_1024x1024_x30` **253 → 185–189 ms**；`render_4k_sphere` **161 → 136 ms**。新增用例 `ScalarMath.Pow5MatchesStdPow` 锁定与 `std::pow` 的差异 < 1e-6。

**压测扩展（96 → 97）**

- `gltf_node_hierarchy`：200 级链式 `children` 的 glTF，验证 `convertModel` 的 children 映射、末节点 meshIndex，并用迭代方式累乘世界矩阵（叶子 x = 2.0，误差 < 0.05）。此前只测过平铺节点。

**修正一处文档错误**

- 此前多轮写“21 个离屏示例”。实际 `stage03_sprite_anim` 是窗口程序（`while (pumpMessages())`），虽无 `_live` 后缀但一直被 CI 归入窗口目标。离屏示例真实数量是 **20**，另 3 个窗口示例（stage03 / stage08_shader_live / stage10_shadow_live）只构建、不自动运行。已回头修正这处数字。

**验证**：`unit_tests` **246/246**；`render_regression` **3/3**；`renderer_stress` **97/97**（`--quick` 78 个，全量 7.9 s）；20 个离屏示例退出码 0。

### 压测二十轮：整帧预算 / FXAA 优化 / 文档对齐（同日）

**整帧预算（新增场景）**

- `frame_budget_1080p`：一帧 = 清屏 + PBR 球（2976 三角形）+ 20 个 2D 面板 + 5 条文本 + 全屏 FXAA。
- 首次测量：3D **30 ms**、2D **5 ms**、**FXAA 213 ms**，整帧 252 ms——后处理占 85%，与“3D 是主力”的直觉相反。

**FXAA 优化（数学等价重构）**

- 预计算亮度图：原实现每像素算 9 次亮度（3×3 邻域），而每个亮度又被周围 9 个像素各算一次，整体 9 倍冗余；1080p 下等于 1800 万次 `getPixel + luminance`。
- 低对比度像素先整行拷贝，不再逐像素 `setPixel`；只有确认是边缘后才读 4 个角落；行指针提前算好，y 方向完全不用 clamp。
- 实测 FXAA 1080p（UI 密集画面）：**213 → 156 ms**；PostProcess 20 个用例全过。注：该场景包含大量 UI/文字边缘，真实 3D 画面的边缘更少、会更快。

**文档对齐**（历史 changelog 条目保留为当时快照）：

- `docs/project_context.md`：用例数 218/52 → **246/56**；ASAN 行改为“无断言、无 ASAN 报告”；新增压测探针状态行；可执行目标说明补上 `renderer_stress`。
- `docs/engineering/21`：两处 218/52 → **246/56**，补上压测场景。
- `docs/engineering/58`：顶部声明性能数字为 2026-10-06 快照，当前数据用 `renderer_stress --json` 采集。

**验证**：构建 0/0；`unit_tests` **246/246**；`render_regression` **3/3**；`renderer_stress` **98/98**（7.7 s）。

### 压测二十一轮：ASAN 全量 + 阈值修正（同日）

- **Debug + ASAN 全量 98/98** 跑通，无断言、无 AddressSanitizer 报告（150 s）。
- 过程中发现 `frame_budget_1080p` 有一个硬编码的 `total < 500 ms` 阈值：在 ASAN 下 FXAA 慢 12 倍（整帧 2.5 s）会误报失败。性能断言不应写死绝对时间——已改为“三段都真的跑了 + 输出非全黑”，性能回归交给 `--baseline`（同构建对比同构建）。

**最终 DoD（Release）**：构建 0/0；`unit_tests` **246/246**；`render_regression` **3/3**；`renderer_stress` **98/98**；**20** 个离屏示例退出码 0。

## 2026-10-23

### 2.0.0 分层解耦重构（TASK-014 阶段 2–5）

版本从 1.0.0 升到 **2.0.0**：头文件路径与命名空间按分层重排，**不保留旧路径转发或全局别名**，
属破坏性变更。保留 C++23、全部渲染功能、示例目标名、`practice` 练习流程与数学/绘制头文件式用法。

**目录与命名**

- 公共头唯一入口 `include/softrenderer/`：`math/ core/ resource/ render/{raster,draw2d,pipeline,shader,postprocess}/ scene/ io/ runtime/`。
- 私有实现集中到 `src/io/`（加载器 + 第三方实现单元）；`src/` 下不再有公共头。
- 功能命名空间收敛：`sr::draw2d`、`sr::postprocess`、`sr::colors`、`sr::transform`、`sr::meshgen`、`sr::texgen`、`sr::animation`、`sr::io`、`sr::keyframe`。

**核心数据与绘制解耦**

| 变更 | 说明 |
|---|---|
| `core/image.h` | 从 TGA 头提取与文件格式无关的 `sr::Image`；Texture / blit / 生成器只依赖它，Texture 不再有 `loadFromFile` |
| `render/postprocess/post_processing.h` | ACES / gamma / LDR resolve / brightPass / gaussianBlur / addBlend 从 `FloatFramebuffer` 迁出为自由函数；缓冲只保留存储与 `row()` 访问 |
| `render/pipeline/varyings.h`、`shader.h`、`surface_shader.h` | 插值契约、管线回调、内置 uniforms 三分；内置着色器继承 `SurfaceShader`，clip 只依赖 Varyings |
| `resource/vertex.h` | `Vertex` 从 mesh.h 独立；`render/shader/deferred_lighting.h` 承接 `lightingPass` |
| `math/barycentric.h` | 重心坐标成为纯数学，基础光栅化不再包含整个 2D 模块；`wireTriangle` 归 `sr::draw2d` |
| `scene/camera2d.h` + `Renderer2D::setViewTransform(Mat3)` | render 不再反向依赖 scene |
| Renderer 去掉 `SR_PROFILE_SCOPE` | 绘制头不再引入时钟/线程/锁；计时由调用方用 `sr::ProfileScope` 包围 |
| `resource/mesh_generator.h`、`texture_generator.h` | 程序化生成独立成头 |

**场景、加载与宿主能力**

- 自有场景数据：`sr::SceneAsset`（网格/材质/纹理/节点/蒙皮/动画片段）、`Material`、`SceneNode`、`Skin`、`AnimationClip`；材质用纹理**索引**引用资源，整体可安全移动/复制。
- 动画在加载阶段解码（时间轴、关键帧值、三次插值切线，含长度/类型/索引/有限值/单调性/零四元数校验），运行时 `sr::animation::sampleChannel` / `sampleAnimation` 只做纯数学，**不访问 tinygltf**；Linear/Step/CubicSpline、四元数归一化与 bind pose 语义不变。
- I/O 私有化：`sr::io::loadGltfScene` / `loadGltfMesh` / `loadObjMesh` / `loadImage` / `readTga` / `writeTga` / `writeImage`；实现移入 `src/io/*.cpp`（`gltf_loader.cpp`、`obj_loader.cpp`、`image_loader.cpp`、`image_write.cpp`、`tga.cpp`），第三方头（stb / tinyobj / tinygltf / nlohmann）只在实现单元可见。`gltf_accessor.h`、`gltf_mesh.h`、`convertModel` 为私有实现，内部单测显式获得 `src/io/` 私有 include。
- `runtime/resource_cache.h`：`ResourceCache::load<T>(key, factory)`，工厂返回 `shared_ptr<T>`，失败不入缓存，不再要求 `T::loadFromFile`。
- `runtime/thread_pool.h`：ThreadPool 与 CommandBuffer 分文件；`scene/keyframe_animation.h` 承接原 skeleton 的简化欧拉角动画。

**CMake 与对外集成**

- 组件目标：`SoftRenderer::core`（math/core/resource）、`::render`、`::scene`、`::io`（STATIC）、`::runtime`、可选 `::entt`；`SoftRenderer::softrenderer` 为已启用组件的聚合目标。
- 新选项 `SR_BUILD_IO`、`SR_BUILD_RUNTIME`、`SR_ENABLE_ENTT`（默认 ON）；三者关闭时只配置核心库，**不查找 Threads、不编译加载器**。
- 组件定义集中到 `cmake/targets.cmake`；`find_package(SoftRenderer COMPONENTS core)` 只导入 core 闭包，不查找 Threads。
- 安装只发布 `include/softrenderer/` 公共头 + 组件目标 + 可选 EnTT 头；**不再安装 stb / tinyobj / tinygltf / nlohmann 头**，也不安装 tests/benchmarks/私有实现头。

**验收实测（Windows Release）**

- 全量构建 0 错误 0 警告；可执行目标 25 个（与基线一致）。
- `unit_tests` **179/179** 通过（新增 ResourceCache 失败不缓存、动画已解码片段采样等用例）。
- `render_regression` 3/3 通过，基准图未更新。
- 20 个离屏程序（14 stage + 6 demo）退出码全为 0；窗口示例只构建。
- 架构门禁：每个公共头单独编译（`public_headers_check`）、命名空间冲突哨兵（`namespace_sentinel`）、模块 include 白名单（`architecture_boundaries`）。
- 下游消费：`consumer_subdirectory` / `consumer_installed` 均通过（安装到临时前缀后整体移动再 `find_package`），并新增只请求 core 的 `consumer_core` 场景。
- 组件隔离：`-DSR_BUILD_IO=OFF -DSR_BUILD_RUNTIME=OFF -DSR_ENABLE_ENTT=OFF` 配置与构建通过。

**本轮完成的 2.0.0 收尾（2026-10-23 晚）**

- **全局类名迁移完成（TASK-007）**：27 个仍含全局声明的公共头（`framebuffer.h`、`depthbuffer.h`、
  `blend.h`、`math/{vec,mat3,mat4,quat,rect}.h`、`renderer.h`、`renderer2d.h`、`clip.h`、`render_queue.h`、
  `shader.h`、`surface_shader.h`、`varyings.h`、`triangle.h`、`shaders.h`、`advanced_shaders.h`、
  `pbr_shader.h`、`cubemap.h`、`mesh.h`、`texture.h`、`vertex.h`、`camera.h`、`camera2d.h`、
  `particle_system.h`、`skeleton.h`）全部包进 `namespace sr`；`color.h` 的 `Color`、`log.h` 的
  `sr_log_init/sr_log_shutdown` 一并收敛（改为 `sr::logInit` / `sr::logShutdown`）。
  `namespace raster_fast` → `sr::raster`，与「功能函数进 `sr::<模块>`」的约定一致。
  **未保留任何别名桥**；应用侧统一 `using namespace sr;`。
- **命名空间门禁升级**：`tests/architecture/namespace_sentinel.cpp` 现在先在全局作用域声明
  60 个同名类型（`Framebuffer`/`Mesh`/`Texture`/`Camera`/`Renderer`/`Shader`/`Varyings`/
  `Material`/`SceneNode`/`Position`/`GBuffer`/`Mat4`/`Log` …），再包含**全部**公共头，
  并用 `static_assert` 确认这些类型位于 `sr`。任何回退都会在编译期失败。
- **`test_core.cpp` 按模块拆分完成（TASK-006）**：1468 行的单文件拆成 10 个模块级文件
  （`math_test` / `core_buffer_test` / `resource_test` / `render_pipeline_test` / `draw2d_test` /
  `scene_test` / `ecs_test` / `runtime_test` / `io_test` / `pixel_regression_support_test`），
  与 `tests/unit/` 已有的 `animation/arena/command_buffer/coverage/gltf/renderer` 单测并列。
  ECS 的编译期哨兵类型移入 `namespace ecsCanary`，使测试文件可以用 using-directive。
- **外部消费工程补全**：新增 `tests/integration/consumer_render`（只请求 `SoftRenderer::render`，
  验证核心渲染无需 io/runtime/线程/窗口）；`run_consumer.cmake` 现同时验证聚合、core-only、
  render-only 三类工程在 `add_subdirectory` 与「安装后整体移动前缀再 `find_package`」两种模式下可用，
  并断言安装产物只含公共头与组件（泄露 `tests/`、`benchmarks/`、`stb_/tiny_/nlohmann` 头即失败）。
- **第三方实现单元归位**：三个 `*_IMPLEMENTATION` 宏集中到 `src/io/stb_impl.cpp`、
  `src/io/tinygltf_impl.cpp`、`src/io/tinyobj_impl.cpp`（文件名即实现的库）；
  `third/` 不再放项目编写的包装单元，旧 `third/tinygltf/tiny_gltf_impl.cpp`
  （实际只含 stb 实现）与 `src/io/gltf_loader_impl.cpp` 已删除。
- **新增加载→采样端到端用例**：`tests/unit/gltf_animation_test.cpp` 写一个内嵌 buffer 的
  真实 glTF（三角形 + translation 动画），加载后把 `SceneAsset` 移出结果对象、删掉文件，
  再采样验证线性插值与首末帧钳制——锁定「加载器返回后仍可采样动画」。
- **新增行为与边界门禁**：`tests/unit/postprocess_test.cpp` 用独立算出的期望值锁定 ACES/gamma/
  brightPass/gaussianBlur/addBlend/resolveToLdr 的数值公式（并验证效果函数不修改源缓冲）；
  `check_boundaries.cmake` 增加公共头 include **循环依赖检查**（已用合成 a↔b 用例验证可命中）。
- **实测（Windows Release）**：全量构建 0 错误 0 警告；`unit_tests` **179/179**；
  `render_regression` **3/3**（基准图未更新）；**20 个离屏示例退出码全为 0**；
  可执行目标 **25 个**（与基线一致）；`ctest -L packaging` **3/3**
  （`architecture_boundaries` + `consumer_subdirectory` + `consumer_installed`）。
- **仍未完成**：覆盖率本地实测（本机无 LLVM/gcov 工具链，见 TASK-005/006）与
  macOS/Linux 远端 CI 结果——两者都必须真实跑过才能报告，本轮不宣称通过。

## 2026-10-23

### 与实测状态对齐：覆盖规则、discard 深度与示例调用点（TASK-014 阶段 1）

本轮先实测再修复。重构前基线（`develop` @ `2adfbf6`）：全量构建**失败**——
`stage01_framebuffer` 6 处与 `stage05_transform` 1 处 `drawLine`、`stage03_sprite_anim`
1 处 `fillRect` 未限定命名空间，另有 2 处 C4244 被 `/WX` 视为错误；`unit_tests`
169 用例中 166 通过 3 失败；`render_regression` 3/3 通过。

核实发现：`include/softrenderer/render/pipeline/renderer.h` 在 git 历史中只有初始化提交与一次 include 补齐，
**TASK-009（discard 深度提交）与 TASK-013（Renderer 共用覆盖规则）的修复从未进入该文件**，
与本变更日志和 `ACTIVE.md` 的既有陈述不符。判定以实测为准。

| 修复 | 文件 | 说明 |
|---|---|---|
| 覆盖规则统一 | `include/softrenderer/render/pipeline/renderer.h` | `rasterTriangle` 改用 `sr::TriangleCoverage` 的整数 top-left 规则，替换浮点边函数 + `>=0` 判定；重心坐标由整数边函数值换算，属性插值仍为浮点 |
| 深度提交顺序 | `include/softrenderer/render/pipeline/renderer.h` | 深度改在 `shader.fragment` 返回 `true` 后提交；被 discard 的片元不再占据深度 |
| 示例调用点 | `examples/stage01_framebuffer.cpp`、`examples/stage05_transform.cpp`、`examples/stage03_sprite_anim.cpp` | 补齐 `draw2d::` 限定；`fillCircle` 的 float 形参显式转换 |

验证：Windows Release 全量构建 0 错误 0 警告；`unit_tests` 169/169；`render_regression` 3/3
（基准图未更新）；20 个离屏程序退出码全为 0。

文档：新增 [ADR-004](adr/ADR-004-module-boundaries.md)（模块边界与 2.0 兼容性决策）；
新增父任务 [TASK-014](tasks/TASK-014-分层解耦重构.md) 与子任务 TASK-015~018；
补齐缺失的 [docs/tasks/README.md](tasks/README.md) 索引。

## 2026-10-06

### 子像素覆盖、MSAA 修复与主管线基线（TASK-013）

- 共享 sr::TriangleCoverage：8 位子像素、int64 边式及 y 向上的 top-left 规则，消除相同共享边的重复覆盖；检查非有限、退化和坐标范围。
- Renderer、基础三角形与标量/SSE2 共用覆盖规则，保持浮点属性插值；背面先剔除并优化取整/包围盒初始化。
- 提取 sr::MSAAFlatRenderer<1/4>，仅向覆盖且深度通过的样本提交颜色。MSAAFramebuffer 拒绝无效尺寸、容量溢出与非法样本编号。
- 新增可选 SR_BUILD_BENCHMARKS 与 renderer_benchmark，固定场景输出中位数、p95和哈希；不承诺所有场景加速。
- 新增 14 项测试；Release 169 单测、3 像素场景和两个消费工程通过；Debug ASAN 通过（2 项既有用例跳过）。相关三个离屏示例通过，基准图未修改。
- 更新 ADR-003、覆盖/MSAA/性能教程、架构、模块、构建开关、状态和任务档案。远端 Sanitizer 与跨平台验证仍待执行。

## 2026-10-05

### 19:30 第二轮：资源读取与任务失败（TASK-011/012）

- 新增 sr::gltf::AccessorView / readMeshPrimitive / convertModel，统一字节范围、类型、stride、归一化和索引校验，加载失败不发布部分资源。
- 动画实现正确 CubicSpline Hermite 插值，修复失败输出未初始化，保留 bind pose 并传递 channel 标志；移除不安全的 accessorFloats，sampleChannel 改为 bool。公共版本仍为 1.0.0，源码兼容迁移见 ADR-002。
- 修复 tinygltf 已解码像素的二次解码错误；拒绝不支持的像素布局；蒙皮示例通过共享视图读取列主序逆绑定矩阵。
- ThreadPool 增加 submitFuture；submit 异常由 waitAll 报告；回收部分启动线程、排空析构和防止 worker 自等待。并行命令收集错误并等待全部回调结束。
- 新增独立 glTF、动画、线程测试，增加 25 项单测；Ubuntu 独立 TSAN 作业已配置、未远端执行。
- 更新加载/纹理/动画/线程教程、架构依赖、支持范围、当前状态与任务档案。

验证：Release 155 单测、3 像素场景和两个消费工程通过；Debug ASAN 153 单测通过、2 项按约定跳过；三个相关 demo 成功输出。线程创建失败只审查清理路径，未作 OS 故障注入；远端 TSAN/UBSAN 和覆盖率仍待执行。

### 运行时正确性、内存复用与库集成（TASK-008/009/010）

- Renderer 仅在 fragment 接受片元后写入深度，修复 discard 镂空区域错误遮挡；不改变既有图像基准。
- Arena 增加对齐/长度/乘法校验、RAII 块所有权；reset 为 O(1) 并复用扩展块，新增 capacity() 观测保留容量。峰值内存保留到析构。
- 根 CMake 拆出 examples/practice/tests；新增 SR_BUILD_EXAMPLES、SR_BUILD_PRACTICE、SR_BUILD_TESTS，作为依赖时默认关闭。
- 修复子工程路径、版本宏、EnTT 安装 include 层级、tinygltf/json 漏装和线程传递依赖；支持相对自定义安装目录。
- 新增独立 Arena/Renderer 单测和两种真实下游工程验证；修复 Debug /WX 的 GTEST_SKIP 不可达代码问题；新增 Ubuntu ASAN+UBSAN CI。
- 覆盖率提取限定仓库根下的 src，排除第三方库内同名 src 目录。
- 统一 README、当前状态、任务入口、架构及构建/分配器文档。

本地验证：Release 全量 25 个程序；130 个单元测试与 3 个像素场景通过；源码引入、安装前缀迁移的消费工程通过。Debug MSVC ASAN 下 128 单测通过、2 个 Release-only 用例跳过，3 个像素场景通过。本轮没有远端 Linux/macOS、UBSAN、TSAN、覆盖率或静态分析的运行结论。

## 2026-09-07

### 零警告强制与 clang-tidy 配置（TASK-005c 部分）

| 变更 | 影响范围 |
|---|---|
| `sr_dev_flags` 加 `/WX`（MSVC）与 `-Werror`（GCC/Clang） | `CMakeLists.txt` |
| 新增 `.clang-tidy`：启用五组检查，对 12 项已知会在本代码库误报的逐条写明禁用理由 | `.clang-tidy` |
| 新增 `third/.clang-tidy`（`Checks: '-*'`）屏蔽 720 个 vendored 文件 | `third/.clang-tidy` |
| `cpp_project_rules.md` 增加 snake_case 文件命名例外条款 | `.qoder/rules/` |

开启 `/WX` 的依据是实测：多次全量构建在 `/W4` 下已是零警告，所以它只是把
已有约定变成构建期强制。实测确认 `unit_tests.vcxproj` 的 Debug 与 Release
两套配置均含 `<TreatWarningAsError>true</TreatWarningAsError>` +
`<WarningLevel>Level4</WarningLevel>`，且开启后全量重编零错误。

作用范围限缩：`sr_dev_flags` 只被本仓示例 / 练习 / 测试目标链接，因此
`third_party_impl`、googletest、spdlog 与**下游集成方均不受影响**。

> `clang-tidy` 本身仍未安装，配置**未经实跑调参**，已在文件头显式标注。
> 全仓 `clang-format` 同样待工具就位后单独 commit 执行。

### 全局命名空间收敛第 2 批：core 高级缓冲（TASK-007 / ADR-001）

**不兼容变更（API Break）**：以下符号需加 `sr::` 前缀：
`SSAO`、`SSAOConfig`、`GBuffer`、`FloatFramebuffer`、`MSAAFramebuffer`、
`kMSAA4Offsets`、`CommandBuffer`、`ThreadPool`。

全局类型数 **52 → 45**（TASK-007 累计 60 → 45）。均**不留全局别名**。

顺带修复两个真实缺陷：

| 缺陷 | 说明 |
|---|---|
| `msaa_framebuffer.h` 用 `std::pair` 却未 include `<utility>` | 与 TASK-004 发现的 `clip.h`/`renderer.h` 同类：MSVC 靠传递包含侥幸通过，libstdc++/libc++ 不保证 |
| `command_buffer.h` 的 `ThreadPool::stop_` **未初始化** | 它是线程池退出标志，工作线程构造后立即读取。未初始化值叠加多线程 → **难重现的偶发挂死或提前退出**，是最难查的一类缺陷 |

### 实测发现：测试文件不能用 `using namespace sr;`

原以为全局哨兵声明会遮蔽 using-directive 引入的名字，实测错误：

```
tests\test_core.cpp(1181,5): error C2872: “Position”: 不明确的符号
```

1. using-directive 把 `sr` 的成员当作**最近公共外围命名空间**（此处即全局）
   的成员参与查找，与真正的全局声明**同级** → 二义性，而非遮蔽
2. gtest 的 `TEST(GBuffer, ...)` 会创建名为 `GBuffer` 的**命名空间**，
   与 `sr::GBuffer` 类同名也冲突

因此 `tests/test_core.cpp` 统一写显式 `sr::` 限定；示例（无哨兵、无 gtest 宏）
则可用 `using namespace sr;`，已用于 `demo_stars.cpp` /
`stage11_postprocess.cpp` / `stage9_msaa.cpp`。

> 后续若想恢复测试文件的 using-directive 便利，应先把编译期哨兵拆到单独文件。
> 而这正好是 TASK-006 测试目录拆分的一部分，**两者宜合并执行**。

### 全量引用面实测数据已入库

52 个剩余类型的引用次数 / 文件数已逐项实测并记入
`docs/tasks/TASK-007-全局命名空间收敛.md`，作为后续分批的直接依据。

关键结论：**`math/` 的低文件数是假象** —— `Vec3` 只出现于 1 个文件，
但全仓实际用的是别名 `Vec3f`（未被统计）。`Color` 已达 400 次 / 49 文件，
`Vec3f` 只会更多。因此 `math/` 应归入高引用面、放到最后。

---

### 全局命名空间收敛第 1 批：ECS 组件（TASK-007 / ADR-001）

**不兼容变更（API Break）**：8 个 ECS 组件类型从全局命名空间消失，
现需写 `sr::` 前缀。

| 旧（全局） | 新 |
|---|---|
| `Position` | `sr::Position` |
| `Velocity` | `sr::Velocity` |
| `Rotation` | `sr::Rotation` |
| `Scale` | `sr::Scale` |
| `MeshRef` | `sr::MeshRef` |
| `MaterialRef` | `sr::MaterialRef` |
| `Light` | `sr::Light` |
| `CameraTag` | `sr::CameraTag` |

迁移指引：`registry.emplace<Position>(e, ...)` → `registry.emplace<sr::Position>(e, ...)`，
`registry.view<Position, Velocity>()` → `registry.view<sr::Position, sr::Velocity>()`。
成员字段与语义未变（`MeshRef::meshIndex` 仍为 `-1` 表示未设置）。

#### 为何现在就做、且不留全局别名

先立了 [`ADR-001`](adr/ADR-001-namespace-convergence.md)，其中推翻了 TASK-007
原计划的「全量别名桥」方案。关键认识：

> `using sr::Position;` 仍然把 `Position` 注入全局命名空间，下游照样撞名。
> **别名桥不解决撞名问题**，它只是让大改动可以分批提交的工具，
> 不是对下游的兼容承诺。因此对高冲突名字不应使用别名桥。

改为**按引用面分类**：实测发现引用面最小、冲突风险最高的正是这 8 个 ECS 组件：

| 实测项 | 结果 |
|---|---|
| 引用 `scene/components.h` 的文件 | **恰好 2 个**（自身 + `tests/test_core.cpp`） |
| `Light` 除定义处外的使用次数 | **0** |
| 使用 ECS 的示例（共 22 个） | **0 个** |

所以可以一次完整迁完，立即获得真实的撞名消除，而不是只做准备工作。

#### 新增

| 文件 | 内容 |
|---|---|
| `docs/adr/README.md` | ADR 索引 + 5 个待立 ADR 的已知决策点 |
| `docs/adr/ADR-001-namespace-convergence.md` | 4 个备选方案对比、选定理由、失效条件 |
| `include/softrenderer/core/namespace.h` | `namespace sr` 的约定锚点与迁移进度文档 |

#### 回归防护：编译期哨兵

`tests/test_core.cpp` 在**全局作用域**声明了 8 个同名哨兵类型：

```cpp
struct Position { int canary = 0; };
struct Light    { int canary = 0; };
// … 共 8 个
```

这能编译通过本身就证明了目标达成：下游使用者现在**可以**定义自己的
`Position` / `Light` 而不与本库冲突。若将来有人加回 `using sr::Position;`，
测试文件会直接报**重定义编译错误** —— 把 ADR-001 的核心结论用编译器锁定，
而不只写在文档里。

另新增用例 `ECS.globalCanaryTypesAreDistinctFromSrComponents`，用
`std::is_same` 断言哨兵类型与 `sr::` 组件确实是不同类型。

#### 验证

| 验证项 | 结果 |
|---|---|
| 全局类型数扫描 | **60 → 52**，8 个 ECS 名字均已不在全局作用域 |
| Release 全量构建 | 零错误零警告，退出码 0 |
| 单元测试 | 120 → **121 全绿** |
| 像素回归 | 3 个场景仍匹配基准图（证明迁移未改变任何渲染输出） |
| 抽样 11 个示例 | 全部正常退出 |
| exe 清单 | **25 个**，与基线一致 |

#### 版本号处理

本次已构成 API Break，但**暂不升 2.0.0**。理由：项目从未发布过任何 tag、
无外部使用者、`VERSION 1.0.0` 仅用于生成版本头。待 52 个类型全部迁完、
真正对外发布时一次性升版，避免为每批迁移反复改版本号。已在 ADR-001 记录。

---

## 2026-09-05

### 覆盖率接入与测试规则例外（TASK-006 部分完成）

| 变更 | 影响范围 |
|---|---|
| `ci.yml` 新增独立 `coverage` job（ubuntu + gcov + lcov + genhtml） | `.github/workflows/ci.yml` |
| 只统计 `src/`（`lcov --extract "*/src/*"`），排除系统头 / 第三方 / 测试代码本身 | 同上 |
| `.gitignore` 补 `build-*/` 与 `coverage*.info` | `.gitignore` |
| `testing_rules.md` 新增两条项目例外条款 | `.qoder/rules/testing_rules.md` |

**门禁策略：首次只报告不设阈值。**

`testing_rules.md` 要求行覆盖率 ≥ 80%、分支 ≥ 60%，但在实测基线未知的情况下
直接设门禁极可能立即红灯，违反「禁止带红灯合并」而使 CI 失去意义。
先把基线量出来写进本文件，下一轮再启用棘轮式门禁（不允许下降）。

**为何覆盖率 job 不与 Sanitizer 共用构建**：ASAN 与 gcov 同时开会相互干扰，
且目标不同（一个查内存错误、一个量覆盖度）。

**未验收**：覆盖率 job 需 Linux 环境，本地无法验证，验收依赖远端 CI 首次运行。
已知风险：`lcov` 各版本的 `--ignore-errors` 可接受值不同（lcov 2.x 对 unused /
empty / inconsistent 默认报错退出），已预防性列全，首次运行可能仍需微调。
因为本 job 不设阈值、不阻塞合并，即使需微调也不会让 CI 失去意义。

#### 新增的两条规则例外条款

1. **存量用例不批量重命名**：120 个用例采用 `TEST(模块名, 场景描述)` 的
PascalCase 命名，不符合规定的 `被测方法_场景_预期结果` 格式，但不属于禁止的
无意义命名。批量重命名会改变 ctest 测试名、使 CI 历史失去可比性，属低价值高扰动。
**新增用例强制用新格式**（已执行）。

2. **插值类测试的数据要求**：输入必须让各顶点取值互不相等，且采样点不得取
对称盲点。源于 TASK-002 的真实教训：顶点深度全相等时任何权重排列都算出
同一个值，缺陷被 100% 掩盖。同时记录了「一致性测试不能替代正确性测试」。

#### 测试目录拆分：已推迟

原计划把 `tests/test_core.cpp`（1300+ 行 / 120 用例 / 33 套件）拆为
`tests/unit/<模块>/<类名>_test.cpp`。**本次推迟**，理由：

环境阻止删除文件（TASK-004 中 `DeleteFile` 连续两次报成功而文件仍在）。
拆分的最后一步必然是删除原文件；若无法删除，它只能被清空成惰性桩
（否则 120 个 `TEST()` 与拆分后的文件重复定义，直接链接失败），
结果是仓库里同时存在「桩文件 + 30 个拆分文件」，**比拆分前更乱**。

拆分是纯机械操作，收益是目录合规 + include 面收敛，**无功能价值且无时间敏感性**；
而留下桩文件的代价是持续的可读性损害。完整拆分程序已写入
`docs/tasks/TASK-006-测试目录重构与覆盖率.md`，**执行前必须先确认能删除文件**。

---

### 构建系统现代化（TASK-005 第一、二节，已完成）

#### target-based 改造

消除 `cpp_project_rules.md`「构建系统规范」明令禁止的全局指令：

| 变更 | 影响范围 |
|---|---|
| 删除 **6 处** `include_directories()` | `CMakeLists.txt` |
| 删除 `link_libraries(softrenderer)` | 同上 |
| 删除 `add_compile_options(/utf-8 /W4)` 与两处 `add_compile_definitions` | 同上 |
| 新增 `sr_add_example()` / `sr_add_realtime_example()` 函数，22 个示例改为调用 | 同上 |
| 新增 `sr_dev_flags` INTERFACE 库（项目内部开发标志，**不导出**） | 同上 |
| 删除 3 处冗余的 `target_link_libraries(... third_party_impl)` | 同上 |

**关键发现**：那 6 处 `include_directories()` 与 `softrenderer` 的 INTERFACE include、
`third_party_impl` 的 PUBLIC include **完全重复** —— 所以是直接删除而非搬迁，
语义零变化。改造后残留全局指令 **0 处**，`target_*` 调用 27 处。

**「公共接口要求」与「开发偏好」的划分**：

| 标志 | 位置 | 理由 |
|---|---|---|
| include 路径、`cxx_std_23`、`/utf-8`、`_CRT_SECURE_NO_WARNINGS` | `softrenderer` INTERFACE（导出） | 下游编译本项目头文件时真正必需 |
| `/W4`、`-Wall -Wextra`、`NOMINMAX`、`WIN32_LEAN_AND_MEAN` | `sr_dev_flags`（**不导出**） | 本项目的开发偏好，不应强加给集成方 |

> `/utf-8` 与 `_CRT_SECURE_NO_WARNINGS` 属接口要求而非偏好：前者因为 `src/`
> 头文件含大量中文注释（不开报 C4819）；后者因为 `core/tga.h` 在**头文件内**
> 直接调 `std::fopen`，该调用在使用方的编译单元展开，MSVC 会在**使用方**报 C4996。
>
> 后者是个妥协（会抑制下游自己的 C4996），根治办法是把 `tga.h` 的 I/O
> 改成不依赖弃用函数，已记为待办。

#### Sanitizer

新增 `option(SR_ENABLE_SANITIZERS ... OFF)`，满足「Debug 必开 ASAN/UBSAN、
Release 禁止开启」的要求。此前项目**没有任何 Sanitizer 配置**，而 TASK-003 修的
整数溢出 UB 与越界读正是 ASAN/UBSAN 能自动抓到的类型。

三个关键设计选择：

1. **用 `$<$<CONFIG:Debug>:...>` 而非 `if(CMAKE_BUILD_TYPE STREQUAL "Debug")`**：
   MSVC 是多配置生成器，`CMAKE_BUILD_TYPE` 为空，一次配置产出两套产物。
   只有生成器表达式能把标志精确限定到 Debug。
2. **用全局 `add_compile_options` 而非 per-target**：Sanitizer 是 **ABI 级**设置。
   实测证据：只加到项目目标上时，googletest（`add_subdirectory` 引入）未开 ASAN，
   链接报 **LNK2038** `annotate_string` / `annotate_vector` 不匹配 + LNK1319。
3. **整个块移到所有 `add_subdirectory` 之前**：`add_compile_options` 改目录属性，
   只对其后的目标与子目录生效；`third/log`（spdlog）在第 19 行就引入了。

第 2、3 点是 `engineering_rules.md`「禁止全局指令」的**有意例外**，
例外理由已写在 `CMakeLists.txt` 对应位置的注释里。

**实测验证**：

| 验证项 | 结果 |
|---|---|
| Debug 构建（ASAN） | 零错误（LNK2038 已消除） |
| Debug 运行 | **118 passed / 2 skipped / 0 failed**，**无 AddressSanitizer 报错** |
| 2 个 skipped | 正是两个越界测试（Debug 下 `assert` 生效→`GTEST_SKIP()`），与 TASK-003 设计一致 |
| **Release 是否带 Sanitizer** | Debug exe 5,087,744 B / 导入 ASAN DLL = **True**；Release exe 847,360 B / = **False** |
| 改造后 Release 全量构建 | 零错误零警告，**25 个 exe 与基线逐项一致** |
| 改造后测试 | 121 项 ctest 全绿 / 12.48 秒；120 单元 + 3 回归全绿 |
| 20 个离屏示例 | 全部正常退出 |

最后两行是本节的关键验收：体积相差 6 倍且只有 Debug 导入 ASAN 运行时，
机械化地证明了「Release 禁止开启 Sanitizer」而不是靠假设。

**阻塞项**：第三节（`/WX` + `.clang-tidy`）与第四节（全仓 clang-format）需先安装
LLVM 工具链（实测 `clang-format` / `clang-tidy` 均未安装），本次未做。

#### 文档同步

- `docs/troubleshooting.md`：新增两个 Sanitizer 坑——退出码 `0xC0000135`
  （ASAN 运行时 DLL 不在 PATH）与 LNK2038（ABI 不一致）
- `engineering_rules.md`「命令」节：更新为 120 用例 / `-LE fast` / `-L regression` /
  `-L fast` 四种跑法，新增 Sanitizer 命令段，DoD 最小验证集补上 `render_regression`
  与「CMakeLists 改动需比对 25 个 exe 基线」

---

### CI 跨平台修复与像素回归接线（TASK-004，代码完成）

单元测试 113 → **120**；新增集成回归目标 `render_regression`（3 个场景）；
全量 `ctest` 从 **39.89 秒 → 14.06 秒**；Windows 上 24 个 exe 全部产出。

#### CI 修复

| 变更 | 影响范围 |
|---|---|
| **修复 ubuntu / macos job 必然编译失败**：给 `stage1_sprite_anim` / `stage6_realtime` / `stage8_realtime` 的整个 `add_executable` 加 `if(WIN32)` 守卫（旧写法只守住了 `target_link_libraries`，没守住编译） | `CMakeLists.txt` |
| **去掉 smoke test 末尾的 `\|\| true`**：该写法让步骤永远不会失败，属典型的掩盖症状 | `.github/workflows/ci.yml` |
| smoke 清单 11 项 → 20 项（补 `stage12_threaded`、`stage1_sprite`、5 个 demo、`practice_hello_2d`） | 同上 |
| 新增「至少跑到 15 个」下限断言，防止「一个都没找到」也绿灯 | 同上 |
| 新增 **Verify platform guards** 步骤：把平台守卫修复本身变成回归测试 | 同上 |
| `ctest` 加 `--parallel -LE fast` | 同上 |

**守卫完备性的验证方式**：全仓扫描确认 `src/` 与 `tests/` 的 Win32 引用数为 **0**，
Win32 依赖只存在于三个已守卫的示例 + `platform/win32_window.h` 本身。
这比用 `if(FALSE)` 模拟更有说服力：证明的不是「`if(WIN32)` 语法有效」，
而是「没有遗漏的第四个文件」。

#### 像素回归接线

`pixel_regression.h` 与 `tests/golden/` 此前零引用、目录为空，能力未落地。
现已接线，并修复了**两个会导致永远假通过**的陷阱：

| 陷阱 | 旧行为 | 修复 |
|---|---|---|
| 硬编码相对路径 `"tests/golden/"` | `ctest --test-dir build` 的工作目录是 `build/`，解析成 `build/tests/golden/` → 找不到基准图 | 新增 `goldenDir()` / `goldenPath()`，由 CMake 用 `file(TO_CMAKE_PATH)` 注入**绝对路径** `SR_GOLDEN_DIR` |
| 基准图不存在时自动创建并返回 `passed=true` | 叠加上条 → **测试永远绿灯**，比没有测试更危险 | 改为 `SR_UPDATE_GOLDEN` 开关控制；默认（CI）下缺失**直接失败**并给出绝对路径与修复指引 |

**反向验证已做**：把一张基准图重命名走后，测试输出
`[REGRESSION] MISSING golden image: <绝对路径>` 并 **FAILED**（退出码 -1）。
并从 `build/` 目录运行验证了绝对路径注入确实绕开工作目录。

三个回归场景（均确定性、无随机、无时间依赖，128×96）：

| 场景 | 锁定什么 |
|---|---|
| `zbuffer_twoOverlappingCubes` | 深度测试 + 背面剔除 + 视锥裁剪的联合行为 |
| `pipeline_phongTexturedSphere` | 变换链 + 透视校正插值 + 纹理采样 + Phong 着色 |
| `blend2d_alphaComposition` | alpha 混合（三种模式）+ draw2d 图元 + 变换栈 + scissor + 点阵字体 |

刻意**不**回归 `demo_stars`（含随机星点）与 `stage12_threaded`（线程顺序不定）。

另新增 `PixelRegression` 单元套件（7 个用例）验证 `compare()` 本身的判定逻辑：
若它恒返回 true，三个集成场景就全是假通过。其中
`compare_uniformSmallDrift_failsOnAvgDelta` 验证「平均差 < 0.5」这个条件能**单独**触发失败
（全图每像素只差 1 时，无任何像素超单像素阈值，但系统性偏移仍必须被拦下）。

#### ctest 项数与标签

全量 `ctest` 共 **122 项** = 120（discover 展开）+ 1（`unit_tests_all`，`fast` 标签）
+ 1（`render_regression`，`regression` 标签）。

| 命令 | 项数 | 耗时 | 用途 |
|---|---|---|---|
| `ctest -L fast` | 1 | 0.43 秒 | 本地开发迭代 |
| `ctest -L regression` | 1 | 0.59 秒 | 只跑像素回归 |
| `ctest --parallel -LE fast` | 121 | 14.06 秒 | **CI 用**（排除与 discover 项重复的 `unit_tests_all`） |
| `ctest`（串行、无标签） | 122 | ~40 秒 | 不推荐 |

#### 待用户手动完成

| 项 | 原因 |
|---|---|
| 删除 `src/test/test_framework.h`（96 行自研 `minitest`，零引用） | 删除工具被环境阻断，连续两次报成功而文件仍在。属无害死代码，不影响构建 |
| 删除空目录 `src/platform/` | 同上（且 git 不跟踪空目录） |
| 推送到 GitHub 并确认三平台 job 全绿 | 需用户提供远端仓库；本地无法验证 Linux/macOS 构建 |

---

### 外部输入校验与边界安全（TASK-003，已完成）

单元测试 97 → **113**，全量构建**零错误零警告**（修前有 2 个 C4840），
20 个离屏示例全部正常。

#### 计划内修复

| 变更 | 影响范围 |
|---|---|
| **修复** `image_loader.h` 有符号整数溢出 UB：`for (int i = 0; i < w * h; ++i)` → `size_t` | `include/softrenderer/io/image_loader.h` |
| 新增尺寸校验与具名常量 `kMaxImageDimension = 16384`；变量声明即初始化 | 同上 |
| 新增 **RAII 释放**（`unique_ptr` + 自定义删除器）——新增的校验分支提前 return 会泄露图像缓冲 | 同上 |
| **修复** `Framebuffer::getPixel` 在 Release 下越界读 UB：越界返回 `colors::black` | `include/softrenderer/core/framebuffer.h` |
| **修复** `DepthBuffer::get` 越界读 UB（旧版连 `assert` 都没有）：返回 `kFar` | `include/softrenderer/core/depthbuffer.h` |
| **修复** `blend.h` alpha 混合截断而非四舍五入（每次混合系统性偏暗 ≤ 1/255）；新增 `toUint8Round` 统一钳制+取整 | `include/softrenderer/core/blend.h` |
| `blend.h` 补 `#include <algorithm>`（原依赖传递包含） | 同上 |
| 5 个枚举合规：`ClipPlane` 裸 enum → `enum class : uint8_t`；`BlendMode`/`AlphaMode`/`Path`/`Interpolation`/`Filter` 补底层类型 | `clip.h`、`blend.h`、`gltf_types.h`、`texture.h` |
| `clip.h` 新增 `static_assert` 防止 `kClipPlaneCount` 与枚举项数漂移 | `include/softrenderer/render/pipeline/clip.h` |

#### 实施中新发现并修复

| 变更 | 发现方式 |
|---|---|
| **`core/log.h` 内置模式重写为变参模板**，消除 4 个缺陷：① 非 POD 类型经 C 可变参传递是 UB；② `{}` 占位符**从不被替换**（格式串按 printf 解析）；③ 日志文本里的字面 `%` 被 `vfprintf` 当转换符解析 → UB；④ `levelName` 的常量数组**漏了 `Off`**（枚举 7 值 / 数组 6 项）→ 越界读 | 给 `image_loader.h` 加日志时构建报出 2 个 **C4840** |
| 新增可测试入口 `sr::formatLog`（从 `Log::log` 拆出），使格式化逻辑可直接断言而无需捕获 stderr | `engineering_rules.md`「代码必须保留可测试入口」 |
| **`gltf_types.h` 的 `Channel::path` 补默认值**：`gltf_loader.h:327-332` 只在 `target_path` 为三个已知值时赋值，而 glTF 2.0 允许 `"weights"` 与扩展值 → 旧代码是不确定值，读取即 UB。另 4 个 int 字段做防御性初始化（统一 `-1` 哨兵） | 改枚举时通读同文件 |
| **三处注释声称不存在的文件**：`image_loader.h:12`（`third/stb/stb_image_impl.cpp`）、`image_write.h:16`（`third/stb/stb_image_write_impl.cpp`）、`CMakeLists.txt:22-24`（「一个 .cpp 同时提供 stb + tinygltf」）均已修正 | 按「修改文件时若头注释过时须同步更新」核查 |

#### 第三方实现单元归属的真相（与文件名直觉相反）

| 实现单元 | `#define` | 提供的符号 |
|---|---|---|
| `third/tinygltf/tiny_gltf_impl.cpp` | `STB_IMAGE_IMPLEMENTATION`、`STB_IMAGE_WRITE_IMPLEMENTATION` | **stb_image + stb_image_write**（不含 tinygltf） |
| `src/scene/gltf_loader_impl.cpp` | `TINYGLTF_IMPLEMENTATION` | **tinygltf** |
| `third/tiny_obj_loader_impl.cpp` | `TINYOBJLOADER_IMPLEMENTATION` | **tinyobjloader** |

> 教训：本次先轻信了 `CMakeLists.txt` 的旧注释并写进了 `third/README.md` 与
> `docs/architecture.md`，读了 `.cpp` 后才发现错误，已全部修正。
> **结论：涉及第三方实现归属时必须读源文件，不能依赖文件名或既有注释推断。**

#### 新增测试（16 个）

| 套件 | 用例 |
|---|---|
| `Framebuffer`（新） | `framebuffer_getPixel_outOfBounds_returnsSentinel` |
| `DepthBuffer` | `depthBuffer_get_outOfBounds_returnsFar` |
| `Blend` | `toUint8Round_roundsAndClamps`、`blendPixels_alphaBlendRoundsInsteadOfTruncating`、`blendPixels_allModesNeverWrapAround` |
| `ImageLoader`（新） | `loadImage_missingFile_returnsFalse`、`loadImage_emptyPath_returnsFalse`、`loadImage_roundTripPreservesPixelsAndOrientation` |
| `Log`（新） | `formatLog_noArgs_returnsFormatString`、`formatLog_substitutesBracesInOrder`、`formatLog_acceptsNonPodTypes`、`formatLog_extraArgsAreAppendedNotDropped`、`formatLog_extraBracesAreKeptLiteral`、`formatLog_literalPercentIsSafe`、`levelName_coversAllEnumValues`、`setLevel_roundTrips` |

> `loadImage` 此前**全仓零调用方**，往返测试是它第一次被真正执行。
> 往返测试写系统临时目录而非仓库，避开提交二进制 fixture。

#### 公共接口影响

| 变更 | 兼容性 |
|---|---|
| `enum ClipPlane` → `enum class ClipPlane : uint8_t` | **API Break**，但实测仅 `clip.h` 内部使用，无外部引用点 |
| `kClipPlaneCount` 由枚举项改为 `static constexpr int` | 同上，仅内部使用 |
| `BlendMode` / `AlphaMode` / `Path` / `Interpolation` / `Filter` 补底层类型 | 兼容（枚举值与用法不变） |
| `sr::Log::log` 由 C 可变参改为变参模板 | 兼容（调用语法不变，且现在接受更多类型） |
| `blendPixels` 结果可能 **+1** | **行为变更**：四舍五入代替截断。影响所有使用 alpha 混合的输出图像 |
| `sr::formatLog`、`sr::levelName`、`toUint8Round` | 新增公共符号（均在全局或 `namespace sr`） |

> ⚠️ `blendPixels` 的行为变更会使**未来的像素回归基准图**与修复前不同。
> TASK-004 接线像素回归时必须以**修复后**的输出为基准。

---

### 光栅化器缺陷修复（TASK-002，已完成）

commit `73122d3`，`--no-ff` 合入 `develop`。

| 变更 | 影响范围 |
|---|---|
| **修复** `rasterizer_fast.h` 深度插值的重心权重错位（同序映射 → 错位映射），标量版 + SSE2 版共 6 处 | `include/softrenderer/render/raster/rasterizer_fast.h` |
| **修复** `setupEdges` 不做符号归一化导致顺时针三角形被静默丢弃 | `include/softrenderer/render/raster/rasterizer_fast.h` |
| 改写文件头注释，标注本模块为**实验性**（未接入 `Renderer` 主链路）及两个原因 | `include/softrenderer/render/raster/rasterizer_fast.h` |
| **新增 3 个回归测试**，单元测试 94 → 97 | `tests/test_core.cpp` |
| 校正 `docs/engineering/54` 全部五节数据漂移，新增「实验性能力」分区 | 文档 |

**修复前实测误差**：

| 采样点 | 期望深度 | 实际深度 | 误差 |
|---|---|---|---|
| 靠近 v0（w0=0.865） | 0.17917 | 0.50417 | **0.325** |
| 靠近 v1 | 0.49583 | 0.82083 | **0.325** |
| 靠近 v2 | 0.82083 | 0.19583 | **0.625** |
| 顺时针绕序 | 与逆时针一致 | `fb` 全黑、`zb` 恒为 `1e9` | **三角形完全丢弃** |

**为何长期未被发现**：原有两个 `RasterizerFast` 用例传入的深度全为
`0.5f / 0.5f / 0.5f`。顶点深度全相等时任何权重排列都算出同一个值，
缺陷被 100% 掩盖。已据此在 `engineering_rules.md` 新增红线：
「禁止用三个顶点取值全相同的数据写插值类测试」。

**公共接口影响**：无。`fillTriangleFast` / `fillTriangleIncremental` / `fillTriangleSSE2`
签名未变，仅修正内部计算与新增绕序容错。

---

### 工程基线建设（TASK-001）

| 变更 | 影响范围 |
|---|---|
| 初始化 git 仓库于 `develop` 分支，首次导入 891 个文件 | 全仓 |
| 新增 `.gitignore`，覆盖构建产物 / IDE / 测试输出 / 第三方构建产物 / 临时文件五类 | 全仓 |
| 新增 `output/.gitkeep`：所有示例以相对路径 `output/xxx` 写出结果，目录缺失会静默失败 | examples/、practice/ |
| 新增根 `README.md`（项目简介 / 特性 / 依赖 / 构建 / 快速启动 / 目录结构） | 文档 |
| 新增 `docs/architecture.md`（分层结构、模块依赖明细、数据流时序图、已知权衡） | 文档 |
| 新增 `docs/modules.md`（6 个 `src/` 子模块 + `platform/` 的职责与扩展点） | 文档 |
| 新增 `docs/changelog.md`、`docs/troubleshooting.md`、`docs/tasks/` | 文档 |
| 新增 `third/README.md`：8 个依赖的版本、许可证与**源码实测证据** | 文档 |
| 填写 `.qoder/rules/engineering_rules.md` 的「红线约束」与「命令」两节（原为模板占位符） | 规则 |

**实测校正的数据**（原 `docs/engineering/54-项目当前状态总览.md` 与实际不符）：

| 项 | 文档原值 | 实测值 | 证据 |
|---|---|---|---|
| GoogleTest 版本 | 1.15 | **1.18.0** | `third/googletest-main/CMakeLists.txt:7` |
| stb_image 版本 | latest | **2.30** | `third/stb/stb_image.h:1` |
| stb_image_write 版本 | latest | **1.16** | `third/stb/stb_image_write.h:1` |
| tinygltf 版本 | 2.9.3 | **v2.9.\***（头文件未锁定修订号） | `third/tinygltf/tiny_gltf.h:28` |
| `src/` 头文件数 | 35 | **52** | 实测统计 |
| 示例程序数 | 20 | **22** | 实测统计 |
| 文档篇数 | 54 | **72** | 实测统计 |
| 单元测试 | 62 用例 / 22 套件 | **94 用例 / 29 套件** | `unit_tests.exe` 实际运行输出 |
| C++ 标准 | 「`src/` 纯 C++17 标准库」 | **C++23**（`vec.h` 用 `requires` 约束） | `CMakeLists.txt:6` |

**新发现的架构事实**（原无处记录）：

- `third/entt/` 下存在**两份版本不同的 EnTT**：`src/` 是 3.14.0（构建实际使用），
  `single_include/` 是陈旧的 3.13.2
- `raster::fillTriangleFast` **从未被 `Renderer` 调用**，仅 `core/benchmark.h`
  与单元测试引用
- `core/benchmark.h → raster/rasterizer_fast.h` 构成 `core/ → raster/` 的**层次倒置**

---

## 欠债登记

按 `documentation_rules.md`「早期原型阶段可暂缓部分文档，但必须在 changelog 记录欠债」：

| 欠债 | 说明 | 跟进 |
|---|---|---|
| 全局命名空间污染 | **60 个**顶层类型位于全局命名空间（分布在 30 个头文件），`Position`/`Light`/`Texture` 等名字对下游集成方构成冲突风险 | TASK-007 |
| `docs/api.md` / `docs/class_reference.md` 缺失 | 公共接口目前只能从 `docs/modules.md` 与各头文件注释获取 | TASK-001 后续 |
| 第三方版本未完全锁定 | googletest 为 main 分支快照；tinygltf / tinyobjloader 未锁定修订号 | `third/README.md`「已知版本锁定缺口」 |
| 无覆盖率度量 | `testing_rules.md` 要求核心模块行覆盖率 ≥ 80%，当前无任何覆盖率配置 | TASK-006 |
