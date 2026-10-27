#pragma once
#include <cstddef>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

// ============================================================
// 命令缓冲：录制渲染命令，多线程并行回放
//
// 设计参考：Vulkan CommandBuffer / Metal CommandEncoder 思想。
//
// 用法：
//   CommandBuffer cmd;
//   cmd.record([&]() { renderer.drawMesh(...); });
//   cmd.record([&]() { renderer.drawMesh(...); });
//   cmd.executeParallel(4); // 4 线程并行执行所有命令
//
// 注意：命令必须使用不重叠存储或显式同步；不同物体仍可能竞争同一 framebuffer。
//
// 【命名空间】CommandBuffer 与 ThreadPool 已迁入 namespace sr（ADR-001 第 2 批）。
// 引用方：tests/unit/{runtime,command_buffer}_test.cpp、benchmarks/stress/
// stress_scenarios.cpp、tests/integration/consumer/main.cpp。3D 分条并行由
// examples/stage14_threaded.cpp 自己的 std::thread 实现，不用本类。
// ============================================================

namespace sr {

class CommandBuffer
{
   public:
    using Command = std::function<void()>;

    // 录制一条命令
    void record(Command cmd)
    {
        if (!cmd)
            throw std::invalid_argument("empty command");
        commands_.push_back(std::move(cmd));
    }

    // 清空
    void clear() { commands_.clear(); }

    size_t size() const { return commands_.size(); }

    // 单线程顺序执行
    // 串行：按记录顺序执行；**遇异常立即中止**，剩余命令不执行。
    void executeSequential()
    {
        for (auto& cmd : commands_)
            cmd();
    }

    // 并行：命令间无依赖时安全。
    //
    // 【异常语义与串行不同，是有意的】一个命令失败**不会阻止其他命令执行**：
    // 并行命令通常是互不相干的区域，各自完成更有用；第一个异常会在全部
    // 命令跑完后抛给调用方。串行路径则相反（见 executeSequential）。
    // 两者差异由 ThreadFailures.executeParallel_failure_attemptsAllCommandsEvenWithOneThread
    // 与 CommandBuffer.SequentialStopsAtFirstFailure 两个用例锁定。
    void executeParallel(int numThreads = 0)
    {
        if (numThreads <= 0)
            numThreads = std::max(1u, std::thread::hardware_concurrency());

        if (commands_.empty())
            return;
        std::atomic<size_t> nextIdx{0};
        std::mutex failureMutex;
        std::exception_ptr failure;
        auto worker = [&]() {
            while (true)
            {
                size_t idx = nextIdx.fetch_add(1);
                if (idx >= commands_.size())
                    break;
                try
                {
                    commands_[idx]();
                }
                catch (...)
                {
                    std::lock_guard<std::mutex> lock(failureMutex);
                    if (!failure)
                        failure = std::current_exception();
                }
            }
        };

        std::vector<std::jthread> threads;
        size_t actualThreads = std::min(size_t(numThreads), commands_.size());
        threads.reserve(actualThreads);
        for (size_t i = 1; i < actualThreads; ++i)
            threads.emplace_back(worker);
        worker();
        for (auto& t : threads)
            t.join();
        if (failure)
            std::rethrow_exception(failure);
    }

   private:
    std::vector<Command> commands_;
};

}  // namespace sr
