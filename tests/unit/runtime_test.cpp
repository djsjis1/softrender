// ============================================================
// runtime 模块单元测试：日志、资源缓存、命令缓冲、线程池与计时器
//
// runtime 通过回调使用其他模块，本文件不直接依赖 io/render。
// ============================================================
#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "softrenderer/runtime/command_buffer.h"
#include "softrenderer/runtime/log.h"
#include "softrenderer/runtime/profiler.h"
#include "softrenderer/runtime/resource_cache.h"
#include "softrenderer/runtime/thread_pool.h"
#include "softrenderer/resource/texture.h"
#include "support/benchmark.h"

using namespace sr;

// ==================== 内置日志格式化 ====================
//
// sr::formatLog 只在内置（非 spdlog）模式下存在；它从 Log::log 中拆出来
// 就是为了能直接断言格式串，而不必去捕获 stderr。
#ifndef SR_USE_SPDLOG

TEST(Log, formatLog_noArgs_returnsFormatString)
{
    EXPECT_EQ(sr::formatLog("no placeholder"), "no placeholder");
    EXPECT_EQ(sr::formatLog(""), "");
}

TEST(Log, formatLog_substitutesBracesInOrder)
{
    EXPECT_EQ(sr::formatLog("a {} b", 42), "a 42 b");
    EXPECT_EQ(sr::formatLog("{} + {} = {}", 1, 2, 3), "1 + 2 = 3");
    EXPECT_EQ(sr::formatLog("{}x{}", 3, 4), "3x4");
}

// 关键回归：std::string 必须能直接传。
// 旧实现用 C 可变参，传 std::string 是 UB（MSVC 报 C4840）。

TEST(Log, formatLog_acceptsNonPodTypes)
{
    EXPECT_EQ(sr::formatLog("path '{}'", std::string("x.png")), "path 'x.png'");
    EXPECT_EQ(sr::formatLog("{} and {}", std::string("a"), 7), "a and 7");
}

// 旧实现把格式串交给 vfprintf，而约定写的是 {}，所以占位符从不被替换。
// 本用例锁定「参数不得静默丢失」这个行为。

TEST(Log, formatLog_extraArgsAreAppendedNotDropped)
{
    EXPECT_EQ(sr::formatLog("only {}", 1, 2), "only 1 2");
    EXPECT_EQ(sr::formatLog("none", 5), "none 5");
}

// 占位符多于参数时降级为保留字面量 {}，而非像 fmt 那样抛异常或崩溃。

TEST(Log, formatLog_extraBracesAreKeptLiteral)
{
    EXPECT_EQ(sr::formatLog("{} and {}", 1), "1 and {}");
    // 未匹配的 {} 在串中间与串尾都应原样保留
    EXPECT_EQ(sr::formatLog("a {} b {} c", 1), "a 1 b {} c");
    EXPECT_EQ(sr::formatLog("{}{}", 1), "1{}");
}

// 旧实现把格式串直接交给 vfprintf，文本里的字面 '%' 会被当转换符解析 → UB。
// 现在消息经单个 %s 输出，字面 '%' 安全。

TEST(Log, formatLog_literalPercentIsSafe)
{
    EXPECT_EQ(sr::formatLog("100% done"), "100% done");
    EXPECT_EQ(sr::formatLog("100% {} ok", 1), "100% 1 ok");
    EXPECT_EQ(sr::formatLog("%s %d %f 不应被解析", 1), "%s %d %f 不应被解析 1");
}

// 等级名映射：旧实现用常量数组且**漏了 Off**，传 Off 会越界读。

TEST(Log, levelName_coversAllEnumValues)
{
    EXPECT_STREQ(sr::levelName(sr::LogLevel::Trace), "TRACE");
    EXPECT_STREQ(sr::levelName(sr::LogLevel::Debug), "DEBUG");
    EXPECT_STREQ(sr::levelName(sr::LogLevel::Info), "INFO ");
    EXPECT_STREQ(sr::levelName(sr::LogLevel::Warn), "WARN ");
    EXPECT_STREQ(sr::levelName(sr::LogLevel::Error), "ERROR");
    EXPECT_STREQ(sr::levelName(sr::LogLevel::Critical), "CRIT ");
    EXPECT_STREQ(sr::levelName(sr::LogLevel::Off), "OFF  ");
    // 越界的枚举值不得崩溃（旧实现会数组越界）
    EXPECT_STREQ(sr::levelName(static_cast<sr::LogLevel>(99)), "?????");
}

