// 日志封装库完整演示：单线程 + 多线程压力测试
// 用法：mylog::Logger 只接受 LogConfig 构造（无默认构造）
#include "Logger.h"

#include <thread>
#include <vector>
#include <chrono>
#include <cstdio>

// ============================================================
// 一、在任意函数中使用单例日志（无需传 logger）
// ============================================================
static void do_business_with_singleton(int user_id)
{
    LOGS_DEBUG("进入业务函数, user_id={}", user_id);
    if (user_id < 0)
    {
        LOGS_ERROR("非法 user_id: {}", user_id);
        return;
    }
    LOGS_INFO("业务处理完成, user_id={}", user_id);
}

// ============================================================
// 二、多线程 — 实例方式
// ============================================================
static void mt_worker_instance(mylog::Logger &log, int tid, int count)
{
    for (int i = 0; i < count; ++i)
    {
        LOG_INFO(log, "[thread-{}] msg #{}/{}", tid, i, count);
        if (i % 100 == 99)
        {
            LOG_DEBUG(log, "[thread-{}] 进度: {}/{}", tid, i + 1, count);
        }
    }
    LOG_WARN(log, "[thread-{}] 完成, 共 {} 条", tid, count);
}

void test_mt_instance()
{
    mylog::LogConfig cfg;
    cfg.logger_name = "mt_inst";
    cfg.log_dir = "logs";
    cfg.log_file = "mt_inst.log";
    cfg.enable_console = false;
    cfg.enable_file = true;
    cfg.async = false;
    cfg.level = mylog::Level::Info;
    cfg.max_file_size = 20 * 1024 * 1024;
    cfg.max_files = 3;

    mylog::Logger logger(cfg); // 构造即初始化
    if (!logger.is_initialized())
    {
        std::fprintf(stderr, "mt_inst logger init failed\n");
        return;
    }

    const int kThreads = 8;
    const int kPerThd = 500;

    LOG_INFO(logger, "======= 实例-同步 多线程: {} 线程 x {} 条 =======", kThreads, kPerThd);

    auto t0 = std::chrono::steady_clock::now();
    {
        std::vector<std::thread> thds;
        for (int i = 0; i < kThreads; ++i)
            thds.emplace_back(mt_worker_instance, std::ref(logger), i, kPerThd);
        for (auto &t : thds)
            t.join();
    }
    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    LOG_INFO(logger, "======= 实例-同步 完成, 总计 {} 条, 耗时 {} ms =======",
             kThreads * kPerThd, ms);
}

// ============================================================
// 三、多线程 — 单例-同步
// ============================================================
static void mt_worker_singleton(int tid, int count)
{
    for (int i = 0; i < count; ++i)
        LOGS_INFO("[s-thread-{}] msg #{}/{}", tid, i, count);
    LOGS_WARN("[s-thread-{}] 完成", tid);
}

void test_mt_singleton()
{
    mylog::LogConfig cfg;
    cfg.logger_name = "mt_single";
    cfg.log_dir = "logs";
    cfg.log_file = "mt_single.log";
    cfg.enable_console = false;
    cfg.enable_file = true;
    cfg.async = false;
    cfg.level = mylog::Level::Info;
    cfg.max_file_size = 20 * 1024 * 1024;
    cfg.max_files = 3;

    if (!mylog::SingletonLogger::init(cfg))
    {
        std::fprintf(stderr, "mt_single init failed\n");
        return;
    }

    const int kThreads = 6;
    const int kPerThd = 400;

    LOGS_INFO("======= 单例-同步 多线程: {} 线程 x {} 条 =======", kThreads, kPerThd);

    auto t0 = std::chrono::steady_clock::now();
    {
        std::vector<std::thread> thds;
        for (int i = 0; i < kThreads; ++i)
            thds.emplace_back(mt_worker_singleton, i, kPerThd);
        for (auto &t : thds)
            t.join();
    }
    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    LOGS_INFO("======= 单例-同步 完成, 总计 {} 条, 耗时 {} ms =======",
              kThreads * kPerThd, ms);

    mylog::SingletonLogger::shutdown();
}

