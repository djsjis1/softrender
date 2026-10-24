#pragma once
#include <cstdint>
#include <cstddef>
#include <utility>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ============================================================
// Chrome Tracing Profiler（零依赖）
//
// 输出 Chrome Trace Event Format JSON，可直接在以下工具中查看：
//   - chrome://tracing
//   - Perfetto UI (ui.perfetto.dev)
//   - Edge/Chromium DevTools Performance 面板
//
// 用法：
//   1. 在函数/代码块中用 SR_PROFILE_SCOPE("name") 宏
//   2. 程序结束前调用 sr::Profiler::instance().flush("trace.json")
//
// 参考：Chrome Trace Event Format 规范
//   https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU
// ============================================================

namespace sr {

struct TraceEvent
{
    std::string name;
    std::string category;
    char phase;           // 'X' = complete event (duration)
    double timestamp_us;  // 微秒时间戳
    double duration_us;   // 持续时间（微秒）
    uint32_t thread_id;
    uint32_t process_id;
};

class Profiler
{
   public:
    static Profiler& instance()
    {
        static Profiler p;
        return p;
    }

    void beginScope(const char* name, const char* category = "render")
    {
        ScopeData sd;
        sd.name = name;
        sd.category = category;
        sd.start = Clock::now();
        sd.thread_id = getThreadId();
        // scope 栈是线程局部的：多线程并发嵌套 scope 不会互相错配
        scopeStack().push_back(std::move(sd));
    }

    void endScope()
    {
        auto end = Clock::now();
        auto& stack = scopeStack();
        if (stack.empty())
            return;
        ScopeData sd = std::move(stack.back());
        stack.pop_back();

        TraceEvent ev;
        ev.name = std::move(sd.name);
        ev.category = sd.category;
        ev.phase = 'X';
        ev.timestamp_us = toUs(sd.start - epoch_);
        ev.duration_us = toUs(end - sd.start);
        ev.thread_id = sd.thread_id;
        ev.process_id = 1;
        std::lock_guard<std::mutex> lock(mutex_);
        events_.push_back(std::move(ev));
    }

    // 输出 Chrome Trace JSON
    void flush(const char* path)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        FILE* f = std::fopen(path, "w");
        if (!f)
            return;
        std::fprintf(f, "{\"traceEvents\":[\n");
        for (size_t i = 0; i < events_.size(); ++i)
        {
            const auto& e = events_[i];
            std::fprintf(f,
                         "{\"name\":\"%s\",\"cat\":\"%s\",\"ph\":\"%c\","
                         "\"ts\":%.3f,\"dur\":%.3f,"
                         "\"pid\":%u,\"tid\":%u}%s\n",
                         e.name.c_str(), e.category.c_str(), e.phase, e.timestamp_us, e.duration_us, e.process_id,
                         e.thread_id, (i + 1 < events_.size()) ? "," : "");
        }
        std::fprintf(f, "],\"displayTimeUnit\":\"ms\"}\n");
        std::fclose(f);
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.clear();
    }

    size_t eventCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return events_.size();
    }

    // 启用/禁用 profiling（禁用时 beginScope/endScope 为空操作）
    void setEnabled(bool e) { enabled_ = e; }
    bool isEnabled() const { return enabled_; }

   private:
    Profiler() : epoch_(Clock::now()) {}

    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;

    struct ScopeData
    {
        std::string name;
        std::string category;
        TimePoint start;
        uint32_t thread_id;
    };

    // 每个线程独立的 scope 栈（避免跨线程共享导致嵌套错配）
    static std::vector<ScopeData>& scopeStack()
    {
        thread_local std::vector<ScopeData> stack;
        return stack;
    }

    static double toUs(std::chrono::nanoseconds ns) { return std::chrono::duration<double, std::micro>(ns).count(); }

    static uint32_t getThreadId()
    {
        // 简化：用 hash
        return static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()) & 0xFFFF);
    }

    bool enabled_ = true;
    TimePoint epoch_;
    std::vector<TraceEvent> events_;
    mutable std::mutex mutex_;
};

// RAII scope guard
class ProfileScope
{
   public:
    ProfileScope(const char* name, const char* category = "render")
    {
        if (Profiler::instance().isEnabled())
            Profiler::instance().beginScope(name, category);
        else
            active_ = false;
    }
    ~ProfileScope()
    {
        if (active_)
            Profiler::instance().endScope();
    }

   private:
    bool active_ = true;
};

}  // namespace sr

// 便捷宏（两级间接展开确保 __LINE__ 被替换成行号，避免同名变量冲突）
#define SR_CONCAT_IMPL(a, b) a##b
#define SR_CONCAT(a, b) SR_CONCAT_IMPL(a, b)
#define SR_PROFILE_SCOPE(name) sr::ProfileScope SR_CONCAT(_sr_profile_, __LINE__)(name)
#define SR_PROFILE_SCOPE_CAT(name, cat) sr::ProfileScope SR_CONCAT(_sr_profile_, __LINE__)(name, cat)
#define SR_PROFILE_FUNCTION() SR_PROFILE_SCOPE(__FUNCTION__)
