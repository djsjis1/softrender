# 常见问题排查

> 格式：现象 → 原因 → 解决方案。
> 按 `documentation_rules.md` 要求，新增问题时同步补充本文件。

## 构建

### 现象：`cmake -S . -B build` 报 `CMAKE_CXX_STANDARD 23` 相关错误

**原因**：CMake 版本低于 3.20，或编译器不完整支持 C++23。
`src/math/vec.h` 使用了 `requires` 约束子句与 `std::same_as`，需要 C++20 以上语法。

**解决**：CMake 升到 3.20+；编译器用 MSVC 2022 17.10+ / GCC 13+ / Clang 17+。

### 现象：非 Windows 平台构建失败，报 `Win32Window` 未定义

**原因**：`platform/win32_window.h` 整个文件包在 `#ifdef _WIN32` 内，非 Windows
平台展开为空。而 `examples/stage6_realtime.cpp`、`examples/stage8_realtime.cpp`、
`examples/stage1_sprite_anim.cpp` **无条件**使用 `Win32Window`，且 `CMakeLists.txt`
对这三个目标的 `add_executable` 没有平台守卫（`if(WIN32)` 只包住了
`target_link_libraries(... gdi32 user32)`）。

**解决**：见 `docs/tasks/` 中 CI 跨平台修复任务。临时绕过：只构建离屏目标，
例如 `cmake --build build --target stage6_offline`。

### 现象：MSVC 下大量 C4819 警告

**原因**：第三方库文件含非当前代码页字符。

**解决**：已在 `CMakeLists.txt` 中对 `third_party_impl` 目标设置
`/utf-8 /wd4819`（`PRIVATE` 作用域，附注释说明原因，符合规范对第三方警告屏蔽的要求）。
若其他目标出现该警告，检查是否遗漏 `/utf-8`。

## 运行

### 现象：示例运行后 `output/` 里没有生成图像，也没有报错

**原因**：所有示例都以**相对路径** `output/xxx.tga` 写文件。`std::fopen(..., "w")`
与 stb_image_write 在目录不存在时**静默失败**，不会创建目录。

**解决**：
1. 从**仓库根目录**运行示例（`cd` 到仓库根，再执行 `.\build\bin\Release\xxx.exe`）
2. 确认 `output/` 目录存在。仓库中已提交 `output/.gitkeep` 保证克隆后目录存在；
   CI 中由 `.github/workflows/ci.yml` 的 `mkdir -p output` 步骤创建

### 现象：`ctest` 跑一次要 40 秒，但测试本身很快

**原因**：`CMakeLists.txt` 用 `gtest_discover_tests(unit_tests)` 把**每个 GTest 用例
注册为一个独立的 ctest 测试项**。94 个用例 = 94 次进程创建，实测每项 0.33~0.45 秒，
几乎全是 Windows 进程启动 + GTest 初始化开销。测试逻辑本身单进程只需 **28 毫秒**。

**解决**：
```bash
# 并行执行（推荐，墙钟降到 2~3 秒）
ctest --test-dir build --output-on-failure -j

# 开发迭代时直接跑测试二进制（约 30 毫秒）
.\build\bin\Release\unit_tests.exe

# 只跑某个套件
.\build\bin\Release\unit_tests.exe --gtest_filter=RasterizerFast.*
```

### 现象：`demo_gltf` / `demo_skinning` / `demo_pbr_material` 加载模型失败

**原因**：这三个示例是**自产自销**的往返测试——先在运行时生成 `.gltf` 到 `output/`，
再立即读回解析。例如 `demo_gltf.cpp` 先写 `output/test_triangle.gltf` 再 load。

**解决**：只要 `output/` 目录存在就无需任何外部模型文件。若失败，检查上一条
「`output/` 里没有生成图像」。

## 依赖

### 现象：想升级 EnTT，改完 `single_include/` 后行为没变化

**原因**：`third/entt/` 下有**两份版本不同的 EnTT**：

| 路径 | 版本 | 是否被构建使用 |
|---|---|---|
| `entt/src/entt/` | 3.14.0 | ✅ 是 |
| `entt/single_include/entt/entt.hpp` | 3.13.2 | ❌ 否（陈旧残留） |

`CMakeLists.txt` 的 `entt` INTERFACE 目标指向 `third/entt/src`。

**解决**：升级时改 `entt/src/`，或干脆删除 `single_include/`。详见
`third/README.md`「EnTT 双份源码警告」。

