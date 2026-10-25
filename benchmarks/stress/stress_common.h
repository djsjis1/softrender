#pragma once
// ============================================================
// 压测公共设施：全局计数、报告、临时文件、场景注册表
//
// 【为什么拆出来】原压测程序是单文件 4600 行：工具函数、98 个场景、注册表、
// CLI 全挤在一个 TU 里，新增场景要在函数堆里找位置、注册表还要手工对齐顺序。
// 现在场景按主题分文件编译，各自用 SR_SCENARIO() 在静态初始化期登记，
// main 只负责 CLI 与汇总。
//
// 【注册顺序】场景按登记顺序执行；同一文件内按书写顺序，跨文件由链接顺序决定。
// --list 输出即真实执行顺序，新增场景不依赖人工维护注册表。
//
// 【使用】
//   #include "stress_common.h"
//   namespace stress {
//   static void stress_my_thing() { report("my_thing", ok, ms, "detail"); }
//   SR_SCENARIO("my_thing", stress_my_thing, /*heavy=*/false);
//   }
// ============================================================
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#endif

#include "softrenderer/core/arena.h"
#include "softrenderer/core/float_framebuffer.h"
#include "softrenderer/core/framebuffer.h"
#include "softrenderer/core/framebuffer_convert.h"
#include "softrenderer/core/gbuffer.h"
#include "softrenderer/core/msaa_framebuffer.h"
#include "softrenderer/core/pixel_format.h"
#include "softrenderer/io/gltf_loader.h"
#include "softrenderer/io/image_loader.h"
#include "softrenderer/io/image_write.h"
#include "softrenderer/io/obj_loader.h"
#include "softrenderer/io/tga.h"
#include "softrenderer/math/transform.h"
#include "softrenderer/render/draw2d/blit2d.h"
#include "softrenderer/render/draw2d/renderer2d.h"
#include "softrenderer/render/draw2d/shape2d.h"
#include "softrenderer/render/pipeline/clip.h"
#include "softrenderer/render/pipeline/msaa_renderer.h"
#include "softrenderer/render/pipeline/render_queue.h"
#include "softrenderer/render/pipeline/renderer.h"
#include "softrenderer/render/postprocess/post_processing.h"
#include "softrenderer/render/raster/rasterizer_fast.h"
#include "softrenderer/render/shader/deferred_lighting.h"
#include "softrenderer/render/shader/pbr_shader.h"
#include "softrenderer/render/shader/shaders.h"
#include "softrenderer/resource/cubemap.h"
#include "softrenderer/resource/mesh_generator.h"
#include "softrenderer/resource/texture.h"
#include "softrenderer/resource/texture_generator.h"
#include "softrenderer/runtime/command_buffer.h"
#include "softrenderer/runtime/log.h"
#include "softrenderer/runtime/profiler.h"
#include "softrenderer/runtime/resource_cache.h"
#include "softrenderer/runtime/thread_pool.h"
#include "softrenderer/scene/animation_player.h"
#include "softrenderer/scene/camera.h"
#include "softrenderer/scene/camera2d.h"
#include "softrenderer/scene/particle_system.h"
#include "softrenderer/scene/scene_asset.h"
#include "softrenderer/scene/skeleton.h"

