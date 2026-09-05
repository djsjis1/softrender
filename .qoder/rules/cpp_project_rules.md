---
trigger: glob
glob: "**/*.{h,hpp,cpp,cc,cxx},**/CMakeLists.txt"
---
# C++ 企业级项目规范

## 适用范围

本规范适用于所有 C++ 企业级项目，与 engineering_rules.md 配合使用。
engineering_rules.md 定义通用编码原则，本文件定义 C++ 工程实践细节。

---

# C++ 标准规范

统一使用 C++17，新项目可使用 C++20。

禁止：

* 混用不同 C++ 标准特性导致编译器兼容性问题
* 使用已废弃的 C++ 特性（如 `auto_ptr`、`register`）
* 依赖未定义行为（UB）

推荐使用 C++17 特性：

* `std::optional` 替代可空指针返回值
* `std::variant` 替代 union
* `if constexpr` 替代 SFINAE 模板特化
* 结构化绑定 `auto [a, b] = pair`

---

# 类型安全规范

数据交换类型（协议、序列化、文件格式、跨模块接口）必须使用固定宽度整型，禁止依赖平台相关类型大小：

| 禁止 | 必须 |
|------|------|
| `int` | `int32_t` |
| `unsigned int` | `uint32_t` |
| `long` | `int64_t` |
| `unsigned long` | `uint64_t` |

例外：局部循环计数与纯内存内统计量可用 `int`；尺寸与容量统一用 `size_t`。

禁止有损隐式类型转换（宽转窄、有符号与无符号混用）：

```cpp
// 禁止（int64_t → int32_t 可能截断）
int64_t a = 100;
int32_t b = a;  // 隐式有损转换

// 必须
int32_t b = static_cast<int32_t>(a);  // 显式声明截断风险，需自行确认安全
```

无损拓宽转换（如 `int32_t` → `int64_t`）可隐式进行。

禁止 C 风格强制转换：

```cpp
// 禁止
int* p = (int*)ptr;

// 必须
int* p = static_cast<int*>(ptr);
```

---

# 初始化规范

所有变量必须在声明时初始化：

```cpp
// 禁止
int count;
bool flag;

// 必须
int32_t count = 0;
bool flag = false;
```

优先使用花括号初始化（防止窄化转换）：

```cpp
// 推荐
int32_t value{42};
std::vector<int32_t> items{1, 2, 3};

// 类成员初始化
class VideoDecoder {
    int32_t frame_count_{0};
    bool is_running_{false};
};
```

---

# 枚举规范

必须使用 `enum class`，禁止裸 `enum`：

```cpp
// 禁止
enum Status { OK, FAILED, TIMEOUT };

// 必须
enum class DecodeStatus : int32_t {
    kSuccess = 0,
    kFailed = -1,
    kTimeout = -2,
};
```

枚举值必须有明确的底层类型。

---

# Rule of 0 / 3 / 5

**Rule of 0**：优先设计不需要手动管理资源的类（用智能指针和 RAII）。

**Rule of 5**：若类定义了析构函数，必须同时定义或删除：

* 拷贝构造函数
* 拷贝赋值运算符
* 移动构造函数
* 移动赋值运算符

```cpp
class VideoBuffer {
public:
    VideoBuffer() = default;
    ~VideoBuffer();

    // 必须明确声明以下四个
    VideoBuffer(const VideoBuffer&) = delete;             // 禁止拷贝
    VideoBuffer& operator=(const VideoBuffer&) = delete;

    VideoBuffer(VideoBuffer&&) noexcept;                  // 允许移动
    VideoBuffer& operator=(VideoBuffer&&) noexcept;
};
```

移动构造函数和移动赋值运算符必须标记 `noexcept`。

析构函数必须标记 `noexcept`（默认即是，不得抛出异常）。

---

# noexcept 规范

以下函数必须标记 `noexcept`：