### 现象：定义了 `SR_USE_SPDLOG` 后日志里的 `{}` 原样输出

**原因**：`core/log.h` 的两种模式**格式字符串语义不同**：spdlog 模式用 fmt 风格
`{}` 占位符，内置模式用 printf 风格 `%d` / `%s`。用错风格的参数不会被替换。

**解决**：确认目标已定义 `SR_USE_SPDLOG` **且**链接了 `mylog`
（CMake 中用 `sr_enable_logging(<target>)` 函数一次搞定两者）。

## Sanitizer

### 现象：开启 Sanitizer 后 Debug 程序一启动就退出，退出码 `-1073741515`

**原因**：`0xC0000135` = `STATUS_DLL_NOT_FOUND`。MSVC 的 ASAN 默认用**动态运行时**，
可执行文件会导入 `clang_rt.asan_dbg_dynamic-x86_64.dll`（Debug）或
`clang_rt.asan_dynamic-x86_64.dll`（Release）。该 DLL 在 VC Tools 目录下，
只有从 **Developer Command Prompt** 启动时它才在 PATH 上；
从普通 PowerShell / cmd 运行就找不到。

**解决**：把 VC Tools 的 `bin\Hostx64\x64` 目录加进 PATH（版本号按本机实际调整）：

```powershell
$env:PATH = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64;" + $env:PATH
$env:ASAN_OPTIONS = "detect_leaks=0"   # 可选：关掉泄漏检测，只留内存错误检测
.\build\asan\bin\Debug\unit_tests.exe
```

或直接使用开始菜单里的「x64 Native Tools Command Prompt for VS 2022」。

**如何确认 ASAN 真的链上了**：比对两个配置的体积与导入表——

| 配置 | 实测体积 | 导入 ASAN DLL |
|---|---|---|
| Debug（`SR_ENABLE_SANITIZERS=ON`） | 5,087,744 B | ✅ True |
| Release（同一配置目录） | 847,360 B | ❌ False |

体积相差 6 倍、且只有 Debug 导入 ASAN 运行时，说明
`$<$<CONFIG:Debug>:/fsanitize=address>` 生成器表达式生效，
符合 `cpp_project_rules.md`「Release 禁止开启 Sanitizer」。

### 现象：开 Sanitizer 后链接报 `LNK2038: 检测到 "annotate_string" 的不匹配项`

**原因**：Sanitizer 是 **ABI 级**设置，同一次链接里的所有目标必须一致。
MSVC ASAN 会给 STL 容器加注解（`annotate_string` / `annotate_vector`），
一侧开了另一侧没开就会报 LNK2038 + LNK1319。

典型触发方式：把 `/fsanitize=address` 只加到项目自己的目标上（例如只放在
`sr_dev_flags` INTERFACE 库里），而 `add_subdirectory` 引入的 googletest / spdlog
没拿到该标志。

**解决**：本项目已用全局 `add_compile_options` 处理，并把这个块放在
**所有 `add_subdirectory` 之前**（`add_compile_options` 改的是目录属性，
只对其后创建的目标与添加的子目录生效；`third/log` 在第 19 行就引入了）。
这是 `engineering_rules.md`「禁止全局指令」的一个**有意例外**，
理由已写在 `CMakeLists.txt` 对应位置的注释里。

### 现象：Windows 上开不了 UBSAN

**原因**：MSVC 的 `/fsanitize=address` 只支持 AddressSanitizer，**不支持
UndefinedBehaviorSanitizer**。此外 MSVC ASAN 与 `/RTC`（运行时检查）互斥，
且需要关闭增量链接。

**解决**：Windows 只开 ASAN；UBSAN 检查交给 CI 的 ubuntu / macos job
（GCC / Clang 支持 `-fsanitize=address,undefined`）。

### 现象：ASAN 报 `odr-violation` 且指向第三方库

**原因**：header-only 第三方库（stb / tinygltf）在多个编译单元中被 include，
若 `*_IMPLEMENTATION` 宏定义位置不当会产生重复符号。

**解决**：本项目已把全部实现收敛到单一静态库 `third_party_impl`
（`tiny_gltf_impl.cpp` + `tiny_obj_loader_impl.cpp` + `gltf_loader_impl.cpp`）。
新增第三方 header-only 库时沿用此模式，不要在多个 .cpp 中定义 `*_IMPLEMENTATION`。
