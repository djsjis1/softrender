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
// fmt 风格（{} 占位符）。内置模式下降级为 printf 风格忽略参数。
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
// ============== 内置极简模式（零依赖）==============
#include <cstdio>
#include <cstdarg>

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

    class Log
    {
    public:
        static LogLevel level() { return level_; }
        static void setLevel(LogLevel l) { level_ = l; }

        static void log(LogLevel l, const char *file, int line, const char *fmt, ...)
        {
            if (l < level_)
                return;
            static const char *names[] = {"TRACE", "DEBUG", "INFO ", "WARN ", "ERROR", "CRIT "};
            std::fprintf(stderr, "[%s] %s:%d: ", names[static_cast<int>(l)], file, line);
            va_list args;
            va_start(args, fmt);
            std::vfprintf(stderr, fmt, args);
            va_end(args);
            std::fprintf(stderr, "\n");
        }

    private:
        static inline LogLevel level_ = LogLevel::Warn; // 默认 Warn，减少噪声
    };
} // namespace sr

// 内置模式：SR_LOG_* 接受 printf 格式（与 spdlog 模式的 fmt 格式不兼容，
// 但渲染器本体内部日志极少，且两种模式不会同时编译）
#define SR_LOG_TRACE(...) sr::Log::log(sr::LogLevel::Trace, __FILE__, __LINE__, __VA_ARGS__)
#define SR_LOG_DEBUG(...) sr::Log::log(sr::LogLevel::Debug, __FILE__, __LINE__, __VA_ARGS__)
#define SR_LOG_INFO(...) sr::Log::log(sr::LogLevel::Info, __FILE__, __LINE__, __VA_ARGS__)
#define SR_LOG_WARN(...) sr::Log::log(sr::LogLevel::Warn, __FILE__, __LINE__, __VA_ARGS__)
#define SR_LOG_ERROR(...) sr::Log::log(sr::LogLevel::Error, __FILE__, __LINE__, __VA_ARGS__)
#define SR_LOG_CRITICAL(...) sr::Log::log(sr::LogLevel::Critical, __FILE__, __LINE__, __VA_ARGS__)

inline bool sr_log_init(bool = false, bool = false) { return true; }
inline void sr_log_shutdown() {}

#endif // SR_USE_SPDLOG