TEST(Log, setLevel_roundTrips)
{
    const sr::LogLevel saved = sr::Log::level();
    sr::Log::setLevel(sr::LogLevel::Error);
    EXPECT_EQ(sr::Log::level(), sr::LogLevel::Error);
    sr::Log::setLevel(saved);  // 测试必须独立，不得泄露全局状态
    EXPECT_EQ(sr::Log::level(), saved);
}

#endif // !SR_USE_SPDLOG

// ==================== 像素回归工具 ====================

// regression::compare 是全部端到端回归测试的判定核心。
// 若它恒返回 passed=true，那么 tests/integration 的三个场景就全部是假通过。
// 本套件直接验证它的判定逻辑，与基准图文件无关。
//
// 通过判据（见 pixel_regression.h）：不匹配像素占比 < 0.1% **且** 平均差 < 0.5。
// 两个条件是 **且** 关系，下面分别验证各自能单独触发失败。

TEST(Benchmark, TimerRecords)
{
    sr::Bench::instance().clear();
    {
        sr::Timer t("test_timer");
        volatile int x = 0;
        for (int i = 0; i < 1000; ++i)
            x += i;
    }
    auto& results = sr::Bench::instance().results();
    ASSERT_FALSE(results.empty());
    EXPECT_EQ(results.back().name, "test_timer");
    EXPECT_GT(results.back().totalUs, 0.0);
}

// ==================== 资源缓存（Texture 作为样本资源类型） ====================

TEST(ResourceCache, FactoryResultIsCachedUntilReleased)
{
    sr::ResourceCache::instance().clear();
    int creations = 0;
    auto factory = [&creations] {
        ++creations;
        return std::make_shared<Texture>();
    };
    auto first = sr::ResourceCache::instance().load<Texture>("test", factory);
    auto second = sr::ResourceCache::instance().load<Texture>("test", factory);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first, second);
    EXPECT_EQ(creations, 1);
    EXPECT_EQ(sr::ResourceCache::instance().size(), 1u);

    first.reset();
    second.reset();
    sr::ResourceCache::instance().purge();
    EXPECT_EQ(sr::ResourceCache::instance().size(), 0u);
}

TEST(ResourceCache, FailedFactoryResultIsNotCached)
{
    sr::ResourceCache::instance().clear();
    int calls = 0;
    auto failing = [&calls] {
        ++calls;
        return std::shared_ptr<Texture>{};
    };
    EXPECT_EQ(sr::ResourceCache::instance().load<Texture>("broken", failing), nullptr);
    EXPECT_EQ(sr::ResourceCache::instance().load<Texture>("broken", failing), nullptr);
    EXPECT_EQ(calls, 2); // 失败不会固化成永久结果
    EXPECT_EQ(sr::ResourceCache::instance().size(), 0u);
}

// 并发首次加载同一资源只能构建一次（single-flight）。
// 构建一份大贴图/模型可能要几百毫秒，8 个线程各自 miss 一次就是 8 倍。
TEST(ResourceCache, ConcurrentFirstLoadBuildsOnce)
{
    sr::ResourceCache::instance().clear();
    std::atomic<int> builds{0};
    const int threads = 8;
    std::vector<std::shared_ptr<Texture>> results(static_cast<size_t>(threads));
    std::vector<std::thread> workers;
    workers.reserve(static_cast<size_t>(threads));
    for (int t = 0; t < threads; ++t)
        workers.emplace_back([&, t] {
            results[size_t(t)] = sr::ResourceCache::instance().load<Texture>("concurrent", [&] {
                builds.fetch_add(1, std::memory_order_relaxed);
                // 拉大 miss 窗口，让所有线程都落在“未命中”分支上
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                return std::make_shared<Texture>();
            });
        });
    for (std::thread& worker : workers)
        worker.join();

    EXPECT_EQ(builds.load(), 1);
    ASSERT_NE(results.front(), nullptr);
    for (const auto& result : results)
    {
        ASSERT_NE(result, nullptr);
        EXPECT_EQ(result.get(), results.front().get());
    }
    results.clear();
    sr::ResourceCache::instance().clear();
}

// ==================== 命令缓冲 ====================

