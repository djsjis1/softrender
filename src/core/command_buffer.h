#pragma once
#include <vector>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>

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
// 注意：命令之间必须无数据依赖（不同物体/不同 tile 的渲染）。
//
// 【命名空间】CommandBuffer 与 ThreadPool 已迁入 namespace sr（ADR-001 第 2 批）。
// 引用方：仅 tests/test_core.cpp（stage12_threaded 用自己的 std::thread 分条）。
// ============================================================

namespace sr
{

class CommandBuffer {
public:
    using Command = std::function<void()>;

    // 录制一条命令
    void record(Command cmd) {
        commands_.push_back(std::move(cmd));
    }

    // 清空
    void clear() { commands_.clear(); }

    size_t size() const { return commands_.size(); }

    // 单线程顺序执行
    void executeSequential() {
        for (auto& cmd : commands_) cmd();
    }

    // 多线程并行执行（命令间无依赖时安全）
    void executeParallel(int numThreads = 0) {
        if (numThreads <= 0)
            numThreads = std::max(1u, std::thread::hardware_concurrency());

        if (commands_.empty()) return;
        if (numThreads == 1 || commands_.size() == 1) {
            executeSequential();
            return;
        }

        std::atomic<size_t> nextIdx{0};
        auto worker = [&]() {
            while (true) {
                size_t idx = nextIdx.fetch_add(1);
                if (idx >= commands_.size()) break;
                commands_[idx]();
            }
        };

        std::vector<std::thread> threads;
        int actualThreads = std::min(numThreads, int(commands_.size()));
        threads.reserve(actualThreads);
        for (int i = 0; i < actualThreads; ++i)
            threads.emplace_back(worker);
        for (auto& t : threads) t.join();
    }

private:
    std::vector<Command> commands_;
};

// ============================================================
// 线程池：避免每帧创建/销毁线程的开销
// ============================================================
class ThreadPool {
public:
    explicit ThreadPool(int numThreads = 0) : stop_(false) {
        if (numThreads <= 0)
            numThreads = std::max(1u, std::thread::hardware_concurrency());
        for (int i = 0; i < numThreads; ++i)
            workers_.emplace_back([this] { workerLoop(); });
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& w : workers_) w.join();
    }

    // 提交任务
    void submit(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.push(std::move(task));
        }
        cv_.notify_one();
    }

    // 等待所有任务完成
    void waitAll() {
        std::unique_lock<std::mutex> lock(mutex_);
        doneCv_.wait(lock, [this] { return tasks_.empty() && activeCount_ == 0; });
    }

    int threadCount() const { return int(workers_.size()); }

private:
    void workerLoop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                if (stop_ && tasks_.empty()) return;
                task = std::move(tasks_.front());
                tasks_.pop();
                ++activeCount_;
            }
            task();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                --activeCount_;
                if (tasks_.empty() && activeCount_ == 0)
                    doneCv_.notify_all();
            }
        }
    }

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable doneCv_;
    // 声明即初始化：stop_ 是线程池的**退出标志**，工作线程会在构造后立即
    // 读它。若构造函数漏了初始化（或将来改动遗漏），读到的就是不确定值，
    // 而且因为有多线程参与，表现为难重现的偶发挂死/提前退出。
    // engineering_rules.md：「线程必须有明确生命周期与可控退出标志」。
    bool stop_ = false;
    int activeCount_ = 0;
};

} // namespace sr
