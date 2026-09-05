// Logger.h
// 对 spdlog 的轻量封装：业务代码仅通过本文件提供的宏/类使用日志，
// 将来若要替换底层日志库，只需修改 Logger.h / Logger.cpp 即可。
#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "spdlog/spdlog.h"
#include "spdlog/common.h"

namespace mylog
{

    // 对外暴露的日志级别（与 spdlog 对齐，业务代码不直接依赖其枚举值）
    enum class Level : int
    {
        Trace = 0,
        Debug = 1,
        Info = 2,
        Warn = 3,
        Error = 4,
        Critical = 5,
        Off = 6
    };

    // 日志配置项，均有默认值，可按需覆盖
    struct LogConfig
    {
        std::string logger_name = "app";              // logger 名称（多实例时需唯一）
        std::string log_dir = "logs";                 // 日志目录（相对或绝对路径）
        std::string log_file = "app.log";             // 日志文件名
        bool enable_console = true;                   // 是否输出到控制台（带颜色）
        bool enable_file = true;                      // 是否输出到文件（滚动）
        bool async = false;                           // 是否使用异步日志
        std::size_t max_file_size = 10 * 1024 * 1024; // 单文件最大字节数 (10MB)
        std::size_t max_files = 5;                    // 保留的滚动文件数量
        Level level = Level::Info;                    // 日志级别
        Level flush_level = Level::Warn;              // 触发自动 flush 的级别
        std::string pattern = "[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] [%t] [%s:%#] %v";
        std::size_t async_queue_size = 8192; // 异步队列大小
        std::size_t async_thread_count = 1;  // 异步后台线程数
    };

    // 普通类：由用户自己持有（构造函数接收配置并立即初始化）。
    // 不可默认构造，不可拷贝，可移动。
    class Logger
    {
    public:
        explicit Logger(const LogConfig &cfg);
        ~Logger();

        Logger(const Logger &) = delete;
        Logger &operator=(const Logger &) = delete;
        Logger(Logger &&) noexcept = default;
        Logger &operator=(Logger &&) noexcept = default;

        // 释放资源（析构时也会调用）
        void shutdown();

        // 运行期设置
        void set_level(Level lvl);
        void set_pattern(const std::string &pattern);
        void flush();

        bool is_initialized() const { return logger_ != nullptr; }
        const std::string &name() const { return name_; }

        // 通用 log 接口：由宏调用，带源码位置
        template <typename... Args>
        void log(spdlog::source_loc loc,
                 Level lvl,
                 spdlog::format_string_t<Args...> fmt,
                 Args &&...args)
        {
            if (!logger_)
                return;
            logger_->log(loc, to_spdlog_level(lvl), fmt, std::forward<Args>(args)...);
        }

        // 不带格式化的原始字符串版本
        void log(spdlog::source_loc loc, Level lvl, std::string_view msg)
        {
            if (!logger_)
                return;
            logger_->log(loc, to_spdlog_level(lvl), msg);
        }

    private:
        Logger() = delete; // 必须通过 LogConfig 构造

        static spdlog::level::level_enum to_spdlog_level(Level lvl);

        std::shared_ptr<spdlog::logger> logger_;
        std::string name_;
    };

    // 全局单例包装：无需手动持有 Logger，直接 LOGS_XXX(...) 即可。
    // 使用前必须先调用 SingletonLogger::init(cfg)，退出前调用 shutdown()。
    class SingletonLogger
    {
    public:
        static Logger &instance();
        static bool init(const LogConfig &cfg = LogConfig{});
        static void shutdown();

    private:
        SingletonLogger() = delete;
    };

} // namespace mylog

// ----------------------------- 对外宏 -----------------------------
// 两套日志宏：
//   1) LOG_XXX(logger, ...) ：传 mylog::Logger 实例（左值），适合多实例场景。
//   2) LOGS_XXX(...)        ：使用全局单例，无需传 logger，更简洁。
//      使用前需先调用 mylog::SingletonLogger::init(cfg)。
#define MYLOG_SRC_LOC \
    ::spdlog::source_loc { __FILE__, __LINE__, SPDLOG_FUNCTION }

// ---- 带实例参数的宏 ----
#define LOG_TRACE(logger, ...) \
    (logger).log(MYLOG_SRC_LOC, ::mylog::Level::Trace, __VA_ARGS__)
#define LOG_DEBUG(logger, ...) \
    (logger).log(MYLOG_SRC_LOC, ::mylog::Level::Debug, __VA_ARGS__)
#define LOG_INFO(logger, ...) \
    (logger).log(MYLOG_SRC_LOC, ::mylog::Level::Info, __VA_ARGS__)
#define LOG_WARN(logger, ...) \
    (logger).log(MYLOG_SRC_LOC, ::mylog::Level::Warn, __VA_ARGS__)
#define LOG_ERROR(logger, ...) \
    (logger).log(MYLOG_SRC_LOC, ::mylog::Level::Error, __VA_ARGS__)
#define LOG_CRITICAL(logger, ...) \
    (logger).log(MYLOG_SRC_LOC, ::mylog::Level::Critical, __VA_ARGS__)

// ---- 使用全局单例的宏 ----
#define LOGS_TRACE(...) \
    ::mylog::SingletonLogger::instance().log(MYLOG_SRC_LOC, ::mylog::Level::Trace, __VA_ARGS__)
#define LOGS_DEBUG(...) \
    ::mylog::SingletonLogger::instance().log(MYLOG_SRC_LOC, ::mylog::Level::Debug, __VA_ARGS__)
#define LOGS_INFO(...) \
    ::mylog::SingletonLogger::instance().log(MYLOG_SRC_LOC, ::mylog::Level::Info, __VA_ARGS__)
#define LOGS_WARN(...) \
    ::mylog::SingletonLogger::instance().log(MYLOG_SRC_LOC, ::mylog::Level::Warn, __VA_ARGS__)
#define LOGS_ERROR(...) \
    ::mylog::SingletonLogger::instance().log(MYLOG_SRC_LOC, ::mylog::Level::Error, __VA_ARGS__)
#define LOGS_CRITICAL(...) \
    ::mylog::SingletonLogger::instance().log(MYLOG_SRC_LOC, ::mylog::Level::Critical, __VA_ARGS__)