TEST(CommandBuffer, SequentialExecution)
{
    sr::CommandBuffer cmd;
    int counter = 0;
    cmd.record([&]() { counter += 1; });
    cmd.record([&]() { counter += 10; });
    cmd.record([&]() { counter += 100; });
    cmd.executeSequential();
    EXPECT_EQ(counter, 111);
}

TEST(CommandBuffer, ParallelExecution)
{
    sr::CommandBuffer cmd;
    std::atomic<int> counter{0};
    for (int i = 0; i < 100; ++i)
        cmd.record([&]() { counter.fetch_add(1); });
    cmd.executeParallel(4);
    EXPECT_EQ(counter.load(), 100);
}

// 串行与并行的异常语义不同，且都是有意的：
//   串行 → 遇异常立即中止，后续命令不执行（本用例）
//   并行 → 一个失败不阻止其他命令，全部跑完后再抛
//          （ThreadFailures.executeParallel_failure_attemptsAllCommandsEvenWithOneThread）
// 两种语义都需被锁定，否则改一处就可能静默改变另一处的行为。
TEST(CommandBuffer, SequentialStopsAtFirstFailure)
{
    sr::CommandBuffer cmd;
    std::atomic<int> executed{0};
    cmd.record([&]() { executed.fetch_add(1); });
    cmd.record([]() { throw std::runtime_error("boom"); });
    cmd.record([&]() { executed.fetch_add(1); });

    EXPECT_THROW(cmd.executeSequential(), std::runtime_error);
    EXPECT_EQ(executed.load(), 1); // 第三个命令不应执行
}

TEST(ThreadPool, SubmitAndWait)
{
    sr::ThreadPool pool(4);
    std::atomic<int> sum{0};
    for (int i = 0; i < 50; ++i)
        pool.submit([&]() { sum.fetch_add(1); });
    pool.waitAll();
    EXPECT_EQ(sum.load(), 50);
}

// ==================== Profiler ====================
// Profiler 此前只有压测场景覆盖，单元测试里一条都没有。它是进程级单例，
// 每个用例用 RAII 守卫恢复 enabled 并清空事件，避免相互污染。
namespace
{
struct ProfilerTestGuard
{
    bool previous = sr::Profiler::instance().isEnabled();
    ProfilerTestGuard()
    {
        sr::Profiler::instance().clear();
        sr::Profiler::instance().setEnabled(true);
    }
    ~ProfilerTestGuard()
    {
        sr::Profiler::instance().setEnabled(previous);
        sr::Profiler::instance().clear();
    }
    ProfilerTestGuard(const ProfilerTestGuard&) = delete;
    ProfilerTestGuard& operator=(const ProfilerTestGuard&) = delete;
};
} // namespace

TEST(Profiler, EnabledRecordsOneEventPerScope)
{
    ProfilerTestGuard guard;
    {
        SR_PROFILE_SCOPE("unit_scope");
    }
    EXPECT_EQ(Profiler::instance().eventCount(), 1u);
}

TEST(Profiler, DisabledRecordsNothing)
{
    ProfilerTestGuard guard;
    Profiler::instance().setEnabled(false);
    {
        SR_PROFILE_SCOPE("unit_scope");
    }
    EXPECT_EQ(Profiler::instance().eventCount(), 0u);
}

TEST(Profiler, NestedScopesRecordEachLevel)
{
    ProfilerTestGuard guard;
    {
        SR_PROFILE_SCOPE("outer");
        {
            SR_PROFILE_SCOPE("inner");
        }
    }
    EXPECT_EQ(Profiler::instance().eventCount(), 2u);
}

TEST(Profiler, UnpairedEndScopeIsIgnored)
{
    ProfilerTestGuard guard;
    EXPECT_NO_THROW(Profiler::instance().endScope());
    EXPECT_EQ(Profiler::instance().eventCount(), 0u);
}

TEST(Profiler, MaxEventsDropsAndCounts)
{
    ProfilerTestGuard guard;
    Profiler::instance().setMaxEvents(2);
    for (int i = 0; i < 5; ++i)
    {
        SR_PROFILE_SCOPE("scope");
    }
    const size_t kept = Profiler::instance().eventCount();
    const size_t dropped = Profiler::instance().droppedEvents();
    Profiler::instance().setMaxEvents(1000000);
    EXPECT_EQ(kept, 2u);
    EXPECT_EQ(dropped, 3u);
}

// ==================== Mat3（2D 仿射）====================

