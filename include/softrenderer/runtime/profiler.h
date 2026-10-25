#pragma once
#include <atomic>
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

    // 关闭时必须是空操作：业务代码可以到处包 SR_PROFILE_SCOPE，
    // 而多线程渲染时每次 begin/end 都加锁会让线程互相串行化。
    void beginScope(const char* name, const char* category = "render")
    {
        if (!enabled_.load(std::memory_order_relaxed))
            return;
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
        auto& stack = scopeStack();
        if (stack.empty())
            return;
        const bool recording = enabled_.load(std::memory_order_relaxed);
        ScopeData sd = std::move(stack.back());
        stack.pop_back();
        // 关闭状态下也要把栈弹干净：开启/关闭中途切换时不能留下错配的残留条目。
        if (!recording)
            return;
        auto end = Clock::now();

        TraceEvent ev;
        ev.name = std::move(sd.name);
        ev.category = sd.category;
        ev.phase = 'X';
        ev.timestamp_us = toUs(sd.start - epoch_);
        ev.duration_us = toUs(end - sd.start);
        ev.thread_id = sd.thread_id;
        ev.process_id = 1;
        std::lock_guard<std::mutex> lock(mutex_);
        // 无上限时长时间运行会把事件堆到内存里（20 万条约 16MB，
        // flush 还要再拷一份）。超出上限就丢弃并计数，而不是无限增长。
        if (events_.size() >= maxEvents_)
        {
            ++dropped_;
            return;
        }
        events_.push_back(std::move(ev));
    }

    // 事件数上限（含已记录数）。默认 100 万条，约 80MB。
    void setMaxEvents(size_t n)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        maxEvents_ = n;
    }

    size_t droppedEvents() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return dropped_;
    }

    // 输出 Chrome Trace JSON
    //
    // 先在内存里拼完整份文档再一次 fwrite：旧实现逐事件 fprintf，
    // 200k 事件时 25 MB 要经过 200k 次 libc 格式化 + 缓冲检查，实测占了总耗时的 90%。
    void flush(const char* path)
    {
        // 先在锁内拷一份再写盘：持锁做文件 I/O 会把所有正在记录的线程卡住。
        std::vector<TraceEvent> events;
        size_t dropped = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            events = events_;
            dropped = dropped_;
        }
        FILE* f = std::fopen(path, "w");
        if (!f)
            return;

        std::string out;
        out.reserve(64 + events.size() * 160);
        out += "{\"traceEvents\":[\n";
        for (size_t i = 0; i < events.size(); ++i)
        {
            const auto& e = events[i];
            // name/category 必须转义：引号与反斜杠会产出非法文档。
            out += "{\"name\":\"";
            appendJsonEscaped(out, e.name);
            out += "\",\"cat\":\"";
            appendJsonEscaped(out, e.category);
            // 时间戳用整数微秒：MSVC 的 "%.3f" 每次约 3 μs（200k 事件就是 600 ms），
            // 而 trace 的时间精度到微秒已经足够。
            const long long ts = static_cast<long long>(e.timestamp_us + 0.5);
            const long long dur = static_cast<long long>(e.duration_us + 0.5);
            // 其余字段全部手写：200k 次 snprintf 的格式串解析本身就是大头。
            out += "\",\"ph\":\"";
            out += e.phase;
            out += "\",\"ts\":";
            appendUnsigned(out, static_cast<unsigned long long>(ts));
            out += ",\"dur\":";
            appendUnsigned(out, static_cast<unsigned long long>(dur));
            out += ",\"pid\":";
            appendUnsigned(out, e.process_id);
            out += ",\"tid\":";
            appendUnsigned(out, e.thread_id);
            out += i + 1 < events.size() ? "},\n" : "}\n";
        }
        out += "],\"displayTimeUnit\":\"ms\",\"droppedEvents\":";
        out += std::to_string(dropped);
        out += "}\n";

        std::fwrite(out.data(), 1, out.size(), f);
        std::fclose(f);
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        events_.clear();
        dropped_ = 0;
    }

    size_t eventCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return events_.size();
    }

    // 启用/禁用 profiling。默认关闭（零开销）；需要 trace 时显式开启。
    // 关闭后 beginScope/endScope 只做一次原子读，不碰时钟、不加锁、不分配。
    //
    // 【限制】enabled 应在启动时一次性设定。运行期反复切换会让 scope 栈失衡：
    // 关闭期间 beginScope 不入栈，而 endScope 无法察觉这一点，重新开启后会弹到
    // 外层的记录帧——表现为外层时长偏大、内层事件完全丢失。要彻底修就得让关闭
    // 路径也入栈，那会破坏上面“不分配”的零开销目标，属于有意取舍。
    void setEnabled(bool e) { enabled_.store(e, std::memory_order_relaxed); }
    bool isEnabled() const { return enabled_.load(std::memory_order_relaxed); }

   private:
    Profiler() : epoch_(Clock::now()) {}

    // steady_clock 保证单调：high_resolution_clock 在部分平台上可被系统校时
    // 回拨，会导致 duration_us 为负或时间轴错乱。
    using Clock = std::chrono::steady_clock;
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

    // 无符号十进制追加。比 snprintf 快一个数量级（后者要解析格式串）。
    static void appendUnsigned(std::string& out, unsigned long long value)
    {
        char buffer[24];
        char* end = buffer + sizeof(buffer);
        char* p = end;
        do
        {
            *--p = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value != 0);
        out.append(p, size_t(end - p));
    }

    // JSON 字符串转义：引号、反斜杠与控制字符必须转义，否则 trace 文件无效。
    // 直接追加到目标缓冲，避免每个事件两次临时 std::string 分配。
    static void appendJsonEscaped(std::string& out, const std::string& in)
    {
        for (char c : in)
        {
            switch (c)
            {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
            {
                const auto byte = static_cast<unsigned char>(c);
                if (byte < 0x20)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", int(byte));
                    out += buf;
                }
                else
                {
                    out += c;
                }
            }
            }
        }
    }

    static uint32_t getThreadId()
    {
        // 简化：用 hash
        return static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()) & 0xFFFF);
    }

    std::atomic<bool> enabled_{false};
    size_t maxEvents_ = 1000000;
    size_t dropped_ = 0;
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

    // 作用域守卫必须不可复制：两份副本各自析构会重复 endScope，
    // 把外层的 scope 也弹掉。
    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;

    ~ProfileScope()
    {
        if (!active_)
            return;
        // 析构函数隐式 noexcept：endScope 内部要拷贝 name/category 并 push_back 事件，
        // 内存不足时会抛 bad_alloc。一旦从析构逸出就是 std::terminate，而且调用方
        // 无法捕获。性能剖析是辅助功能，不能因为“记不下来一条事件”而终止进程。
        // 这里不发日志：日志本身也可能分配，且在析构里再抛一次。
        try
        {
            Profiler::instance().endScope();
        }
        catch (...)
        {
        }
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
