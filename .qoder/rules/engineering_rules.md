---
trigger: always_on
---
# 工程总纲（根规则文件）

本文件是规则体系的**加载入口与索引**，不是最高优先级 —— 冲突裁决时本文件优先级最低。

## 规则索引与加载方式

| 规则文件 | 职责 | 加载方式 |
| ---------- | ------ | ---------- |
| `ai_workflow_rules.md` | 七步工作流 + S/M/L 变更分级 | 常驻 |
| `project_structure_rules.md` | 目录结构与文件命名 | 项目初始化 / 重构目录时 |
| `task_management_rules.md` | 任务建档与状态流转 | 多步骤任务、触及建档阈值时 |
| `documentation_rules.md` | 文档编写与同步 | 工作流 Step 4（文档计划） |
| `architecture_decision_record.md` | ADR 决策记录 | 重大设计决策（选型/协议/模式） |
| `testing_rules.md` | 测试体系 | 编写或修改测试代码时 |
| `cpp_project_rules.md` | C++ 语言与工程 | 编辑 C++ 源文件时（glob） |
| `git_rules.md` | Commit / 分支 / PR | 提交代码时 |

## 冲突优先级

越接近具体项目的规则优先级越高：

```
项目专属规则 > 语言规则（cpp_project_rules）> 工作流与领域规范 > 工程总则（本文件）
```

权衡原则：**可维护性 > 可扩展性 > 性能 > 开发速度**。规则可豁免，但必须在代码注释中说明原因。

* 就近优先：子目录的项目专属规则覆盖顶层规则；用户在当前对话中的明确指示优先于一切规则。

---

## 架构约定与代码解耦

* 模块边界 = 目录边界；高内聚低耦合，模块间通过接口 / 依赖注入 / 事件通信，禁止直接引用内部实现
* 先设计后编码：明确模块职责、依赖方向、扩展点，禁止边写边设计
* 修改旧代码先分析调用链与影响范围，优先扩展而非重写
* 单一职责：禁止 God Object；函数单一职责，建议 ≤ 50 行；公共接口尽量窄，实现细节尽量隐藏
* 新增功能先判断能否复用已有模块，禁止重复实现相同逻辑

## 通用约定

* 命名表达真实含义，禁止 `tmp` / `data` / `obj` / `helper` / `manager` 等无意义命名
* 注释解释「为什么」而非「做什么」；禁止长期保留注释掉的死代码
* 错误必须处理：禁止空 catch、静默失败；关键路径（启动/停止/异常/通信失败/状态变化）有日志
* 热路径禁止动态内存分配；资源一律 RAII / 智能指针，禁止裸 `new` 后无人管理
* 外部输入视为不可信：路径、网络数据、用户输入、长度与偏移量均需校验
* 线程必须有明确生命周期与可控退出标志，禁止无退出条件的 `while (true)`
* 魔数提取为具名常量；线程、锁、资源管理的具体语言细节见对应语言规则
* 代码必须保留可测试入口：UI 等无法自动化的路径预留测试钩子，构建宏单点开关、集中声明不散落（细节见语言规则）

## 文件头注释规范

每个源文件必须在头部用注释说明：文件职责、关键类/函数及其公共接口、使用方式。

AI 修改代码前必须先读目标文件的头注释，快速理解模块职责与接口，而非通读全部实现。

新增文件时必须编写头注释；修改已有文件时若头注释过时，必须同步更新。

## 红线约束（SoftRenderer 项目级禁令）

> 每条针对一个**代价高昂且难以事后发现**的错误，而非抽象风格要求。
> 触碰红线必须在代码注释中说明豁免理由，并在 `docs/changelog.md` 登记。

### 平台隔离

* **禁止 `src/` 下任何文件 `#include <windows.h>`、`#include "platform/..."` 或引用 Win32 API**
  平台隔离是本项目的核心卖点（`CMakeLists.txt` 第 72-79 行的分层注释已声明此意图）。
  渲染器本体一旦沾上平台代码，「可作为纯头文件库跨平台集成」即告失效。
  唯一例外：`src/raster/rasterizer_fast.h` 的 SSE2 intrinsics，已用
  `#if defined(__SSE2__) || defined(_M_X64) || ...` 编译期探测并提供标量降级路径。
  新增此类例外必须同样提供**非 x86 平台的降级实现**。

### 坐标与数据契约

* **禁止修改 `Framebuffer` 的坐标约定**（x 向右、y 向上、`(0,0)` 在左下角）
  `framebuffer.h` 第 11-15 行注明「后面所有阶段都遵守」。`Renderer2D`、`tga` 写出、
  `pixel_regression` 比对、全部 22 个示例都依赖此约定。改动会**静默翻转所有输出图像**——
  不报错、不崩溃，只是图上下颠倒，极难定位。

* **禁止修改 `Varyings`（`shader/shader.h`）已有字段的语义**
  它是 `Renderer` 与全部 6 个内置着色器之间的公共契约。新增字段需同步修改
  `interpolateVaryings`，否则该字段不会被透视校正插值（静默得到错误值）。