* 析构函数
* 移动构造函数
* 移动赋值运算符
* 不会抛出异常的工具函数

```cpp
// 必须
VideoDecoder(VideoDecoder&&) noexcept = default;
~VideoDecoder() noexcept;
int32_t getFrameCount() const noexcept { return frame_count_; }
```

---

# 异常策略

项目统一选择一种错误处理策略，禁止混用：

**策略一（推荐用于性能敏感项目）**：使用错误码 + `std::optional` / `std::expected`

```cpp
// 推荐
std::optional<VideoFrame> decodeFrame(const AVPacket* packet);
// 失败返回 std::nullopt，调用方必须检查
```

**策略二**：使用异常，但必须：

* 只在构造函数、无法恢复的错误中抛出
* 禁止在析构函数中抛出
* 禁止跨线程传递异常

不论选择哪种策略，必须在 `docs/adr/` 中记录决策。

---

# 命名空间规范

每个模块必须定义独立命名空间，推荐使用 C++17 嵌套语法：

```cpp
namespace player::codec {
    class VideoDecoder { ... };
}
```

禁止：全局命名空间定义业务符号、命名空间嵌套超过 3 层、头文件中使用 `using namespace`。

---

# 全局状态规范

禁止全局变量（包括 `static` 全局对象）。

禁止裸单例模式（`getInstance()` 返回静态局部变量除外）：

```cpp
// 禁止
static VideoDecoder* g_decoder = nullptr;

// 允许（Meyers Singleton，线程安全）
class Logger {
public:
    static Logger& instance() {
        static Logger logger;
        return logger;
    }
};
```

单例必须有明确的销毁时机，不得依赖程序退出自动销毁。

---

# C++ 命名约定

## 常量命名

全局常量、类成员常量、constexpr 常量统一使用 `k` 前缀 + PascalCase：

```cpp
constexpr int32_t kMaxRetryCount = 3;
constexpr int32_t kReconnectDelayMs = 1000;
constexpr double kPi = 3.14159265;

class VideoDecoder {
    static constexpr int32_t kDefaultBufferSize = 4096;
};
```

## 成员变量命名

类成员变量使用尾缀下划线 `_`：

```cpp
class VideoDecoder {
    int32_t frame_count_{0};
    bool is_running_{false};
    std::unique_ptr<Impl> impl_;  // pImpl 模式
};
```

## 函数/方法命名

函数使用 camelCase，表达动作 + 对象：

```cpp
class VideoDecoder {
    bool initialize(const Config& config);
    std::optional<VideoFrame> decodeFrame(const AVPacket* packet);
    int32_t getFrameCount() const noexcept;
    void releaseBuffers();
};
```

布尔 getter 使用 `is` / `has` / `can` 前缀。

## 文件名

* 头文件：`.h` 扩展名（非 `.hpp`）
* 源文件：`.cpp` 扩展名
* 文件名与类名一致，使用 PascalCase：`VideoDecoder.h`、`VideoDecoder.cpp`
* 工具类/函数文件使用 snake_case：`string_utils.h`、`frame_pool.cpp`

---

# C++ 并发实现规范

## 锁使用

锁的持锁时间必须尽量短：

```cpp
// 禁止：持锁期间调用外部回调
std::lock_guard<std::mutex> lock(mutex_);
callback_(data);  // 可能引发死锁

// 必须：先拷贝数据，释放锁后再回调
{
    std::lock_guard<std::mutex> lock(mutex_);
    local_data = shared_data_;
}
callback_(local_data);
```

多锁获取顺序必须全局一致（防止死锁）。

优先使用 `std::lock_guard` / `std::unique_lock`，禁止裸 `mutex.lock()` / `mutex.unlock()`。

## 条件变量

条件变量必须配合循环检查（防止虚假唤醒）：

```cpp
// 禁止
cv.wait(lock);

// 必须
cv.wait(lock, [this] { return condition_; });
```

