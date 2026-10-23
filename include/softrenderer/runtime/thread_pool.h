#pragma once
#include <algorithm>
#include <condition_variable>
#include <exception>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace sr
{

// ============================================================
// 线程池：避免每帧创建/销毁线程的开销
// ============================================================
class ThreadPool
{
   public:
    explicit ThreadPool(int numThreads = 0) : stop_(false)
    {
        if (numThreads <= 0)
            numThreads = std::max(1u, std::thread::hardware_concurrency());
        workers_.reserve(size_t(numThreads));
        try
        {
            for (int i = 0; i < numThreads; ++i)
                workers_.emplace_back([this] { workerLoop(); });
        }
        catch (...)
        {
            shutdown();
            throw;
        }
    }

    ~ThreadPool() { shutdown(); }

    // 提交任务
    void submit(std::function<void()> task)
    {
        if (!task)
            throw std::invalid_argument("empty task");
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.push(std::move(task));
        }
        cv_.notify_one();
    }

    // Exceptions belong to the returned future, not waitAll(). Supports move-only callables.
    template <class F>
    auto submitFuture(F&& function) -> std::future<std::invoke_result_t<std::decay_t<F>&>>
    {
        using Result = std::invoke_result_t<std::decay_t<F>&>;
        auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<F>(function));
        auto result = task->get_future();
        submit([task] { (*task)(); });
        return result;
    }

    // 等待空闲并消费首个 submit 异常；并发等待者中仅一个收到该异常。
    void waitAll()
    {
        if (currentPool_ == this)
            throw std::logic_error("worker cannot wait on its own pool");
        std::unique_lock<std::mutex> lock(mutex_);
        doneCv_.wait(lock, [this] { return tasks_.empty() && activeCount_ == 0; });
        auto failure = std::exchange(failure_, {});
        lock.unlock();
        if (failure)
            std::rethrow_exception(failure);
    }

    int threadCount() const { return int(workers_.size()); }

   private:
    void shutdown() noexcept
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& w : workers_)
            if (w.joinable())
                w.join();
    }
    void workerLoop()
    {
        currentPool_ = this;
        while (true)
        {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                if (stop_ && tasks_.empty())
                    return;
                task = std::move(tasks_.front());
                tasks_.pop();
                ++activeCount_;
            }
            std::exception_ptr failure;
            try
            {
                task();
            }
            catch (...)
            {
                failure = std::current_exception();
            }
            // Destroy captures before reporting idle; capture cleanup can release owned resources.
            task = {};
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (failure && !failure_)
                    failure_ = failure;
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
    std::exception_ptr failure_;
    inline static thread_local ThreadPool* currentPool_ = nullptr;
};

} // namespace sr