* **禁止在多个 `.cpp` 中定义 `STB_IMAGE_IMPLEMENTATION` / `TINYGLTF_IMPLEMENTATION` 等
  `*_IMPLEMENTATION` 宏**
  本项目已把全部第三方实现收敛到单一静态库 `third_party_impl`
  （`tiny_gltf_impl.cpp` + `tiny_obj_loader_impl.cpp` + `gltf_loader_impl.cpp`）。
  重复定义会导致符号重定义链接错误，或在 ASAN 下报 `odr-violation`。

### 光栅化热路径

* **禁止在 `Renderer::rasterTriangle` 中放宽包围盒的 clamp**
  内层循环用 `fb.row(y)` / `zbuf_.row(y)` **行指针直写**，绕过 `setPixel` /
  `testAndSet` 的边界检查。安全性完全依赖包围盒预先 clamp 到
  `[0, width-1] × [0, height-1]`（`renderer.h` 第 102-108 行）。
  任何修改包围盒计算或行指针用法的改动，都必须重新确认 clamp 仍然生效，
  否则会引入**越界写内存**。

* **禁止 2D 图元绕过 `sr2d::plotBlend` 直接调用 `Framebuffer::setPixel`**
  `plotBlend` 是 2D 子系统唯一的像素写入出口，统一处理边界裁剪、`BlendMode`
  混合与 scissor。绕过它会让 scissor 裁剪与混合模式**静默失效**。

### 日志

* **禁止把 `core/log.h` 内置模式改回 C 可变参**（`const char* fmt, ...` + `va_list` + `vfprintf`）
  历史上它就是这样，导致三个**真实发生过**的缺陷：
  ① 传 `std::string` 等非 POD 类型经 `...` 是未定义行为（MSVC 报 C4840），
     不是「只是忽略参数」那么无害；
  ② 约定写 fmt 风格 `{}` 但按 printf 语义解析 → 占位符从不被替换、参数静默丢失，
     而 `unit_tests` 与大部分示例都未定义 `SR_USE_SPDLOG`，**内置模式是常用路径**；
  ③ 日志文本里的字面 `%`（如 `"100% done"`）被 `vfprintf` 当转换符解析 → UB。
  现版本用变参模板 + `sr::formatLog`，三者均已消除，并由 `Log` 测试套件锁定。
  详见 `docs/architecture.md` §6.2。

* **`SR_LOG_*` 统一写 fmt 风格的 `{}` 占位符**，禁止写 printf 风格的 `%s` / `%d`
  两种模式（spdlog / 内置）都按 `{}` 语法按顺序替换；写 `%s` 在两种模式下
  都只会输出字面量。内置模式**不支持** `{{}}` 转义与 `{:>8}` 格式规格，
  需要这些能力时改用 spdlog 模式（CMake 中调 `sr_enable_logging(<target>)`）。

* **新增日志参数类型必须能被 `std::ostringstream` 输出**
  没有 `operator<<` 的类型会在编译期报错（这是期望行为，优于运行时 UB）；
  若确实需要记录这类对象，在调用处先转成 `std::string` 或 POD 再传。

### 命名空间

* **禁止在全局命名空间新增顶层类型、枚举或自由函数**
  本项目是 header-only 库，符号直接进入用户的编译单元。现存 60 个全局类型
  已是待偿还的架构债（见 `docs/tasks/TASK-007`），`Position` / `Light` / `Texture` /
  `Mesh` 这类通用名对下游集成方构成实际冲突风险。
  新增符号必须放入 `namespace sr`（渲染核心）或 `namespace sr2d`（2D 子系统）。
  历史教训：`sr2d` 命名空间的引入原因，正是 2D 图元名与 `examples/` 中的
  全局同名符号发生了真实冲突。

### 测试

* **禁止新增自研测试框架**
  统一使用 GoogleTest（`testing_rules.md` 明令「已有成熟开源方案，不得重复造轮子」）。
  `src/test/test_framework.h` 是待删除的历史违例（见 `docs/tasks/TASK-004`）。

* **禁止用「三个顶点取值全相同」的数据写插值类测试**
  本项目已因此漏掉一个真实 bug：`rasterizer_fast.h` 的深度插值重心权重错位，
  被两个都传 `z0=z1=z2=0.5f` 的测试用例 100% 掩盖（见 `docs/tasks/TASK-002`）。
  插值 / 混合 / 变换类测试的输入**必须让各顶点取值互不相等**，否则无法区分
  正确实现与权重排列错误的实现。

### 版本控制

* **禁止提交 `build/`、`output/` 下的任何产物**（`output/.gitkeep` 除外）
  `build/` 实测 590 MB；`output/` 全部为示例运行产物，可重新生成。
  唯一允许入库的图像是 `tests/fixtures/golden/*.tga`（像素回归基准图）与
  `tests/fixtures/`（测试输入资产）。

* **禁止提交 `.env`、密钥、个人配置**

## 命令（SoftRenderer）

> 全部命令在**仓库根目录**执行。示例程序以相对路径 `output/xxx` 写文件，
> 从其他目录运行会静默失败。

**配置与构建**

