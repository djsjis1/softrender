// Logger.cpp
#include "Logger.h"

#include <vector>
#include <cstdio>
#include <filesystem>

#include "spdlog/async.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"

namespace mylog
{

    // ---------------------- Logger ----------------------

    Logger::Logger(const LogConfig &cfg)
    {
        try
        {
            // 若指定了日志目录，先确保目录存在
            if (!cfg.log_dir.empty() && cfg.enable_file)
            {
                std::filesystem::create_directories(cfg.log_dir);
            }

            std::vector<spdlog::sink_ptr> sinks;

            if (cfg.enable_console)
            {
                sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
            }

            if (cfg.enable_file)
            {
                std::string path = cfg.log_dir.empty()
                                       ? cfg.log_file
                                       : (cfg.log_dir + "/" + cfg.log_file);
                auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    path, cfg.max_file_size, cfg.max_files);
                sinks.push_back(file_sink);
            }

            if (sinks.empty())
            {
                sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
            }

            if (cfg.async)
            {
                try
                {
                    spdlog::init_thread_pool(cfg.async_queue_size, cfg.async_thread_count);
                }
                catch (...)
                {
                    // 线程池已存在，忽略
                }
                logger_ = std::make_shared<spdlog::async_logger>(
                    cfg.logger_name,
                    sinks.begin(), sinks.end(),
                    spdlog::thread_pool(),
                    spdlog::async_overflow_policy::block);
            }
            else
            {
                logger_ = std::make_shared<spdlog::logger>(
                    cfg.logger_name, sinks.begin(), sinks.end());
            }

            logger_->set_level(to_spdlog_level(cfg.level));
            logger_->flush_on(to_spdlog_level(cfg.flush_level));
            logger_->set_pattern(cfg.pattern);

            spdlog::drop(cfg.logger_name);
            spdlog::register_logger(logger_);

            name_ = cfg.logger_name;
        }
        catch (const spdlog::spdlog_ex &ex)
        {
            std::fprintf(stderr, "[mylog] init failed (%s): %s\n",
                         cfg.logger_name.c_str(), ex.what());
            logger_.reset();
            name_.clear();
        }
    }

    Logger::~Logger() { shutdown(); }

    spdlog::level::level_enum Logger::to_spdlog_level(Level lvl)
    {
        switch (lvl)
        {
        case Level::Trace:
            return spdlog::level::trace;
        case Level::Debug:
            return spdlog::level::debug;
        case Level::Info:
            return spdlog::level::info;
        case Level::Warn:
            return spdlog::level::warn;
        case Level::Error:
            return spdlog::level::err;
        case Level::Critical:
            return spdlog::level::critical;
        case Level::Off:
            return spdlog::level::off;
        }
        return spdlog::level::info;
    }

    void Logger::shutdown()
    {
        if (logger_)
        {
            try
            {
                logger_->flush();
            }
            catch (...)
            {
            }
        }
        if (!name_.empty())
        {
            try
            {
                spdlog::drop(name_);
            }
            catch (...)
            {
            }
            name_.clear();
        }
        logger_.reset();
    }

    void Logger::set_level(Level lvl)
    {
        if (logger_)
            logger_->set_level(to_spdlog_level(lvl));
    }

    void Logger::set_pattern(const std::string &pattern)
    {
        if (logger_)
            logger_->set_pattern(pattern);
    }

    void Logger::flush()
    {
        if (logger_)
            logger_->flush();
    }

    // ---------------------- SingletonLogger ----------------------

    namespace
    {
        std::unique_ptr<Logger> g_singleton;
    }

    Logger &SingletonLogger::instance()
    {
        return *g_singleton;
    }

    bool SingletonLogger::init(const LogConfig &cfg)
    {
        // 如果先前已初始化，先释放
        if (g_singleton)
        {
            g_singleton->shutdown();
        }
        g_singleton = std::make_unique<Logger>(cfg);
        return g_singleton->is_initialized();
    }

    void SingletonLogger::shutdown()
    {
        if (g_singleton)
        {
            g_singleton->shutdown();
            g_singleton.reset();
        }
    }

} // namespace mylog