namespace stress
{

using Clock = std::chrono::steady_clock;

struct ScenarioResult
{
    std::string id;
    std::string name;
    std::string detail;
    double ms = 0.0;
    bool ok = false;
};

struct Scenario
{
    const char* id;
    void (*run)();
    bool heavy;
};

// 函数内 static：跨 TU 注册发生在静态初始化期，必须避免全局对象初始化顺序问题。
inline std::vector<Scenario>& registry()
{
    static std::vector<Scenario> scenarios;
    return scenarios;
}

struct Registrar
{
    Registrar(const char* id, void (*run)(), bool heavy) { registry().push_back({id, run, heavy}); }
};

inline int g_failures = 0;
inline int g_regressions = 0;
inline Clock::time_point g_start = Clock::now();

// 场景表驱动执行：g_currentScenario 让 report() 在不改每个场景签名的前提下
// 把结果挂到对应场景 ID 上（过滤、JSON、基线对比都依赖这个 ID）。
inline const char* g_currentScenario = "";

inline std::vector<ScenarioResult> g_results;

// 基线：上一次 --json 输出的场景耗时。只用于发现性能回退，不参与正确性判定。
inline std::map<std::string, double> g_baseline;
inline bool g_strictBaseline = false;

// 回退判定阈值：既要有相对余量（噪声/频率漂移），又要有绝对余量
// （毫秒级场景的抖动百分比天然很大）。
inline constexpr double kRegressionRatio = 1.5;
inline constexpr double kRegressionFloorMs = 5.0;

inline double msSince(Clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

inline std::string jsonEscape(const std::string& in)
{
    std::string out;
    out.reserve(in.size() + 8);
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
                if (static_cast<unsigned char>(c) < 0x20)
                    out += ' ';
                else
                    out += c;
        }
    }
    return out;
}

inline void report(const char* name, bool ok, double ms, const std::string& detail)
{
    if (!ok)
        ++g_failures;

    auto base = g_baseline.find(name);
    if (base != g_baseline.end())
    {
        const double limit = base->second * kRegressionRatio + kRegressionFloorMs;
        if (ms > limit)
        {
            ++g_regressions;
            std::printf("[SLOW] %-27s %9.2f ms  baseline=%.2f ms (x%.2f)\n", name, ms, base->second,
                        base->second > 0.0 ? ms / base->second : 0.0);
        }
    }

    g_results.push_back({g_currentScenario, name, detail, ms, ok});
    std::printf("%s %-28s %9.2f ms  %s\n", ok ? "[OK ]" : "[BAD]", name, ms, detail.c_str());
}

// 写文件辅助：场景需要临时模型/图片头，统一走二进制一次写，避免逐行流输出。
inline bool writeFileBytes(const std::string& path, const void* data, size_t bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out.write(static_cast<const char*>(data), std::streamsize(bytes));
    return out.good();
}

inline bool writeFileText(const std::string& path, const std::string& text)
{
    return writeFileBytes(path, text.data(), text.size());
}

// Profiler 默认关闭（零开销），需要事件的场景必须显式打开并在退出时恢复，
// 否则会静默得到 events=0，让"全绿"变成假绿。
struct ProfilerEnabledGuard
{
    bool previous;
    explicit ProfilerEnabledGuard(bool enable = true) : previous(sr::Profiler::instance().isEnabled())
    {
        sr::Profiler::instance().setEnabled(enable);
    }
    ~ProfilerEnabledGuard() { sr::Profiler::instance().setEnabled(previous); }
    ProfilerEnabledGuard(const ProfilerEnabledGuard&) = delete;
    ProfilerEnabledGuard& operator=(const ProfilerEnabledGuard&) = delete;
};

inline sr::PBRShader makePbr(int w, int h)
{
    sr::PBRShader s;
    sr::Camera cam;
    cam.position = {0, 0, 4};
    cam.aspect = float(w) / float(h);
    s.view = cam.viewMatrix();
    s.proj = cam.projMatrix();
    s.cameraPos = cam.position;
    s.lightDir = sr::Vec3f{0.3f, 0.8f, 0.5f}.normalized();
    return s;
}

inline bool allFinite(const sr::Vec3f& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// 进程私有内存（字节）；非 Windows 返回 0（调用方按"无法测量"处理）
inline size_t processPrivateBytes()
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
        return size_t(pmc.PrivateUsage);
#endif
    return 0;
}

}  // namespace stress

// 在场景所在 TU 内登记；fn 为同 TU 的 static 函数地址。
#define SR_SCENARIO(id_, fn_, heavy_) static const ::stress::Registrar sr_reg_##fn_{id_, fn_, heavy_}
