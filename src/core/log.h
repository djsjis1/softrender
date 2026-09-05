#pragma once
// ============================================================
// 渲染器日志接口（条件编译）
//
// 两种模式：
//   1. 定义了 SR_USE_SPDLOG：底层走 third/log 的 spdlog 封装，
//      功能完整（文件滚动、异步、彩色控制台等）。
//      需要链接 mylog 库。
//   2. 未定义（默认）：使用内置的极简实现（fprintf stderr），
//      渲染器本体保持零第三方依赖。
//
// 无论哪种模式，渲染器内部统一使用 SR_LOG_* 宏，格式为
// fmt 风格（{} 占位符），两种模式都会按顺序替换 {}。
//
// 【内置模式为何用变参模板而非 C 可变参】旧实现是
//   static void log(LogLevel, const char* file, int line, const char* fmt, ...)
// 搭配 va_list + vfprintf。它有**三个**问题，均已在本版修正：
//   1. 传 std::string 等非 POD 类型经 ... 是未定义行为（MSVC 报 C4840），
//      而非直觉上的「只是忽略参数」。实际已在 image_loader.h 上踩过坑。
//   2. 格式串按 printf 语义解析，但约定写的是 {} → 占位符从不被替换，
//      默认构建下所有带参日志都只能看到字面量 {}，参数全丢。
//      而 unit_tests 与大部分示例并未定义 SR_USE_SPDLOG，**内置模式是常用路径**。
//   3. 日志文本里若含字面 '%'（如 "100% done"），vfprintf 会把它当
//      转换符解析 → UB。
//
// 现在改用变参模板 + std::ostringstream：任何有 operator<< 的类型都合法，
// {} 按顺序替换，最终消息经单个 %s 输出（所以字面 '%' 也安全）。
// 代价是仍用 <sstream>（标准库），**src/ 保持零第三方依赖**。
//
// 【仍与 fmt 的差异】不支持 {{ }} 转义、不支持 {:>8} 之类的格式规格，
// 浮点精度走 ostringstream 默认（6 位有效数字）。参数多于占位符时
// 追加输出而不丢弃；占位符多于参数时原样保留 {}（降级而非像 fmt 那样抛异常）。
//
// 企业级使用建议：在 CMake 中为需要日志的目标定义 SR_USE_SPDLOG
// 并链接 mylog，即可无缝切换到 spdlog。
// ============================================================

#ifdef SR_USE_SPDLOG
// ============== spdlog 模式 ==============
#include "Logger.h"

#define SR_LOG_TRACE(...) LOGS_TRACE(__VA_ARGS__)
#define SR_LOG_DEBUG(...) LOGS_DEBUG(__VA_ARGS__)
#define SR_LOG_INFO(...) LOGS_INFO(__VA_ARGS__)
#define SR_LOG_WARN(...) LOGS_WARN(__VA_ARGS__)
#define SR_LOG_ERROR(...) LOGS_ERROR(__VA_ARGS__)
#define SR_LOG_CRITICAL(...) LOGS_CRITICAL(__VA_ARGS__)

inline bool sr_log_init(bool enableFile = false, bool async = false)
{
    mylog::LogConfig cfg;
    cfg.logger_name = "SoftRenderer";
    cfg.enable_console = true;
    cfg.enable_file = enableFile;
    cfg.async = async;
    cfg.level = mylog::Level::Info;
    cfg.log_dir = "logs";
    cfg.log_file = "softrenderer.log";
    cfg.pattern = "[%H:%M:%S.%e] [%^%l%$] %v";
    return mylog::SingletonLogger::init(cfg);
}

inline void sr_log_shutdown()
{
    mylog::SingletonLogger::shutdown();
}

#else
// ============== 内置模式（零第三方依赖）==============
#include <cstdio>
#include <sstream>
#include <string>

namespace sr
{
    enum class LogLevel : int
    {
        Trace = 0,
        Debug = 1,
        Info = 2,
        Warn = 3,
        Error = 4,
        Critical = 5,
        Off = 6
    };

    // 等级名。用 switch 而非常量数组：数组下标依赖枚举值的连续性，
    // 且旧实现的数组漏了 Off，一旦传 Off 就会越界读。
    inline const char *levelName(LogLevel l)
    {
        switch (l)
        {
        case LogLevel::Trace:
            return "TRACE";
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO ";
        case LogLevel::Warn:
            return "WARN ";
        case LogLevel::Error:
            return "ERROR";
        case LogLevel::Critical:
            return "CRIT ";
        case LogLevel::Off:
            return "OFF  ";
        }
        return "?????";
    }