// ============================================================
// 四、多线程 — 单例-异步（压力最大）
// ============================================================
static void mt_worker_async(int tid, int count)
{
    for (int i = 0; i < count; ++i)
        LOGS_INFO("[async-thread-{}] msg #{}/{}", tid, i, count);
    LOGS_WARN("[async-thread-{}] 完成", tid);
}

void test_mt_async()
{
    mylog::LogConfig cfg;
    cfg.logger_name = "mt_async";
    cfg.log_dir = "logs";
    cfg.log_file = "mt_async.log";
    cfg.enable_console = false;
    cfg.enable_file = true;
    cfg.async = true;
    cfg.level = mylog::Level::Info;
    cfg.async_queue_size = 32768;
    cfg.async_thread_count = 2;
    cfg.max_file_size = 50 * 1024 * 1024;
    cfg.max_files = 5;

    if (!mylog::SingletonLogger::init(cfg))
    {
        std::fprintf(stderr, "mt_async init failed\n");
        return;
    }

    const int kThreads = 16;
    const int kPerThd = 1000;

    LOGS_INFO("======= 单例-异步 多线程: {} 线程 x {} 条 =======", kThreads, kPerThd);

    auto t0 = std::chrono::steady_clock::now();
    {
        std::vector<std::thread> thds;
        for (int i = 0; i < kThreads; ++i)
            thds.emplace_back(mt_worker_async, i, kPerThd);
        for (auto &t : thds)
            t.join();
    }
    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    LOGS_INFO("======= 单例-异步 完成, 总计 {} 条, 投递耗时 {} ms =======",
              kThreads * kPerThd, ms);

    // 异步必须 shutdown 等后台写完
    mylog::SingletonLogger::shutdown();
}

// ============================================================
// main
// ============================================================
int main()
{
    std::printf("\n==================== 单线程基本演示 ====================\n");

    // ---------- 实例方式 ----------
    {
        mylog::LogConfig cfg;
        cfg.logger_name = "demo";
        cfg.log_dir = "logs";
        cfg.log_file = "demo.log";
        cfg.enable_console = true;
        cfg.enable_file = true;
        cfg.level = mylog::Level::Trace;
        cfg.max_file_size = 5 * 1024 * 1024;
        cfg.max_files = 3;

        mylog::Logger logger(cfg); // 构造即初始化
        LOG_TRACE(logger, "[实例] trace 日志");
        LOG_DEBUG(logger, "[实例] debug, value={}", 42);
        LOG_INFO(logger, "[实例] 程序启动 v{}.{}.{}", 1, 0, 0);
        LOG_WARN(logger, "[实例] 阈值 {:.2f} 接近上限", 0.95);
        LOG_ERROR(logger, "[实例] 读取文件失败: {}", "/tmp/missing.txt");
        LOG_CRITICAL(logger, "[实例] 致命错误, code={}", -1);

        // 第二个独立实例
        mylog::LogConfig cfg2 = cfg;
        cfg2.logger_name = "audit";
        cfg2.log_file = "audit.log";
        cfg2.enable_console = false;
        mylog::Logger audit(cfg2);
        LOG_INFO(audit, "[审计] 仅写入 audit.log 的日志");
    }

    // ---------- 单例方式 ----------
    {
        mylog::LogConfig cfg;
        cfg.logger_name = "global";
        cfg.log_dir = "logs";
        cfg.log_file = "global.log";
        cfg.enable_console = true;
        cfg.enable_file = true;
        cfg.level = mylog::Level::Trace;

        mylog::SingletonLogger::init(cfg);

        LOGS_TRACE("[单例] trace 日志");
        LOGS_DEBUG("[单例] debug, value={}", 100);
        LOGS_INFO("[单例] 全局单例工作正常");
        LOGS_WARN("[单例] 警告: {}", "queue 80% 满");
        LOGS_ERROR("[单例] 出错: code={}", 500);
        LOGS_CRITICAL("[单例] 致命: {}", "out of memory");

        do_business_with_singleton(7);
        do_business_with_singleton(-1);

        mylog::SingletonLogger::shutdown();
    }

    // ========== 多线程压力测试 ==========
    std::printf("\n==================== 多线程压力测试 ====================\n");

    test_mt_instance();
    test_mt_singleton();
    test_mt_async();

    std::printf("\n==================== 全部测试完成 ====================\n");
    std::printf("日志文件位于 ./logs/ 目录下\n");
    return 0;
}