## 原子操作

简单标志位优先使用 `std::atomic<bool>` 而非 mutex：

```cpp
std::atomic<bool> running_{true};  // 线程安全退出标志
```

---

# C++ 注释规范

## Doxygen 格式

公共接口必须使用 Doxygen 格式文档注释：

```cpp
/**
 * @brief 解码一帧视频数据
 * @param packet 输入的编码包，不能为空
 * @return 解码后的帧，失败返回 std::nullopt
 */
std::optional<VideoFrame> decodeFrame(const AVPacket* packet);
```

## 注释原则

* 注释解释「为什么」，而非「做什么」
* 复杂算法必须在函数上方说明算法思想和时间复杂度
* 禁止注释掉的死代码长期保留（应直接删除）
* 禁止无意义注释，如 `// 加1` 对应 `i++`
* 禁止翻译代码的注释

---

# C++ 文件头注释规范

每个 C++ 文件（头文件和源文件）必须在文件顶部包含 Doxygen 格式的头注释，说明文件职责、关键类的公共接口摘要和使用示例。

## 头文件（.h）格式

```cpp
/**
 * @file VideoDecoder.h
 * @brief 视频解码器——将编码包解码为视频帧
 *
 * 关键类：
 *   VideoDecoder
 *     - initialize(config) → 初始化解码器
 *     - decodeFrame(packet) → std::optional<VideoFrame>，解码一帧
 *     - getFrameCount() → int32_t，已解码帧数
 *     - releaseBuffers() → 释放解码缓冲区
 *
 * 用法示例：
 * @code
 *   VideoDecoder decoder;
 *   decoder.initialize(config);
 *   auto frame = decoder.decodeFrame(packet);
 *   if (frame) { render(*frame); }
 * @endcode
 */
```

## 源文件（.cpp）格式

```cpp
/**
 * @file VideoDecoder.cpp
 * @brief VideoDecoder 实现，基于 FFmpeg 硬解码
 */
```

## 工具函数文件格式

```cpp
/**
 * @file string_utils.h
 * @brief 字符串工具函数集
 *
 * 导出函数：
 *   - trim(str) → 去除首尾空白
 *   - split(str, delim) → std::vector<std::string>，按分隔符拆分
 *   - toLower(str) → 转小写
 */
```

## 强制要求

* 新增文件必须编写头注释，修改文件时若头注释过时须同步更新
* 头注释只描述「是什么」和「怎么用」，不重复实现细节
* 接口变更时头注释必须同步更新（与公共接口 Doxygen 注释同等重要）
* AI 探索陌生代码时优先读头注释快速定位，因此头注释的准确性直接影响开发效率

---

# C++ 安全规范

## 禁止不安全的 C 函数

| 禁止 | 替代 |
|------|------|
| `strcpy` | `strncpy` / `std::string` |
| `sprintf` | `snprintf` / `std::format` |
| `gets` | `fgets` |
| `scanf("%s")` | 限定长度版本 |

## 边界检查

* 数组下标访问前必须检查边界
* 外部输入的下标优先用 `std::vector::at()`（越界抛异常）；内部热路径在已证明边界安全后可用 `operator[]`
* 外部传入的偏移量、长度必须验证合法性

## 整数安全

* 禁止有符号/无符号混用比较
* 整数运算注意溢出（优先使用 `std::size_t` 处理大小）
* 缓冲区大小计算必须检查溢出

---

# C++ 编译规范

## 零警告原则

* 必须在 `-Wall -Wextra -Werror` 下无警告编译
* 禁止无差别屏蔽警告；确需屏蔽第三方库警告时，可用 `#pragma` / `NOLINT`，但必须附注释说明原因，且作用域限缩到最小范围
* 禁止强制类型转换绕过编译器警告

## 头文件规范

