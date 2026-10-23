/** @file command_buffer_test.cpp
 *  @brief 任务异常、等待语义与生命周期回归。
 */
#include "softrenderer/runtime/command_buffer.h"
#include "softrenderer/runtime/thread_pool.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace sr;

namespace sr::tests {
TEST(ThreadFailures, waitAll_throwingTask_reportsOnCallerWithoutTerminating)
{
    ASSERT_EXIT(
        {
            int result = 1;
            {
                sr::ThreadPool pool(2);
                pool.submit([] { throw std::runtime_error("task failed"); });
                try
                {
                    pool.waitAll();
                }
                catch (const std::runtime_error&)
                {
                    result = 0;
                }
            }
            std::_Exit(result);
        },
        ::testing::ExitedWithCode(0), "");
}

TEST(ThreadFailures, executeParallel_throwingCommand_reportsOnCallerWithoutTerminating)
{
    ASSERT_EXIT(
        {
            sr::CommandBuffer commands;
            commands.record([] { throw std::runtime_error("command failed"); });
            commands.record([] {});
            try
            {
                commands.executeParallel(2);
            }
            catch (const std::runtime_error&)
            {
                std::_Exit(0);
            }
            std::_Exit(1);
        },
        ::testing::ExitedWithCode(0), "");
}
TEST(ThreadFailures, waitAll_failure_drainsAndConsumesErrorOnce)
{
    ThreadPool pool(2);
    std::atomic<int> count{0};
    pool.submit([] { throw std::runtime_error("failed"); });
    for (int i = 0; i < 100; ++i)
        pool.submit([&] { ++count; });
    EXPECT_THROW(pool.waitAll(), std::runtime_error);
    EXPECT_EQ(count.load(), 100);
    EXPECT_NO_THROW(pool.waitAll());
    pool.submit([&] { ++count; });
    pool.waitAll();
    EXPECT_EQ(count.load(), 101);
}
TEST(ThreadFailures, submitFuture_moveOnlyAndFailure_reportsOnFuture)
{
    ThreadPool pool(2);
    auto result = pool.submitFuture([value = std::make_unique<int>(42)] { return *value; });
    auto failed = pool.submitFuture([]() -> int { throw std::runtime_error("future"); });
    EXPECT_NO_THROW(pool.waitAll());
    EXPECT_EQ(result.get(), 42);
    EXPECT_THROW(failed.get(), std::runtime_error);
}
TEST(ThreadFailures, waitAll_fromOwnWorker_reportsInsteadOfDeadlocking)
{
    ThreadPool pool(1);
    auto result = pool.submitFuture([&] { pool.waitAll(); });
    EXPECT_THROW(result.get(), std::logic_error);
    EXPECT_NO_THROW(pool.waitAll());
}
TEST(ThreadFailures, destructor_pendingTasks_drainsAndReleasesCaptures)
{
    std::atomic<int> count{0};
    auto resource = std::make_shared<int>(1);
    std::weak_ptr<int> observer = resource;
    {
        ThreadPool pool(2);
        for (int i = 0; i < 100; ++i)
            pool.submit([&, resource] { count.fetch_add(*resource); });
        resource.reset();
    }
    EXPECT_EQ(count.load(), 100);
    EXPECT_TRUE(observer.expired());
}
TEST(ThreadFailures, submit_multipleProducers_executesEachTaskOnce)
{
    ThreadPool pool(4);
    std::atomic<int> count{0};
    std::vector<std::jthread> producers;
    for (int i = 0; i < 4; ++i)
        producers.emplace_back([&] {
            for (int j = 0; j < 100; ++j)
                pool.submit([&] { ++count; });
        });
    for (auto& producer : producers)
        producer.join();
    pool.waitAll();
    EXPECT_EQ(count.load(), 400);
}
TEST(ThreadFailures, emptyTasks_rejectedSynchronously)
{
    ThreadPool pool(1);
    CommandBuffer commands;
    EXPECT_THROW(pool.submit({}), std::invalid_argument);
    EXPECT_THROW(commands.record({}), std::invalid_argument);
    EXPECT_EQ(commands.size(), 0u);
}
TEST(ThreadFailures, executeParallel_failure_attemptsAllCommandsEvenWithOneThread)
{
    for (int threads : {1, 4})
    {
        CommandBuffer commands;
        std::atomic<int> count{0};
        commands.record([] { throw std::runtime_error("command"); });
        for (int i = 0; i < 20; ++i)
            commands.record([&] { ++count; });
        EXPECT_THROW(commands.executeParallel(threads), std::runtime_error);
        EXPECT_EQ(count.load(), 20);
    }
}
}  // namespace sr::tests
