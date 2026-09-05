#pragma once
#include <chrono>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

// ============================================================
// 内置性能基准框架（零依赖）
//
// 用法：
//   SR_BENCH("rasterize_triangle", [&]() {
//       raster_fast::fillTriangleFast(...);
//   });
//   sr::Bench::report(); // 打印所有结果
//
// 或手动：
//   sr::Timer t("my_scope");
//   ... code ...
//   // 析构时自动记录
//
// 企业级替代：google-benchmark（可通过 third/ 集成）
// ============================================================
namespace sr
{

    using Clock = std::chrono::high_resolution_clock;
    using Duration = std::chrono::duration<double, std::micro>; // 微秒

    struct BenchResult
    {
        std::string name;
        double totalUs; // 总耗时（微秒）
        int iterations;
        double avgUs() const { return iterations > 0 ? totalUs / iterations : 0; }
        double minUs;
        double maxUs;
    };

    class Bench
    {
    public:
        static Bench &instance()
        {
            static Bench b;
            return b;
        }

        void record(const std::string &name, double us, int iters = 1)
        {
            // 查找或创建
            for (auto &r : results_)
            {
                if (r.name == name)
                {
                    r.totalUs += us;
                    r.iterations += iters;
                    r.minUs = std::min(r.minUs, us);
                    r.maxUs = std::max(r.maxUs, us);
                    return;
                }
            }
            results_.push_back({name, us, iters, us, us});
        }

        void report() const
        {
            std::printf("\n===== Benchmark Results =====\n");
            std::printf("%-40s %10s %10s %10s %8s\n", "Name", "Avg(us)", "Min(us)", "Max(us)", "Iters");
            std::printf("%-40s %10s %10s %10s %8s\n", "----", "-------", "-------", "-------", "-----");
            for (auto &r : results_)
            {
                std::printf("%-40s %10.2f %10.2f %10.2f %8d\n",
                            r.name.c_str(), r.avgUs(), r.minUs, r.maxUs, r.iterations);
            }
            std::printf("=============================\n\n");
        }

        void clear() { results_.clear(); }
        const std::vector<BenchResult> &results() const { return results_; }

    private:
        std::vector<BenchResult> results_;
    };

    // RAII 计时器
    class Timer
    {
    public:
        explicit Timer(const std::string &name) : name_(name), start_(Clock::now()) {}
        ~Timer()
        {
            auto end = Clock::now();
            double us = Duration(end - start_).count();
            Bench::instance().record(name_, us);
        }

    private:
        std::string name_;
        Clock::time_point start_;
    };

    // 多次迭代基准
    inline double benchRun(const std::string &name, const std::function<void()> &fn, int iterations = 100)
    {
        // 预热
        fn();
        auto start = Clock::now();
        for (int i = 0; i < iterations; ++i)
            fn();
        auto end = Clock::now();
        double totalUs = Duration(end - start).count();
        Bench::instance().record(name, totalUs, iterations);
        return totalUs / iterations;
    }

} // namespace sr

// 便捷宏
#define SR_BENCH(name, code)        \
    do                              \
    {                               \
        sr::Timer _sr_timer_(name); \
        code;                       \
    } while (0)
#define SR_BENCH_RUN(name, fn, iters) sr::benchRun(name, fn, iters)