```powershell
# 配置（首次或 CMakeLists.txt 变更后）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# 全量构建（MSVC 多配置生成器必须带 --config）
cmake --build build --config Release --parallel

# 只构建单个目标
cmake --build build --config Release --target unit_tests
```

**测试**

```powershell
# 开发迭代首选：单进程跑完 169 个用例，约 20 毫秒
.\build\bin\Release\unit_tests.exe

# 全量 ctest（CI 用的命令；串行约 40 秒，并行约 13 秒）
ctest --test-dir build -C Release --parallel -LE fast --output-on-failure

# 只跑像素回归（3 个端到端场景）
ctest --test-dir build -C Release -L regression --output-on-failure

# ctest 的冒烟项（单进程跑全部单元测试，0.4 秒）
ctest --test-dir build -C Release -L fast

# 只跑某个套件 / 用例
.\build\bin\Release\unit_tests.exe --gtest_filter=RasterizerFast.*
```

> ⚠️ `ctest` 慢的原因：`gtest_discover_tests` 把每个用例注册为独立 ctest 项，
> 169 用例 = 169 次进程启动，开销全在进程创建而非测试本身。
> `-LE fast` 排除 `unit_tests_all`，避免与 169 个 discover 项重复跑。
> 详见 `docs/troubleshooting.md`。

**Sanitizer**（默认关闭，需显式开启）

```powershell
# 单独配置一个带 Sanitizer 的构建目录，不干扰日常 Release 构建
cmake -S . -B build/asan -DSR_ENABLE_SANITIZERS=ON
cmake --build build/asan --config Debug --target unit_tests --parallel

# 运行前必须把 MSVC ASAN 运行时目录加进 PATH，否则退出码 0xC0000135
$env:PATH = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\<版本>\bin\Hostx64\x64;" + $env:PATH
.\build\asan\bin\Debug\unit_tests.exe
```

> Sanitizer 只在 **Debug 配置**生效（用 `$<$<CONFIG:Debug>:...>` 生成器表达式限定），
> Release 永不带入 —— 实测验证：Debug exe 5.09 MB 且导入 ASAN 运行时，
> Release exe 847 KB 且不导入。
> MSVC 只支持 ASAN，**不支持 UBSAN**；UBSAN 交给 CI 的 ubuntu / macos job。
> 两个越界测试在 Debug 下会 `GTEST_SKIP()`（`assert` 会先中止进程），属预期行为。

**运行示例**

```powershell
.\build\bin\Release\stage06_depth.exe     # 离屏，写 output/*.tga
.\build\bin\Release\demo_2d.exe            # 离屏，写 output/*.png + .tga
.\build\bin\Release\stage10_shadow_live.exe    # 实时窗口，仅 Windows
```

**格式化与静态检查**

```powershell
# 需先安装 LLVM 工具链（当前环境未安装，见 docs/tasks/TASK-005）
clang-format -i <files>          # 配置见根目录 .clang-format
clang-tidy <files>               # 配置见根目录 .clang-tidy（待建立）
```

> `.clang-format` 已存在但**从未被执行过**：实测 19 个 `src/` 头文件的 namespace 体
> 缩进 4 空格，违反配置第 92 行的 `NamespaceIndentation: None`。
> 全仓格式化必须排在所有功能性修改之后、单独一个 commit 执行，
> 并配合 `.git-blame-ignore-revs`。详见 `docs/tasks/TASK-005`。

**覆盖率**

当前**未配置**。`testing_rules.md` 要求核心模块行覆盖率 ≥ 80%、分支 ≥ 60%，
接入方案见 `docs/tasks/TASK-006`。

**DoD 最小验证集**（宣称完成前必须全部通过）

```powershell
cmake --build build --config Release --parallel   # 零错误零警告
.\build\bin\Release\unit_tests.exe                # 169 用例全绿
.\build\bin\Release\render_regression.exe         # 3 个像素回归全绿
```

涉及渲染输出的改动额外要求：跑一遍 19 个离屏示例，确认均正常退出。
涉及 `CMakeLists.txt` 的改动额外要求：比对可执行目标清单与改动前一致
（当前基线：Windows 上 **25 个 exe**）。

---

## AI 工作方式与行为约束

**工作习惯**

* 事实优先：先读现有代码与配置再回答或修改；禁止凭记忆猜测接口与行为，拿不准先查证（搜索 / 读文件）
* 最小变更：只做任务要求的改动，禁止顺手修复、重构无关代码；过程中发现的其他问题先记录，未经确认不动手
* 根因原则：修 bug 先复现、确认根因再修复；禁止掩盖症状（如吞异常、绕过报错）
* 停止条件：同一思路连续尝试 3 次仍失败时停下，报告已尝试方案与卡点，不再循环重试
* 开源优先：已有成熟开源方案时优先直接集成（`third/`），禁止重复造轮子；自研必须在 ADR 中说明理由
* 需求不明确时先确认，禁止猜测后直接编码

**完成标准**

* 完成定义（DoD）：宣称完成前必须机械验证 —— 运行「命令」节的构建 / 测试 / 静态检查并全部通过；无法运行时明确说明原因与替代验证方式
* 不留无跟进计划的 TODO / FIXME；破坏性操作必须先征得确认