* 必须有头文件保护（`#pragma once` 或 include guard）
* 包含顺序：自身头文件 → 项目头文件 → 第三方库 → 标准库
* 禁止在头文件中使用 `using namespace`
* 尽量使用前置声明减少不必要的 `#include`（降低编译依赖）

---

# 工具链规范

配置细节以项目内的 `.clang-format` / `.clang-tidy` 文件与 CI 为准（工具强制，不在规则中复述），仅声明行为要求：

* 格式化遵循 `.clang-format`（推荐 BasedOnStyle: Google / IndentWidth: 4 / ColumnLimit: 100），提交前自动格式化，禁止手动格式化混用
* 静态分析遵循 `.clang-tidy`（推荐启用 `bugprone-*` / `cppcoreguidelines-*` / `modernize-*` / `performance-*` / `readability-*`），屏蔽第三方库警告用 `NOLINT` / `#pragma` 并注释原因，作用域限缩到最小
* Debug 构建开启 ASAN + UBSAN（`-fsanitize=address,undefined`）；TSAN 检测数据竞争（与 ASAN 互斥）；发布版本禁止开启 Sanitizer

---

# 构建系统规范

## CMake

使用 target-based CMake（CMake 3.15+）：禁止 `include_directories()` / `link_libraries()` 全局设置，统一使用 `target_include_directories()` / `target_link_libraries()` 并显式声明 `PRIVATE` / `PUBLIC` 可见性。

## 构建配置

必须支持以下构建类型：

| 类型 | 用途 | Sanitizer |
|------|------|-----------|
| `Debug` | 开发调试 | 必须开启 ASAN/UBSAN |
| `RelWithDebInfo` | 集成测试 | 不开启 |
| `Release` | 生产发布 | 禁止开启 |

## 依赖管理

第三方库引入必须：

1. 评估许可证是否与项目兼容
2. 锁定具体版本号（禁止使用 `latest`）
3. 已有成熟开源库时优先直接集成，不重复造轮子；二选一：
   * 集成到 `third/` 目录，并在 `third/README.md` 记录版本、来源、许可证（推荐用于 header-only 或源码集成的库）
   * 系统包管理或 CMake FetchContent 锁定版本管理
4. 在 `docs/architecture.md` 中记录引入原因

```cmake
# 推荐：使用 FetchContent 锁定版本
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0  # 必须锁定版本
)
```

---

# ABI / API 兼容性规范

## 对外接口变更原则

公共头文件中的接口（类、函数、结构体）修改前必须评估：

* 是否破坏二进制兼容性（ABI Break）
* 是否破坏源码兼容性（API Break）

**禁止**在同一大版本内：

* 删除公共接口
* 修改函数签名（参数类型、顺序、数量）
* 修改已导出类的成员布局

## 废弃策略

废弃接口必须：

1. 使用 `[[deprecated("使用 newFunction() 替代")]]` 标注
2. 在至少一个版本内保留废弃接口
3. 在 `docs/api.md` 和 `changelog.md` 中记录

```cpp
// 标记废弃，提示替代方案
[[deprecated("请使用 decodeFrameAsync() 替代，将在 v3.0 移除")]]
VideoFrame* decodeFrame(const AVPacket* packet);
```

## 动态库导出规范

```cpp
// 统一使用宏控制符号导出/导入（构建库时定义 PROJECT_EXPORTS）
#if defined(_WIN32)
    #if defined(PROJECT_EXPORTS)
        #define PROJECT_API __declspec(dllexport)
    #else
        #define PROJECT_API __declspec(dllimport)  // 使用方需要导入分支
    #endif
#else
    #define PROJECT_API __attribute__((visibility("default")))
#endif

class PROJECT_API VideoDecoder {
    // 仅暴露必要接口
};
```

---

# AI 自测钩子（Test Hook）规范

## 目的

AI 编写的代码必须保留可验证入口：UI 点击、硬件依赖、时序敏感等无法自动化的路径，必须能让 AI 通过程序化调用完成自测（配合总纲的完成定义 DoD）。