    namespace logfmt
    {
        // 递归终止：参数已用完，剩余格式串原样输出
        // （其中未匹配的 {} 保留为字面量，属可接受的降级）
        inline void appendRest(std::ostringstream &os, const char *fmt)
        {
            os << fmt;
        }

        // 扫到下一个 "{}" 就写入 value，然后从它之后继续处理剩余参数
        template <typename T, typename... Rest>
        void appendRest(std::ostringstream &os, const char *fmt, const T &value, const Rest &...rest)
        {
            for (const char *p = fmt; *p != '\0'; ++p)
            {
                // p[1] 在 p[0]=='{' 且已到串尾时读到的是 '\0'，仍在字面量数组内，合法
                if (p[0] == '{' && p[1] == '}')
                {
                    os << value;
                    appendRest(os, p + 2, rest...);
                    return;
                }
                os << *p;
            }
            // 格式串里已没有 {} 但还有参数：追加输出而不丢弃。
            // 旧实现会静默吃掉这些参数，导致信息丢失且无从察觉。
            os << ' ' << value;
            appendRest(os, "", rest...);
        }
    } // namespace logfmt

    // 无参重载：避开 ostringstream 构造。声明在模板之前，
    // 使 SR_LOG_INFO("纯文本") 这类无参调用直接命中它。
    inline std::string formatLog(const char *fmt) { return std::string(fmt); }

    // 把 fmt 风格的 {} 按出现顺序替换为参数。
    //
    // 【为何独立成函数而不藏在 Log::log 里】为了**可测试**：
    // engineering_rules.md 要求「代码必须保留可测试入口」。若格式化逻辑
    // 只存在于 log() 内部，验证它就得捕获 stderr（在 Windows 上既繁琐又不稳定）；
    // 独立出来后可直接断言返回的字符串。
    template <typename... Args>
    inline std::string formatLog(const char *fmt, const Args &...args)
    {
        std::ostringstream os;
        logfmt::appendRest(os, fmt, args...);
        return os.str();
    }

    class Log
    {
    public:
        static LogLevel level() { return level_; }
        static void setLevel(LogLevel l) { level_ = l; }

        // 类型安全：参数经模板转发到 ostringstream，不再走 C 可变参，
        // 因此任何有 operator<< 的类型（含 std::string）都合法，无 C4840 / UB。
        // 没有 operator<< 的类型会在**编译期**报错，而非运行时 UB。
        template <typename... Args>
        static void log(LogLevel l, const char *file, int line, const char *fmt, const Args &...args)
        {
            if (l < level_)
                return;
            // 消息先格式化好，再用单个 %s 输出：这样消息文本里的字面 '%'
            // 不会被 fprintf 当转换符解析（旧实现的 vfprintf 路径有此 UB）。
            const std::string msg = formatLog(fmt, args...);
            std::fprintf(stderr, "[%s] %s:%d: %s\n", levelName(l), file, line, msg.c_str());
        }

    private:
        static inline LogLevel level_ = LogLevel::Warn; // 默认 Warn，减少噪声
    };
} // namespace sr

// 内置模式：SR_LOG_* 使用 fmt 风格的 {} 占位符，由 sr::formatLog 按顺序替换。
// 与 spdlog 模式的差别仅在格式化能力（不支持 {{}} 转义与 {:>8} 规格），
// 占位符语法与参数类型要求完全一致，两种模式不会同时编译。
#define SR_LOG_TRACE(...) sr::Log::log(sr::LogLevel::Trace, __FILE__, __LINE__, __VA_ARGS__)
#define SR_LOG_DEBUG(...) sr::Log::log(sr::LogLevel::Debug, __FILE__, __LINE__, __VA_ARGS__)
#define SR_LOG_INFO(...) sr::Log::log(sr::LogLevel::Info, __FILE__, __LINE__, __VA_ARGS__)
#define SR_LOG_WARN(...) sr::Log::log(sr::LogLevel::Warn, __FILE__, __LINE__, __VA_ARGS__)
#define SR_LOG_ERROR(...) sr::Log::log(sr::LogLevel::Error, __FILE__, __LINE__, __VA_ARGS__)
#define SR_LOG_CRITICAL(...) sr::Log::log(sr::LogLevel::Critical, __FILE__, __LINE__, __VA_ARGS__)

inline bool sr_log_init(bool = false, bool = false) { return true; }
inline void sr_log_shutdown() {}

#endif // SR_USE_SPDLOG