## 设计原则：解耦优先，宏不污染业务代码

1. **根治手段是入口-逻辑分离**：事件处理拆成「事件入口 + 动作逻辑」，UI 回调与测试钩子都转发到同一动作函数；钩子是薄封装，只转发调用，禁止在钩子里写业务逻辑。
2. **集中声明**：钩子声明集中在类内一处 `PROJECT_TEST_HOOK:` 段（或独立的 `<模块>TestHook.h`）；禁止在业务逻辑函数体内散落 `#ifdef`。
3. **发布版零开销**：开关关闭时钩子编译期不可见，无任何运行时开销。
4. **函数体内零插桩**：若某段逻辑「必须从函数体内部观测」，正确做法是把该段拆成独立动作函数供钩子直接调用，而非在函数体内加 `#ifdef` 插桩。

## 宏开关：单点定义，构建系统控制

开关只允许在构建系统中定义，禁止在源文件里硬编码 `#define`：

```cmake
option(PROJECT_ENABLE_TEST_HOOKS "启用 AI 自测钩子（仅限 Debug/测试构建）" OFF)
target_compile_definitions(core PRIVATE
    $<$<BOOL:${PROJECT_ENABLE_TEST_HOOKS}>:PROJECT_ENABLE_TEST_HOOKS>)
```

可见性宏统一放在一个头文件（如 `core/TestHookConfig.h`），全项目只此一处定义：

```cpp
#ifdef PROJECT_ENABLE_TEST_HOOKS
    #define PROJECT_TEST_HOOK public
#else
    #define PROJECT_TEST_HOOK private   // 发布构建中钩子不可见
#endif
```

## 示例：动作转发 + 状态读取钩子（首选模式）

```cpp
class TimelineController {
public:
    void onUndoButtonClicked();      // UI 入口：转发到动作层，不写逻辑

PROJECT_TEST_HOOK:
    // AI 自测钩子：薄封装，仅转发/读取，无业务逻辑
    void testHook_triggerUndo() { performUndo(); }
    int32_t testHook_getUndoDepth() const noexcept {
        return static_cast<int32_t>(undo_stack_.size());
    }

private:
    void performUndo();              // 真实逻辑，UI 与钩子共用
    std::vector<std::unique_ptr<Command>> undo_stack_;
};
```

时序 / 异步场景（如等待渲染完成）：钩子只做**同步状态查询**，等待与超时控制放在测试侧（GoogleTest 的轮询断言），禁止在钩子内 sleep 轮询或挂起业务线程：

```cpp
PROJECT_TEST_HOOK:
    // 供测试侧轮询：渲染是否已完成（自测用途）
    bool testHook_isRenderFinished() const noexcept { return render_finished_.load(); }
```

涉及 UI 线程的钩子必须声明线程语义（在哪个线程调用）；只读查询用 `std::atomic` 或锁保护，与「C++ 并发实现规范」一致。

## 强制约束

* 钩子统一 `testHook_` 前缀，便于全局检索与批量剔除；每个钩子附一行注释说明自测用途；新增 / 删除钩子必须同步登记到 `docs/test_hooks.md`（清单格式见 `testing_rules.md`）
* 钩子禁止出现在 `include/` 公共头，禁止导出到动态库符号表（仅限内部构建使用）
* 单个类内 `PROJECT_TEST_HOOK` 段最多一处；业务函数体内钩子相关 `#ifdef` 必须为零
* 钩子数量受控：只暴露验证所需的路径，禁止借钩子把内部实现全部开放（绕过封装）
* 需要剔除时：关闭构建开关即可；彻底移除只需删除集中的钩子段，不会散落残留

---

# CI/CD 规范

每次 PR 必须通过全部 CI 检查（格式化 / `clang-tidy` / 零警告编译 / `ctest` / Debug Sanitizer）后方可合并；禁止带红灯合并。

