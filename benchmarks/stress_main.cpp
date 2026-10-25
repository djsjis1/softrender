// ============================================================
// SoftRenderer 压测程序（独立工程，只用公共头）
//
// 目的：在极端输入与重度负载下找出崩溃、挂死、越界、非有限值与性能峭壁。
// 每个场景独立 try/catch，输出 [OK]/[BAD] 与耗时、关键度量。
// 放在 build/stress 下构建，不进入版本库。
// ============================================================
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
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
#include "softrenderer/core/framebuffer.h"
#include "softrenderer/core/float_framebuffer.h"
#include "softrenderer/core/msaa_framebuffer.h"
#include "softrenderer/io/gltf_loader.h"
#include "softrenderer/io/image_loader.h"
#include "softrenderer/io/image_write.h"
#include "softrenderer/io/obj_loader.h"
#include "softrenderer/io/tga.h"
#include "softrenderer/math/transform.h"
#include "softrenderer/render/draw2d/blit2d.h"
#include "softrenderer/render/draw2d/renderer2d.h"
#include "softrenderer/render/draw2d/shape2d.h"
#include "softrenderer/render/raster/rasterizer_fast.h"
#include "softrenderer/render/pipeline/msaa_renderer.h"
#include "softrenderer/render/pipeline/render_queue.h"
#include "softrenderer/render/pipeline/renderer.h"
#include "softrenderer/render/postprocess/post_processing.h"
#include "softrenderer/render/shader/deferred_lighting.h"
#include "softrenderer/render/shader/pbr_shader.h"
#include "softrenderer/render/shader/shaders.h"
#include "softrenderer/resource/cubemap.h"
#include "softrenderer/resource/mesh_generator.h"
#include "softrenderer/resource/texture.h"
#include "softrenderer/resource/texture_generator.h"
#include "softrenderer/runtime/command_buffer.h"
#include "softrenderer/runtime/profiler.h"
#include "softrenderer/runtime/resource_cache.h"
#include "softrenderer/runtime/thread_pool.h"
#include "softrenderer/scene/animation_player.h"
#include "softrenderer/scene/camera.h"
#include "softrenderer/scene/scene_asset.h"
#include "softrenderer/scene/particle_system.h"
#include "softrenderer/scene/skeleton.h"

using namespace sr;
using Clock = std::chrono::steady_clock;

namespace
{
int g_failures = 0;
int g_regressions = 0;
Clock::time_point g_start = Clock::now();

// 场景表驱动执行：g_currentScenario 让 report() 在不改每个场景签名的前提下
// 把结果挂到对应场景 ID 上（过滤、JSON、基线对比都依赖这个 ID）。
const char* g_currentScenario = "";

struct ScenarioResult
{
    std::string id;
    std::string name;
    std::string detail;
    double ms = 0.0;
    bool ok = false;
};

std::vector<ScenarioResult> g_results;

// 基线：上一次 --json 输出的场景耗时。只用于发现性能回退，不参与正确性判定。
std::map<std::string, double> g_baseline;
bool g_strictBaseline = false;

// 回退判定阈值：既要有相对余量（噪声/频率漂移），又要有绝对余量
// （毫秒级场景的抖动百分比天然很大）。
constexpr double kRegressionRatio = 1.5;
constexpr double kRegressionFloorMs = 5.0;

double msSince(Clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

std::string jsonEscape(const std::string& in)
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

void report(const char* name, bool ok, double ms, const std::string& detail)
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
bool writeFileBytes(const std::string& path, const void* data, size_t bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out.write(static_cast<const char*>(data), std::streamsize(bytes));
    return out.good();
}

bool writeFileText(const std::string& path, const std::string& text)
{
    return writeFileBytes(path, text.data(), text.size());
}

// Profiler 默认关闭（零开销），需要事件的场景必须显式打开并在退出时恢复，
// 否则会静默得到 events=0，让“全绿”变成假绿。
struct ProfilerEnabledGuard
{
    bool previous;
    explicit ProfilerEnabledGuard(bool enable = true) : previous(Profiler::instance().isEnabled())
    {
        Profiler::instance().setEnabled(enable);
    }
    ~ProfilerEnabledGuard() { Profiler::instance().setEnabled(previous); }
    ProfilerEnabledGuard(const ProfilerEnabledGuard&) = delete;
    ProfilerEnabledGuard& operator=(const ProfilerEnabledGuard&) = delete;
};

PBRShader makePbr(int w, int h)
{
    PBRShader s;
    Camera cam;
    cam.position = {0, 0, 4};
    cam.aspect = float(w) / float(h);
    s.view = cam.viewMatrix();
    s.proj = cam.projMatrix();
    s.cameraPos = cam.position;
    s.lightDir = Vec3f{0.3f, 0.8f, 0.5f}.normalized();
    return s;
}

bool allFinite(const Vec3f& v)
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// 进程私有内存（字节）；非 Windows 返回 0（调用方按“无法测量”处理）
size_t processPrivateBytes()
{
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
        return size_t(pmc.PrivateUsage);
#endif
    return 0;
}
}  // namespace

// ---------------------------------------------------------------- 1
static void stress_render_heavy()
{
    const auto t0 = Clock::now();
    const int W = 1024, H = 1024;
    Framebuffer fb(W, H);
    Renderer r(W, H);
    const Mesh sphere = sr::meshgen::sphere(1.0f, 64, 48);
    auto shader = makePbr(W, H);
    r.clear(fb, Color(10, 12, 16));
    const int draws = 30;
    for (int i = 0; i < draws; ++i)
    {
        const Mat4 model = Mat4::translate(0.0f, 0.0f, -float(i % 5)) * Mat4::rotateY(0.3f * float(i));
        shader.model = model;
        shader.mvp = shader.proj * shader.view * model;
        shader.normalMatrix = model.inversed().transposed();
        r.drawMesh(fb, sphere, shader);
    }
    uint64_t hash = 1469598103934665603ull;
    for (int y = 0; y < H; y += 7)
        for (int x = 0; x < W; x += 7)
        {
            const Color c = fb.getPixel(x, y);
            hash = (hash ^ c.r) * 1099511628211ull;
        }
    const double ms = msSince(t0);
    report("render_1024x1024_x30", hash != 0, ms,
           "tris/draw=" + std::to_string(sphere.triangleCount()) + " total_tris=" +
               std::to_string(int64_t(sphere.triangleCount()) * draws));
}

// ---------------------------------------------------------------- 2
static void stress_render_hostile()
{
    const auto t0 = Clock::now();
    const int W = 320, H = 240;
    Framebuffer fb(W, H);
    Renderer r(W, H);
    auto shader = makePbr(W, H);

    const float bad[] = {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                         -std::numeric_limits<float>::infinity(), 1e30f, -1e30f, 5e5f, -5e5f, 0.0f, 1e-30f};
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> pick(0, int(sizeof(bad) / sizeof(bad[0])) - 1);

    Mesh mesh;
    const int tris = 5000;
    mesh.vertices.reserve(size_t(tris) * 3);
    for (int i = 0; i < tris; ++i)
        for (int k = 0; k < 3; ++k)
        {
            Vertex v;
            v.position = {bad[pick(rng)], bad[pick(rng)], bad[pick(rng)]};
            v.normal = {bad[pick(rng)], bad[pick(rng)], bad[pick(rng)]};
            v.uv = {bad[pick(rng)], bad[pick(rng)]};
            mesh.vertices.push_back(v);
            mesh.indices.push_back(int(mesh.vertices.size()) - 1);
        }
    r.clear(fb, Color(0, 0, 0));
    shader.model = Mat4::identity();
    shader.mvp = shader.proj * shader.view;
    shader.normalMatrix = Mat4::identity();
    r.drawMesh(fb, mesh, shader);

    int painted = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            if (fb.getPixel(x, y).r != 0)
                ++painted;
    const double ms = msSince(t0);
    report("render_hostile_5000tris", true, ms, "painted_px=" + std::to_string(painted));
}

// ---------------------------------------------------------------- 3
static void stress_texture_extremes()
{
    const auto t0 = Clock::now();
    const Texture tex(sr::texgen::checkerboard(64, 8));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const float coords[] = {nan, inf, -inf, 1e9f, -1e9f, 0.0f, 0.5f, 1.0f, -1.0f, 1e-30f};
    int badNearest = 0, badBilinear = 0, badTrilinear = 0, badUnified = 0;
    std::string firstBad;
    for (float u : coords)
        for (float v : coords)
        {
            if (!allFinite(tex.sampleNearest(u, v)))
            {
                ++badNearest;
                if (firstBad.empty())
                    firstBad = "nearest u=" + std::to_string(u) + " v=" + std::to_string(v);
            }
            if (!allFinite(tex.sampleBilinear(u, v)))
            {
                ++badBilinear;
                if (firstBad.empty())
                    firstBad = "bilinear u=" + std::to_string(u) + " v=" + std::to_string(v);
            }
            if (!allFinite(tex.sampleTrilinear(u, v, nan, inf, u, v)))
            {
                ++badTrilinear;
                if (firstBad.empty())
                    firstBad = "trilinear(nan dudx) u=" + std::to_string(u);
            }
            if (!allFinite(tex.sample(u, v, nan, nan, nan, nan)))
            {
                ++badUnified;
                if (firstBad.empty())
                    firstBad = "sample(nan derivatives) u=" + std::to_string(u);
            }
        }
    const double ms = msSince(t0);
    const int bad = badNearest + badBilinear + badTrilinear + badUnified;
    report("texture_extreme_uv", bad == 0, ms,
           "nearest=" + std::to_string(badNearest) + " bilinear=" + std::to_string(badBilinear) +
               " trilinear=" + std::to_string(badTrilinear) + " sample=" + std::to_string(badUnified) +
               (firstBad.empty() ? "" : "  first: " + firstBad));
}

// ---------------------------------------------------------------- 4
static void stress_texture_tiny()
{
    const auto t0 = Clock::now();
    sr::Image one;
    one.width = 1;
    one.height = 1;
    one.pixels = {Color(1, 2, 3, 4)};
    const Texture tex(one);
    int bad = 0;
    for (int i = -100; i <= 100; ++i)
    {
        const float u = float(i) * 0.37f;
        if (!allFinite(tex.sampleTrilinear(u, -u, 1.0f, 1.0f, 1.0f, 1.0f)))
            ++bad;
    }
    const double ms = msSince(t0);
    report("texture_1x1_trilinear", bad == 0, ms,
           "mipLevels=" + std::to_string(tex.mipLevels()) + " bad=" + std::to_string(bad));
}

// ---------------------------------------------------------------- 5
static void stress_postprocess_edges()
{
    const auto t0 = Clock::now();
    int bad = 0;

    // 1x1
    Framebuffer tiny(1, 1, Color(9, 9, 9));
    Framebuffer tinyOut(1, 1, Color(0, 0, 0));
    sr::postprocess::fxaa(tiny, tinyOut);

    FloatFramebuffer f1(1, 1);
    f1.clear();
    FloatFramebuffer fOut(1, 1);
    sr::postprocess::gaussianBlur(f1, fOut, true, -5);
    sr::postprocess::gaussianBlur(f1, fOut, true, 0);
    sr::postprocess::radialMotionBlur(f1, 1, 1, 5.0f, 0, fOut);
    sr::postprocess::radialMotionBlur(f1, 1, 1, 5.0f, 1, fOut);
    std::vector<float> shortDepth(1, 1.0f);
    sr::postprocess::depthOfField(f1, shortDepth, 64, 64, 1.0f, 0.0f, 4.0f, fOut);
    sr::postprocess::brightPass(f1, f1, 1.0f);
    if (!std::isfinite(fOut.getPixel(0, 0).x))
        ++bad;

    // 负尺寸与空输入
    Framebuffer zero(1, 1, Color(0, 0, 0));
    Framebuffer zeroOut(1, 1, Color(0, 0, 0));
    sr::postprocess::fxaa(zero, zeroOut);

    FloatFramebuffer fa(32, 32);
    fa.clear();
    FloatFramebuffer fb2(32, 32);
    std::vector<float> depth(4, 0.5f);  // 远短于 32*32
    sr::postprocess::depthOfField(fa, depth, 32, 32, 1.0f, std::numeric_limits<float>::quiet_NaN(), -3.0f, fb2);

    sr::postprocess::SSAO ssao;
    sr::postprocess::SSAOConfig cfg;
    cfg.sampleCount = -5;
    cfg.noiseSize = -3;
    cfg.radius = std::numeric_limits<float>::quiet_NaN();
    ssao.configure(cfg);
    std::vector<Vec3f> pos(4, Vec3f{0, 0, -1});
    std::vector<Vec3f> nrm(4, Vec3f{0, 0, 1});
    std::vector<float> d(4, 0.5f);
    std::vector<float> ao;
    ssao.compute(pos, nrm, d, 2, 2, Mat4::identity(), ao);
    if (ao.size() != 4)
        ++bad;

    const double ms = msSince(t0);
    report("postprocess_edge_inputs", bad == 0, ms, "bad=" + std::to_string(bad));
}

// ---------------------------------------------------------------- 6
static void stress_render_queue()
{
    const auto t0 = Clock::now();
    RenderQueue q;
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    const int n = 200000;
    std::vector<Vec3f> positions;
    positions.reserve(size_t(n));
    for (int i = 0; i < n; ++i)
    {
        DrawItem it;
        it.layer = (i % 5 == 0) ? RenderLayer::Transparent : RenderLayer::Opaque;
        it.objectIndex = i;
        it.materialId = i % 32;
        q.add(it);
        positions.push_back({dist(rng) * 100.0f, dist(rng) * 100.0f, dist(rng) * 100.0f});
    }
    // 掺入 NaN 距离
    positions[0] = {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};
    q.sort({0, 0, 0}, positions);

    bool orderOk = true;
    float lastOpaque = -1.0f;
    float lastTransparent = std::numeric_limits<float>::max();
    for (const DrawItem& it : q.items())
    {
        if (it.layer < RenderLayer::Transparent)
        {
            if (it.sortDistance < lastOpaque)
                orderOk = false;
            lastOpaque = it.sortDistance;
        }
        else
        {
            if (it.sortDistance > lastTransparent)
                orderOk = false;
            lastTransparent = it.sortDistance;
        }
    }
    const double ms = msSince(t0);
    report("renderqueue_sort_200k", orderOk, ms, orderOk ? "ordering ok" : "ORDER VIOLATION");
}

// ---------------------------------------------------------------- 7
static void stress_thread_pool()
{
    const auto t0 = Clock::now();
    ThreadPool pool(8);
    std::atomic<int> counter{0};
    const int tasks = 200000;
    for (int i = 0; i < tasks; ++i)
        pool.submit([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
    pool.waitAll();

    // 异常必须回到 future，且 waitAll 也能拿到首个异常
    auto f = pool.submitFuture([]() -> int { throw std::runtime_error("boom"); });
    bool futureThrew = false;
    try
    {
        f.get();
    }
    catch (const std::runtime_error&)
    {
        futureThrew = true;
    }
    pool.submit([] { throw std::logic_error("submit-path"); });
    bool waitThrew = false;
    try
    {
        pool.waitAll();
    }
    catch (const std::logic_error&)
    {
        waitThrew = true;
    }
    // 工作线程内等待自身必须被拒绝（不能死锁）
    std::atomic<bool> nestedRejected{false};
    auto nested = pool.submitFuture([&pool, &nestedRejected]() {
        try
        {
            pool.waitAll();
        }
        catch (const std::logic_error&)
        {
            nestedRejected = true;
        }
    });
    nested.get();

    const double ms = msSince(t0);
    const bool ok = counter.load() == tasks && futureThrew && waitThrew && nestedRejected.load();
    report("threadpool_200k", ok, ms,
           "count=" + std::to_string(counter.load()) + " futureEx=" + std::to_string(futureThrew) +
               " waitEx=" + std::to_string(waitThrew) + " nestedRejected=" + std::to_string(nestedRejected.load()));
}

// ---------------------------------------------------------------- 8
static void stress_skeleton_depth()
{
    const auto t0 = Clock::now();
    Skeleton skel;
    const int bones = 2000;
    skel.bones.resize(size_t(bones));
    for (int i = 0; i < bones; ++i)
    {
        skel.bones[size_t(i)].parent = i - 1;
        skel.bones[size_t(i)].localTransform = Mat4::translate(0.0f, 0.001f, 0.0f);
        skel.bones[size_t(i)].inverseBindMatrix = Mat4::identity();
    }
    const auto global = skel.computeGlobalTransforms();
    const auto skin = skel.computeSkinMatrices();
    const double ms = msSince(t0);
    report("skeleton_chain_2000", global.size() == size_t(bones) && !skin.empty(), ms,
           "bones=" + std::to_string(bones) + " result=" + std::to_string(global.size()));
}

// ---------------------------------------------------------------- 9
static void stress_particles()
{
    // 三个规模同一帧数：若每次 emit 都从头扫描空槽，耗时应随规模平方增长。
    const int sizes[] = {10000, 50000, 100000};
    std::string detail;
    bool ok = true;
    const auto t0 = Clock::now();
    std::string firstMs;
    for (int size : sizes)
    {
        EmitterConfig cfg;
        cfg.maxParticles = size;
        cfg.rate = float(size) * 10.0f;
        cfg.lifeMin = 0.2f;
        cfg.lifeMax = 0.5f;
        ParticleSystem ps(cfg);
        const auto ts = Clock::now();
        for (int i = 0; i < 20; ++i)
            ps.update(1.0f / 60.0f);
        const double ms = msSince(ts);
        detail += std::to_string(size) + "=" + std::to_string(int(ms)) + "ms ";
        if (size == 10000)
            firstMs = std::to_string(int(ms));
        if (ps.getAlive().empty())
            ok = false;
    }
    report("particles_emit_scaling", ok, msSince(t0), detail + "(20 frames each, base=" + firstMs + "ms)");
}

// ---------------------------------------------------------------- 10
static void stress_profiler()
{
    const auto t0 = Clock::now();
    ProfilerEnabledGuard profilerEnabled;
    auto& p = Profiler::instance();
    p.clear();
    const int n = 200000;
    const auto tRecord = Clock::now();
    for (int i = 0; i < n; ++i)
    {
        p.beginScope("scope_with_\"quotes\"_and_\\backslash", "cat\nline");
        p.endScope();
    }
    const double recordMs = msSince(tRecord);
    std::filesystem::create_directories("stress-out");
    const auto tFlush = Clock::now();
    p.flush("stress-out/trace.json");
    const double flushMs = msSince(tFlush);
    const auto size = std::filesystem::file_size("stress-out/trace.json");
    const double ms = msSince(t0);
    report("profiler_200k_events", p.eventCount() == size_t(n), ms,
           "events=" + std::to_string(p.eventCount()) + " record=" + std::to_string(int(recordMs)) +
               "ms flush=" + std::to_string(int(flushMs)) + "ms file=" + std::to_string(size) + "B");
    p.clear();
}

// ---------------------------------------------------------------- 11
static void stress_arena()
{
    const auto t0 = Clock::now();
    ArenaAllocator arena(4096);
    int nulls = 0;
    for (int frame = 0; frame < 200; ++frame)
    {
        for (int i = 0; i < 5000; ++i)
        {
            void* p = arena.alloc(size_t(64 + (i & 15)), size_t(1) << (i & 3));
            if (p == nullptr)
                ++nulls;
        }
        arena.reset();
    }
    const double ms = msSince(t0);
    report("arena_1M_allocs", nulls == 0, ms,
           "capacity=" + std::to_string(arena.capacity()) + " nulls=" + std::to_string(nulls));
}

// ---------------------------------------------------------------- 12
static void stress_draw2d()
{
    const auto t0 = Clock::now();
    const int W = 1024, H = 1024;
    Framebuffer fb(W, H, Color(0, 0, 0));
    Renderer2D r2d;
    r2d.begin(fb);
    r2d.setBlend(BlendMode::AlphaBlend);

    const auto tRects = Clock::now();
    for (int i = 0; i < 20000; ++i)
    {
        r2d.setColor(Color(uint8_t(i), uint8_t(i * 3), uint8_t(i * 7), 128));
        r2d.fillRect(float(i % W), float((i * 7) % H), 12.0f, 12.0f);
    }
    const double msRects = msSince(tRects);

    const auto tText = Clock::now();
    r2d.setColor(sr::colors::white);
    for (int i = 0; i < 200; ++i)
        r2d.text(2.0f, float(i * 5), "STRESS-0123456789");
    const double msText = msSince(tText);

    const auto tBlitSmall = Clock::now();
    const Texture big(sr::texgen::checkerboard(512, 16));
    sr::draw2d::blitTexture(fb, big, 0, 0, 1, 1);
    const double msBlitSmall = msSince(tBlitSmall);

    // 目标矩形 4000x4000 远大于屏幕：屏幕内只有 1M 像素，旧实现会跑 16M 次
    const auto tBlitHuge = Clock::now();
    sr::draw2d::blitTexture(fb, big, -100, -100, 4000, 4000);
    const double msBlitHuge = msSince(tBlitHuge);

    r2d.end();

    const double ms = msSince(t0);
    report("draw2d_20k_rects", true, ms,
           "rects=" + std::to_string(int(msRects)) + "ms text=" + std::to_string(int(msText)) +
               "ms blit1x1=" + std::to_string(int(msBlitSmall)) + "ms blit4000x4000=" +
               std::to_string(int(msBlitHuge)) + "ms");
}

// ---------------------------------------------------------------- 13
static void stress_resource_cache()
{
    const auto t0 = Clock::now();
    auto& cache = ResourceCache::instance();
    cache.clear();
    // 必须持续持有强引用，否则 weak_ptr 立即过期，下次 load 会重建（这本身是设计语义）
    std::mutex createdMutex;
    std::vector<std::shared_ptr<int>> strong;
    std::atomic<int> factoryCalls{0};

    const int threads = 8, perThread = 500;
    std::vector<std::thread> workers;
    for (int t = 0; t < threads; ++t)
        workers.emplace_back([&] {
            for (int i = 0; i < perThread; ++i)
            {
                auto res = cache.load<int>("hot-key", [&] {
                    factoryCalls.fetch_add(1);
                    std::this_thread::sleep_for(std::chrono::microseconds(50));
                    return std::make_shared<int>(42);
                });
                std::lock_guard<std::mutex> lock(createdMutex);
                strong.push_back(std::move(res));
            }
        });
    for (auto& w : workers)
        w.join();

    std::vector<const void*> uniq;
    uniq.reserve(strong.size());
    for (const auto& p : strong)
        uniq.push_back(p.get());
    std::sort(uniq.begin(), uniq.end());
    uniq.erase(std::unique(uniq.begin(), uniq.end()), uniq.end());
    const double ms = msSince(t0);
    report("resourcecache_8x500", uniq.size() == 1, ms,
           "factory=" + std::to_string(factoryCalls.load()) + " distinct=" + std::to_string(uniq.size()) +
               (uniq.size() > 1 ? "  (MULTIPLE INSTANCES)" : ""));
}

// ---------------------------------------------------------------- 14
static void stress_msaa()
{
    const auto t0 = Clock::now();
    const int W = 512, H = 512;
    MSAAFlatRenderer<4> r(W, H);
    r.clear(Color(0, 0, 0));
    const Mesh sphere = sr::meshgen::sphere(1.0f, 48, 32);
    Camera cam;
    cam.position = {0, 0, 4};
    cam.aspect = float(W) / float(H);
    const Mat4 vp = cam.projMatrix() * cam.viewMatrix();
    for (int t = 0; t < sphere.triangleCount(); ++t)
    {
        Vec2f s[3];
        float z[3];
        bool skip = false;
        for (int k = 0; k < 3; ++k)
        {
            const Vec3f p = sphere.vertices[sphere.indices[t * 3 + k]].position;
            const Vec4f clip = vp * Vec4f(p, 1.0f);
            if (clip.w <= 0.0f)
            {
                skip = true;
                break;
            }
            s[k] = {(clip.x / clip.w * 0.5f + 0.5f) * float(W), (clip.y / clip.w * 0.5f + 0.5f) * float(H)};
            z[k] = clip.z / clip.w * 0.5f + 0.5f;
        }
        if (skip)
            continue;
        r.rasterize(s[0], s[1], s[2], z[0], z[1], z[2], Color(200, 120, 90));
    }
    Framebuffer out(W, H);
    r.resolveTo(out);
    const double ms = msSince(t0);
    report("msaa4x_512_sphere", true, ms, "tris=" + std::to_string(sphere.triangleCount()));
}

// ---------------------------------------------------------------- 15
static void stress_obj_load()
{
    const auto t0 = Clock::now();
    std::filesystem::create_directories("stress-out");
    const std::string path = "stress-out/stress.obj";
    const auto tWrite = Clock::now();
    {
        const int faces = 50000;
        // 先拼到内存再一次写出：逐行 `ofstream <<` 200k 行要 430 ms，
        // 会把「测试自己造数据」的开销算到库头上，且拖慢 --quick。
        std::string content;
        content.reserve(size_t(faces) * 26 * 3 + size_t(faces) * 24);
        char line[96];
        for (int i = 0; i < faces * 3; ++i)
        {
            const int n = std::snprintf(line, sizeof(line), "v %d %d %d\n", i % 97, i % 53, i % 31);
            content.append(line, size_t(n));
        }
        for (int i = 0; i < faces; ++i)
        {
            const int n = std::snprintf(line, sizeof(line), "f %d %d %d\n", i * 3 + 1, i * 3 + 2, i * 3 + 3);
            content.append(line, size_t(n));
        }
        writeFileText(path, content);
    }
    const double writeMs = msSince(tWrite);
    Mesh mesh;
    const auto tParse = Clock::now();
    const bool ok = sr::io::loadObjMesh(path, mesh);
    const double parseMs = msSince(tParse);
    const double ms = msSince(t0);
    // 把「测试自己写文件」与「库解析」分开报：前者不是库的成本。
    report("obj_50k_faces", ok && mesh.triangleCount() == 50000, ms,
           "write=" + std::to_string(int(writeMs)) + "ms parse=" + std::to_string(int(parseMs)) + "ms verts=" +
               std::to_string(mesh.vertices.size()) + " tris=" + std::to_string(mesh.triangleCount()));
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// ---------------------------------------------------------------- 16
static void stress_texture_odd_sizes()
{
    const int sizes[][2] = {{1, 7}, {7, 1}, {3, 3}, {5, 3}, {17, 13}, {64, 1}};
    int bad = 0;
    std::string detail;
    const auto t0 = Clock::now();
    for (const auto& s : sizes)
    {
        sr::Image img;
        img.width = s[0];
        img.height = s[1];
        img.pixels.assign(size_t(s[0]) * size_t(s[1]), Color(120, 80, 40));
        const Texture tex(img);
        detail += std::to_string(s[0]) + "x" + std::to_string(s[1]) + "->L" +
                  std::to_string(tex.mipLevels()) + " ";
        for (int level = 0; level < tex.mipLevels(); ++level)
            if (!allFinite(tex.sampleLevel(0.5f, 0.5f, level)))
                ++bad;
        if (!allFinite(tex.sampleTrilinear(0.5f, 0.5f, 8.0f, 8.0f, 8.0f, 8.0f)))
            ++bad;
    }
    report("texture_odd_sizes", bad == 0, msSince(t0), detail + "bad=" + std::to_string(bad));
}

// ---------------------------------------------------------------- 17
static void stress_cubemap_edges()
{
    Cubemap cube;
    generateSkyCubemap(cube, 32);
    int bad = 0;
    const Vec3f dirs[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
                          {0, 0, 0}, {1e-30f, 0, 0}, {1e30f, 1e30f, 1e30f},
                          {std::numeric_limits<float>::quiet_NaN(), 1, 0}};
    const auto t0 = Clock::now();
    for (const Vec3f& d : dirs)
    {
        if (!allFinite(cube.sample(d)))
            ++bad;
        if (!allFinite(cube.sampleReflection(d, 0.5f)))
            ++bad;
    }
    if (!allFinite(cube.sampleIrradiance(Vec3f{0, 0, 0}, 8)))
        ++bad;
    if (!allFinite(cube.sampleIrradiance(Vec3f{1, 1, 1}, 0)))
        ++bad;
    if (!allFinite(cube.sampleReflection(Vec3f{1, 0, 0}, std::numeric_limits<float>::quiet_NaN())))
        ++bad;
    if (!allFinite(cube.sampleReflection(Vec3f{1, 0, 0}, 1e30f)))
        ++bad;
    report("cubemap_edge_dirs", bad == 0, msSince(t0), "bad=" + std::to_string(bad));
}

// ---------------------------------------------------------------- 18
static void stress_camera_extremes()
{
    const auto t0 = Clock::now();
    int nonFinite = 0;
    const float fovs[] = {1e-4f, 0.01f, 1.5f, 3.0f};
    const float nears[] = {1e-6f, 0.1f};
    const float fars[] = {1e-5f, 1.0f, 1e9f};
    for (float fov : fovs)
        for (float nz : nears)
            for (float fz : fars)
            {
                Camera cam;
                cam.fovY = fov;
                cam.nearZ = nz;
                cam.farZ = fz;
                const Mat4 p = cam.projMatrix();
                for (int r = 0; r < 4; ++r)
                    for (int c = 0; c < 4; ++c)
                        if (!std::isfinite(p.m[r][c]))
                            ++nonFinite;
                const Mat4 inv = p.inversed();
                for (int r = 0; r < 4; ++r)
                    for (int c = 0; c < 4; ++c)
                        if (!std::isfinite(inv.m[r][c]))
                            ++nonFinite;
            }
    report("camera_extreme_proj", nonFinite == 0, msSince(t0), "nonfinite=" + std::to_string(nonFinite));
}

// ---------------------------------------------------------------- 19
static void stress_postprocess_cost()
{
    std::string detail;
    const auto t0 = Clock::now();
    for (int radius : {4, 16, 64})
    {
        FloatFramebuffer src(256, 256);
        FloatFramebuffer dst(256, 256);
        src.clear();
        const auto ts = Clock::now();
        sr::postprocess::gaussianBlur(src, dst, true, radius);
        detail += "blur r" + std::to_string(radius) + "=" + std::to_string(int(msSince(ts))) + "ms ";
    }
    for (int maxBlur : {4, 16, 48})
    {
        FloatFramebuffer src(128, 128);
        FloatFramebuffer out(128, 128);
        src.clear();
        std::vector<float> depth(size_t(128) * 128, 0.5f);
        const auto ts = Clock::now();
        sr::postprocess::depthOfField(src, depth, 128, 128, 0.0f, 1.0f, float(maxBlur), out);
        detail += "dof r" + std::to_string(maxBlur) + "=" + std::to_string(int(msSince(ts))) + "ms ";
    }
    report("postprocess_cost_scaling", true, msSince(t0), detail);
}

// ---------------------------------------------------------------- 20
static void stress_texture_creation()
{
    std::string detail;
    const auto t0 = Clock::now();
    for (int size : {512, 2048})
    {
        // 把「程序化生成」与「mip 链构建」拆开计时：两者都在这条路径上，
        // 混在一起会让人以为瓶颈在 mip 生成。各取 3 次最小值。
        double bestGen = 1e300, bestCopy = 1e300, bestMip = 1e300;
        for (int rep = 0; rep < 3; ++rep)
        {
            const auto tg = Clock::now();
            const sr::Image img = sr::texgen::checkerboard(size, 8);
            bestGen = std::min(bestGen, msSince(tg));

            const auto tc = Clock::now();
            sr::Image copy = img;
            bestCopy = std::min(bestCopy, msSince(tc));

            // 用 move 传入：排除“按值传参拷贝”的干扰，只计 mip 链构建
            const auto tm = Clock::now();
            const Texture tex(std::move(copy));
            (void)tex.sampleBilinear(0.3f, 0.6f);
            bestMip = std::min(bestMip, msSince(tm));
        }
        detail += std::to_string(size) + ":gen=" + std::to_string(int(bestGen)) + "ms,copy=" +
                  std::to_string(int(bestCopy)) + "ms,mip=" + std::to_string(int(bestMip)) + "ms ";
    }
    report("texture_create_mipmaps", true, msSince(t0), detail + "(best of 3)");
}

// ---------------------------------------------------------------- 21
static void stress_renderer_tiny_target()
{
    // Renderer 与 Framebuffer 尺寸不一致：必须以较小者为准
    Framebuffer fb(1, 1);
    Renderer r(512, 512);
    auto shader = makePbr(512, 512);
    const Mesh sphere = sr::meshgen::sphere(1.0f, 32, 16);
    shader.model = Mat4::identity();
    shader.mvp = shader.proj * shader.view;
    shader.normalMatrix = Mat4::identity();
    const auto t0 = Clock::now();
    r.clear(fb, Color(0, 0, 0));
    r.drawMesh(fb, sphere, shader);
    report("renderer_512_on_1x1_fb", true, msSince(t0), "px=" + std::to_string(fb.getPixel(0, 0).r));
}

// ---------------------------------------------------------------- 22
static void stress_deep_transform_stack()
{
    Framebuffer fb(64, 64, Color(0, 0, 0));
    Renderer2D r2d;
    r2d.begin(fb);
    const auto t0 = Clock::now();
    for (int i = 0; i < 5000; ++i)
    {
        r2d.push();
        r2d.translate(0.001f, 0.001f);
    }
    r2d.setColor(sr::colors::red);
    r2d.fillRect(0.0f, 0.0f, 4.0f, 4.0f);
    for (int i = 0; i < 5000; ++i)
        r2d.pop();
    r2d.end();
    report("renderer2d_deep_stack", true, msSince(t0), "depth=5000");
}

// ---------------------------------------------------------------- 23
static void stress_msaa8()
{
    // 模板只支持 1x / 4x 采样（msaa_renderer.h 有 static_assert），
    // 这里改为 4x 大尺寸负载。
    const int W = 1024, H = 1024;
    MSAAFlatRenderer<4> r(W, H);
    r.clear(Color(0, 0, 0));
    const Mesh sphere = sr::meshgen::sphere(1.0f, 32, 16);
    Camera cam;
    cam.position = {0, 0, 4};
    cam.aspect = float(W) / float(H);
    const Mat4 vp = cam.projMatrix() * cam.viewMatrix();
    const auto t0 = Clock::now();
    for (int t = 0; t < sphere.triangleCount(); ++t)
    {
        Vec2f s[3];
        float z[3];
        bool skip = false;
        for (int k = 0; k < 3; ++k)
        {
            const Vec3f p = sphere.vertices[sphere.indices[t * 3 + k]].position;
            const Vec4f clip = vp * Vec4f(p, 1.0f);
            if (clip.w <= 0.0f)
            {
                skip = true;
                break;
            }
            s[k] = {(clip.x / clip.w * 0.5f + 0.5f) * float(W), (clip.y / clip.w * 0.5f + 0.5f) * float(H)};
            z[k] = clip.z / clip.w * 0.5f + 0.5f;
        }
        if (!skip)
            r.rasterize(s[0], s[1], s[2], z[0], z[1], z[2], Color(200, 120, 90));
    }
    Framebuffer out(W, H);
    r.resolveTo(out);
    report("msaa4x_1024_sphere", true, msSince(t0), "tris=" + std::to_string(sphere.triangleCount()));
}

// ---------------------------------------------------------------- 24
static void stress_nested_submit()
{
    ThreadPool pool(4);
    std::atomic<int> done{0};
    const auto t0 = Clock::now();
    for (int i = 0; i < 200; ++i)
    {
        pool.submit([&pool, &done] {
            // 工作线程再提交（不等待，避免自等待）
            pool.submit([&done] { done.fetch_add(1); });
        });
    }
    pool.waitAll();
    int spins = 0;
    while (done.load() < 200 && spins++ < 2000)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    report("threadpool_nested_submit", done.load() == 200, msSince(t0), "done=" + std::to_string(done.load()));
}

// ---------------------------------------------------------------- 25
static void stress_profiler_bad_path()
{
    const auto t0 = Clock::now();
    ProfilerEnabledGuard profilerEnabled;
    auto& p = Profiler::instance();
    p.clear();
    p.beginScope("x");
    p.endScope();
    p.flush("Z:/definitely_missing_dir/trace.json");  // 不可写路径：只允许静默失败
    const bool ok = p.eventCount() == 1;
    p.clear();
    report("profiler_unwritable_path", ok, msSince(t0), "no crash");
}

// ---------------------------------------------------------------- 26
static void stress_gltf_corrupt()
{
    std::filesystem::create_directories("stress-out");
    struct Case
    {
        const char* name;
        std::string body;
        bool binary;
    };
    const std::string hugeCount =
        R"({"asset":{"version":"2.0"},"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],)"
        R"("accessors":[{"bufferView":0,"componentType":5126,"count":999999999,"type":"VEC3"}],)"
        R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":12}],)"
        R"("buffers":[{"uri":"data:application/octet-stream;base64,AAAA","byteLength":12}]})";
    const std::vector<Case> cases = {
        {"empty_object", "{}", false},
        {"truncated", "{\"asset\":", false},
        {"huge_count", hugeCount, false},
        {"not_json", "PNG\x89\x00garbage", false},
        {"bad_glb", std::string("glTF\x02\x00\x00\x00\xff\xff\xff\xff", 12), true},
    };

    int accepted = 0;
    const auto t0 = Clock::now();
    std::string detail;
    for (const Case& c : cases)
    {
        const std::string path = std::string("stress-out/") + c.name + (c.binary ? ".glb" : ".gltf");
        {
            std::ofstream out(path, std::ios::binary);
            out.write(c.body.data(), std::streamsize(c.body.size()));
        }
        const auto result = sr::io::loadGltfScene(path);
        if (result.success)
        {
            ++accepted;
            detail += std::string(c.name) + "=ACCEPTED ";
        }
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    report("gltf_corrupt_files", accepted == 0, msSince(t0), detail.empty() ? "all rejected" : detail);
}

// ---------------------------------------------------------------- 27
static void stress_obj_hostile()
{
    std::filesystem::create_directories("stress-out");
    struct Case
    {
        const char* name;
        const char* body;
    };
    const std::vector<Case> cases = {
        {"no_vertices", "f 0 0 0\n"},
        {"zero_index", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 0 0 0\n"},
        {"relative", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf -3 -2 -1\n"},
        {"nan_vertex", "v nan inf 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"},
        {"huge_index", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 99999999\n"},
        {"bad_face", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2\n"},
    };
    int acceptedBad = 0;
    bool nonFiniteAccepted = false;
    const auto t0 = Clock::now();
    std::string detail;
    for (const Case& c : cases)
    {
        const std::string path = std::string("stress-out/") + c.name + ".obj";
        {
            std::ofstream out(path);
            out << c.body;
        }
        Mesh mesh;
        const bool ok = sr::io::loadObjMesh(path, mesh);
        detail += std::string(c.name) + (ok ? "=ok " : "=rej ");
        if (ok)
        {
            if (std::string(c.name) == "no_vertices" || std::string(c.name) == "zero_index" ||
                std::string(c.name) == "huge_index" || std::string(c.name) == "bad_face")
                ++acceptedBad;
            for (const Vertex& v : mesh.vertices)
                if (!allFinite(v.position))
                    nonFiniteAccepted = true;
        }
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    report("obj_hostile_inputs", acceptedBad == 0 && !nonFiniteAccepted, msSince(t0),
           detail + (nonFiniteAccepted ? " (NONFINITE_VERTEX_ACCEPTED)" : ""));
}

// ---------------------------------------------------------------- 28
static void stress_image_corrupt()
{
    std::filesystem::create_directories("stress-out");
    const auto t0 = Clock::now();
    int accepted = 0;

    // 随机字节冒充 PNG
    {
        const std::string path = "stress-out/corrupt.png";
        std::ofstream out(path, std::ios::binary);
        const unsigned char png[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 1, 2, 3, 4, 5, 6, 7, 8};
        out.write(reinterpret_cast<const char*>(png), sizeof(png));
        out.close();
        sr::Image img;
        if (sr::io::loadImage(path, img))
            ++accepted;
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    // 只有 18 字节头的 TGA（声明 64x64 却无像素数据）
    {
        const std::string path = "stress-out/truncated.tga";
        unsigned char header[18] = {};
        header[2] = 2;
        header[12] = 64;
        header[14] = 64;
        header[16] = 24;
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(header), sizeof(header));
        out.close();
        sr::Image img;
        if (sr::io::readTga(path, img))
            ++accepted;
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    // RLE 包声称 128 个像素但数据缺失
    {
        const std::string path = "stress-out/rle_overflow.tga";
        unsigned char header[18] = {};
        header[2] = 10;
        header[12] = 64;
        header[14] = 64;
        header[16] = 24;
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(header), sizeof(header));
        const unsigned char packet = 0x80 | 0x7F; // RLE，128 像素
        out.write(reinterpret_cast<const char*>(&packet), 1);
        out.close();
        sr::Image img;
        if (sr::io::readTga(path, img))
            ++accepted;
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    report("image_corrupt_inputs", accepted == 0, msSince(t0), "accepted=" + std::to_string(accepted));
}

// ---------------------------------------------------------------- 29
static void stress_dof_1080p()
{
    // 真实分辨率 + 多档半径：验证按半径分桶后不再随 r² 爆炸
    const int W = 1920, H = 1080;
    FloatFramebuffer color(W, H);
    color.clear();
    std::vector<float> depth(size_t(W) * size_t(H));
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            depth[size_t(y) * size_t(W) + size_t(x)] = 0.01f * float((x * 7 + y * 13) % 100);
    FloatFramebuffer out(W, H);

    const auto t0 = Clock::now();
    sr::postprocess::depthOfField(color, depth, W, H, 0.5f, 0.8f, 48.0f, out);
    report("dof_1080p_r48", true, msSince(t0), std::to_string(W) + "x" + std::to_string(H));
}

// ---------------------------------------------------------------- 30
static void stress_profiler_overflow()
{
    ProfilerEnabledGuard profilerEnabled;
    auto& p = Profiler::instance();
    p.clear();
    p.setMaxEvents(1000);
    const auto t0 = Clock::now();
    for (int i = 0; i < 5000; ++i)
    {
        p.beginScope("x");
        p.endScope();
    }
    const size_t kept = p.eventCount();
    const size_t dropped = p.droppedEvents();
    const double ms = msSince(t0);
    p.setMaxEvents(1000000);
    p.clear();
    report("profiler_event_cap", kept == 1000 && dropped == 4000, ms,
           "kept=" + std::to_string(kept) + " dropped=" + std::to_string(dropped));
}

// ---------------------------------------------------------------- 31
static void stress_draw2d_states()
{
    Framebuffer fb(128, 128, Color(0, 0, 0));
    Renderer2D r2d;
    r2d.begin(fb);
    const auto t0 = Clock::now();

    // 反向 scissor（x0 > x1）：不得崩溃，也不得画出任何东西
    r2d.setScissor(Recti(100, 100, -50, -50));
    r2d.setColor(sr::colors::red);
    r2d.fillRect(0.0f, 0.0f, 128.0f, 128.0f);
    int painted = 0;
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x)
            if (fb.getPixel(x, y).r != 0)
                ++painted;

    // 超出屏幕的 scissor + 超小矩形
    r2d.clearScissor();
    r2d.setScissor(Recti(-1000, -1000, 5000, 5000));
    r2d.fillRect(10.0f, 10.0f, 1.0f, 1.0f);
    r2d.clearScissor();
    r2d.end();

    report("draw2d_scissor_edges", painted == 0, msSince(t0), "painted=" + std::to_string(painted));
}

// ---------------------------------------------------------------- 32
static void stress_math_singular()
{
    const auto t0 = Clock::now();
    int nonFinite = 0;
    const Mat4 zero{};
    const Mat4 zInv = zero.inversed();
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            if (!std::isfinite(zInv.m[r][c]))
                ++nonFinite;

    // 退化缩放矩阵（某轴为 0）
    const Mat4 flat = Mat4::scale(1.0f, 0.0f, 1.0f);
    const Mat4 fInv = flat.inversed();
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c)
            if (!std::isfinite(fInv.m[r][c]))
                ++nonFinite;

    report("math_singular_inverse", nonFinite == 0, msSince(t0),
           "nonfinite=" + std::to_string(nonFinite));
}

// ---------------------------------------------------------------- 33
static void stress_msaa_resolve_mismatch()
{
    MSAAFlatRenderer<4> r(64, 64);
    r.clear(Color(0, 0, 0));
    r.rasterize(Vec2f{8, 8}, Vec2f{56, 8}, Vec2f{32, 56}, 0.5f, 0.5f, 0.5f, Color(255, 0, 0));

    // 目标帧缓冲尺寸与渲染器不一致：不得越界写
    Framebuffer smallFb(16, 16, Color(0, 0, 0));
    Framebuffer bigFb(128, 128, Color(0, 0, 0));
    const auto t0 = Clock::now();
    r.resolveTo(smallFb);
    r.resolveTo(bigFb);
    report("msaa_resolve_size_mismatch", true, msSince(t0),
           "small=" + std::to_string(smallFb.getPixel(8, 8).r) + " big=" + std::to_string(bigFb.getPixel(32, 32).r));
}

// ---------------------------------------------------------------- 34
static void stress_thread_pool_many_threads()
{
    const auto t0 = Clock::now();
    ThreadPool pool(64);
    std::atomic<int> n{0};
    for (int i = 0; i < 50000; ++i)
        pool.submit([&n] { n.fetch_add(1, std::memory_order_relaxed); });
    pool.waitAll();
    report("threadpool_64_threads", n.load() == 50000, msSince(t0),
           "threads=" + std::to_string(pool.threadCount()) + " done=" + std::to_string(n.load()));
}

// ---------------------------------------------------------------- 35
static void stress_gltf_empty_model()
{
    // 合法但无网格的 glTF：必须报「无可用网格」而不是成功或崩溃
    std::filesystem::create_directories("stress-out");
    const std::string path = "stress-out/empty_model.gltf";
    {
        std::ofstream out(path);
        out << R"({"asset":{"version":"2.0"},"scenes":[{"nodes":[]}]})";
    }
    const auto t0 = Clock::now();
    const auto result = sr::io::loadGltfScene(path);
    const bool ok = !result.success && !result.error.empty();
    std::error_code ec;
    std::filesystem::remove(path, ec);
    report("gltf_empty_model", ok, msSince(t0), "error=" + result.error);
}

// ---------------------------------------------------------------- 36
static void stress_soak()
{
    // 长时间运行：渲染 + 粒子 + 临时纹理，观察私有内存是否持续上涨（泄漏/缓存无上限）。
    const int W = 320, H = 240;
    Framebuffer fb(W, H, Color(8, 10, 14));
    Renderer renderer(W, H);
    const Mesh sphere = sr::meshgen::sphere(1.0f, 24, 16);
    auto shader = makePbr(W, H);

    EmitterConfig cfg;
    cfg.maxParticles = 2000;
    cfg.rate = 500.0f;
    cfg.lifeMin = 0.4f;
    cfg.lifeMax = 0.9f;
    ParticleSystem ps(cfg);

    Profiler::instance().setEnabled(false); // soak 不测 Profiler 增长（已有上限用例）
    const auto t0 = Clock::now();
    size_t baseline = 0;
    const int frames = 1200;
    for (int i = 0; i < frames; ++i)
    {
        renderer.clear(fb, Color(8, 10, 14));
        shader.model = Mat4::rotateY(0.02f * float(i));
        shader.mvp = shader.proj * shader.view * shader.model;
        renderer.drawMesh(fb, sphere, shader);
        ps.update(1.0f / 60.0f);

        // 每 100 帧创建一个临时纹理（含 mip 链）并释放
        if (i % 100 == 0)
        {
            const Texture tmp(sr::texgen::checkerboard(128, 8));
            (void)tmp.sampleBilinear(0.2f, 0.7f);
        }
        if (i == 200)
            baseline = processPrivateBytes();
    }
    const size_t end = processPrivateBytes();
    Profiler::instance().setEnabled(true);

    const long long growth = (baseline != 0 && end != 0) ? (long long)end - (long long)baseline : 0;
    // 允许 4MB 波动（分配器保留、STL 碎片）
    const bool ok = baseline == 0 || growth < 4LL * 1024 * 1024;
    report("soak_1200_frames", ok, msSince(t0),
           baseline == 0 ? "memory api unavailable"
                         : "growth=" + std::to_string(growth / 1024) + "KB over " + std::to_string(frames - 200) +
                               " frames");
}

// ---------------------------------------------------------------- 37
namespace
{
// 固定颜色、不过光照的着色器，用来把两条光栅路径的覆盖集合拉到同一基准
struct SolidShader : Shader
{
    Mat4 mvp = Mat4::identity();
    Vec4f vertex(const Vertex& in, Varyings& out) override
    {
        out.worldPos = in.position;
        out.normal = Vec3f{0, 0, 1};
        out.uv = in.uv;
        return mvp * Vec4f(in.position, 1.0f);
    }
    bool fragment(const Varyings&, Vec3f& color) override
    {
        color = Vec3f{1, 1, 1};
        return true;
    }
};
} // namespace

static void stress_render_path_consistency()
{
    const auto t0 = Clock::now();
    // 同一组三角形：主 Renderer（clip + TriangleCoverage）vs rasterizer_fast。
    // ADR-003 声称两者共用同一套 top-left 规则，这里用像素集合直接验证。
    const int W = 128, H = 128;
    Framebuffer fa(W, H, Color(0, 0, 0));
    Framebuffer fb2(W, H, Color(0, 0, 0));
    DepthBuffer zb(W, H);
    Renderer renderer(W, H);
    renderer.cullBackface = false;
    renderer.clear(fa, Color(0, 0, 0));
    zb.clear();

    const Vec3f ndc[][3] = {
        {{-0.7f, -0.5f, 0.3f}, {0.6f, -0.3f, 0.6f}, {0.1f, 0.8f, 0.4f}},
        {{-0.9f, 0.9f, 0.5f}, {0.95f, 0.1f, 0.2f}, {-0.2f, -0.95f, 0.8f}},
    };

    Mesh mesh;
    SolidShader shader;
    shader.mvp = Mat4::identity(); // 顶点已经是 NDC，w=1
    for (const auto& tri : ndc)
        for (int k = 0; k < 3; ++k)
        {
            Vertex v;
            v.position = tri[k];
            v.normal = {0, 0, 1};
            mesh.vertices.push_back(v);
            mesh.indices.push_back(int(mesh.vertices.size()) - 1);
        }
    renderer.drawMesh(fa, mesh, shader);

    // 手工做同一批三角形的视口变换，交给 rasterizer_fast。
    // 必须用库自己的 viewport 矩阵，否则两边屏幕坐标的浮点舍入不一致，
    // 会把“压测换算误差”误报成“库内两路径不一致”。
    const Mat4 vp = sr::transform::viewport(W, H);
    auto toScreen = [&](const Vec3f& n) { return vp.transformPoint(n); };
    for (const auto& tri : ndc)
    {
        const Vec3f s0 = toScreen(tri[0]);
        const Vec3f s1 = toScreen(tri[1]);
        const Vec3f s2 = toScreen(tri[2]);
        sr::raster::fillTriangleIncremental(fb2, zb, {s0.x, s0.y}, {s1.x, s1.y}, {s2.x, s2.y}, s0.z, s1.z, s2.z,
                                            Color(255, 255, 255));
    }

    int diff = 0;
    int coveredA = 0, coveredB = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            const bool a = fa.getPixel(x, y).r != 0;
            const bool b = fb2.getPixel(x, y).r != 0;
            coveredA += a ? 1 : 0;
            coveredB += b ? 1 : 0;
            if (a != b)
                ++diff;
        }
    report("render_path_consistency", diff == 0, msSince(t0),
           "A=" + std::to_string(coveredA) + " B=" + std::to_string(coveredB) + " diff=" + std::to_string(diff) +
               (diff == 0 ? " (identical coverage)" : " (COVERAGE MISMATCH)"));
}

// ---------------------------------------------------------------- 38
static void stress_renderer2d_rotated_texture()
{
    // 旋转态下的贴图：旧实现 `if (!axisAligned()) return;` 直接不画，属静默丢弃
    Framebuffer fb(64, 64, Color(0, 0, 0));
    sr::Image img;
    img.width = 4;
    img.height = 4;
    img.pixels.assign(16, Color(255, 0, 0, 255));
    const Texture tex(img);

    Renderer2D r2d;
    r2d.begin(fb);
    const auto t0 = Clock::now();
    r2d.translate(32.0f, 32.0f);
    r2d.rotate(0.6f);
    r2d.setColor(sr::colors::white);
    r2d.texture(tex, -20.0f, -20.0f, 40.0f, 40.0f);
    r2d.end();

    int lit = 0;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            if (fb.getPixel(x, y).r > 0)
                ++lit;
    // 40×40 的四边形旋转后面积不变，至少应覆盖一半
    report("renderer2d_rotated_texture", lit > 800, msSince(t0), "lit=" + std::to_string(lit));
}

// ---------------------------------------------------------------- 39
static void stress_command_buffer()
{
    const auto t0 = Clock::now();
    CommandBuffer cb;
    std::atomic<int> counter{0};
    const int n = 50000;
    for (int i = 0; i < n; ++i)
        cb.record([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });

    cb.executeParallel(8);
    const int parallel = counter.load();

    counter = 0;
    cb.executeSequential();
    const int sequential = counter.load();

    // 重复执行应可重入（命令没有被消费掉）
    counter = 0;
    cb.executeParallel(4);
    const int again = counter.load();

    // 异常命令：顺序路径必须传播，且不得吞掉
    bool threw = false;
    CommandBuffer bad;
    bad.record([] { throw std::runtime_error("boom"); });
    try
    {
        bad.executeSequential();
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }

    const bool ok = parallel == n && sequential == n && again == n && threw;
    report("command_buffer_50k", ok, msSince(t0),
           "par=" + std::to_string(parallel) + " seq=" + std::to_string(sequential) + " again=" +
               std::to_string(again) + " throw=" + std::to_string(threw));
}

// ---------------------------------------------------------------- 40
static void stress_animation()
{
    const auto t0 = Clock::now();
    // 大片段：10000 个线性关键帧，采样 20000 次
    AnimationClip clip;
    AnimationSampler sampler;
    const int keys = 10000;
    sampler.interpolation = AnimationSampler::Interpolation::Linear;
    sampler.times.reserve(size_t(keys));
    sampler.values.reserve(size_t(keys));
    for (int i = 0; i < keys; ++i)
    {
        sampler.times.push_back(float(i) * 0.001f);
        sampler.values.push_back(Vec4f{float(i) * 0.01f, 0.0f, 0.0f, 0.0f});
    }
    clip.samplers.push_back(std::move(sampler));
    AnimationChannel ch;
    ch.samplerIndex = 0;
    ch.targetNode = 0;
    ch.path = AnimationChannel::Path::Translation;
    clip.channels.push_back(ch);
    // 模拟加载器输出：已校验过时间轴，运行时不再 O(n) 重扫
    clip.validated = true;

    std::vector<SceneNode> nodes(1);
    int nonFinite = 0;
    const float probes[] = {-1.0f, 0.0f, 0.5f, 5.0f, 1e9f, std::numeric_limits<float>::quiet_NaN(),
                            std::numeric_limits<float>::infinity()};
    for (float t : probes)
    {
        const auto poses = sr::animation::sampleAnimation(nodes, clip, t);
        for (const auto& p : poses)
            if (!std::isfinite(p.translation.x) || !std::isfinite(p.translation.y) || !std::isfinite(p.translation.z))
                ++nonFinite;
    }
    for (int i = 0; i < 20000; ++i)
    {
        const float t = float(i) * 0.0005f;
        const auto poses = sr::animation::sampleAnimation(nodes, clip, t);
        if (!poses.empty() && !std::isfinite(poses[0].translation.x))
            ++nonFinite;
    }
    report("animation_10k_keys", nonFinite == 0, msSince(t0), "nonfinite=" + std::to_string(nonFinite));
}

// ---------------------------------------------------------------- 41
static void stress_msaa_1080p()
{
    // 4 样本 × 1920×1080：内存大头（样本缓冲 ≈ 100MB 量级），验证能分配并 resolve
    const int W = 1920, H = 1080;
    const auto t0 = Clock::now();
    MSAAFlatRenderer<4> r(W, H);
    r.clear(Color(10, 10, 10));
    r.rasterize(Vec2f{0, 0}, Vec2f{float(W), 0}, Vec2f{0, float(H)}, 0.5f, 0.5f, 0.5f, Color(200, 50, 50));
    r.rasterize(Vec2f{float(W), float(H)}, Vec2f{float(W), 0}, Vec2f{0, float(H)}, 0.4f, 0.4f, 0.4f, Color(50, 200, 50));
    Framebuffer out(W, H);
    r.resolveTo(out);
    report("msaa4x_1080p", out.getPixel(W / 2, H / 2).r + out.getPixel(W - 1, 0).g > 0, msSince(t0),
           "px=" + std::to_string(out.getPixel(1, 1).r) + "/" + std::to_string(out.getPixel(W - 2, 1).g));
}

// ---------------------------------------------------------------- 42
static void stress_renderer2d_transforms()
{
    Framebuffer fb(256, 256, Color(0, 0, 0));
    sr::Image img;
    img.width = 8;
    img.height = 8;
    img.pixels.assign(64, Color(30, 200, 240, 255));
    const Texture tex(img);

    Renderer2D r2d;
    r2d.begin(fb);
    std::mt19937 rng(99);
    std::uniform_real_distribution<float> pos(0.0f, 256.0f);
    std::uniform_real_distribution<float> ang(-3.2f, 3.2f);
    std::uniform_real_distribution<float> scl(0.2f, 2.0f);

    const auto t0 = Clock::now();
    for (int i = 0; i < 2000; ++i)
    {
        r2d.push();
        r2d.translate(pos(rng), pos(rng));
        r2d.rotate(ang(rng));
        r2d.scale(scl(rng), scl(rng));
        r2d.setBlend((i % 3 == 0) ? BlendMode::Opaque : BlendMode::AlphaBlend);
        r2d.setColor(Color(uint8_t(i), uint8_t(i * 5), uint8_t(i * 11), 160));
        r2d.fillRect(-8.0f, -8.0f, 16.0f, 16.0f);
        r2d.rectGradient(-8.0f, -8.0f, 16.0f, 16.0f, Color(0, 0, 0), Color(255, 255, 255), (i & 1) != 0);
        r2d.texture(tex, -8.0f, -8.0f, 16.0f, 16.0f);
        r2d.pop();
    }
    r2d.end();

    // 全图必须被画过（非全黑）且无越界崩溃
    int lit = 0;
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x)
            if (fb.getPixel(x, y).r + fb.getPixel(x, y).g + fb.getPixel(x, y).b > 0)
                ++lit;
    report("renderer2d_transforms_2k", lit > 1000, msSince(t0), "lit=" + std::to_string(lit));
}

// ---------------------------------------------------------------- 43
static void stress_scene_asset_copy()
{
    // SceneAsset 内含 vector<Texture>，每张带完整 mip 链：拷贝代价容易被低估
    SceneAsset asset;
    for (int i = 0; i < 4; ++i)
        asset.textures.push_back(Texture(sr::texgen::checkerboard(256, 8)));
    asset.meshes.push_back(sr::meshgen::sphere(1.0f, 32, 16));
    asset.meshMaterialIndices.push_back(-1);
    asset.nodes.resize(1);
    asset.nodes[0].meshIndices = {0};
    asset.nodes[0].meshIndex = 0;

    const auto t0 = Clock::now();
    size_t checksum = 0;
    for (int i = 0; i < 40; ++i)
    {
        SceneAsset copy = asset; // 深拷贝
        checksum += copy.textures.size() + copy.meshes[0].triangleCount();
    }
    report("sceneasset_deep_copy", checksum > 0, msSince(t0), "40 copies of 4×(256² tex + mip)");
}

// ---------------------------------------------------------------- 44
static void stress_render_4k()
{
    // 4K 全屏：帧缓冲 33MB + 深度 33MB，验证大目标下的时间与内存
    const int W = 3840, H = 2160;
    const auto t0 = Clock::now();
    Framebuffer fb(W, H, Color(8, 10, 14));
    Renderer renderer(W, H);
    const Mesh sphere = sr::meshgen::sphere(1.0f, 64, 48);
    auto shader = makePbr(W, H);
    renderer.clear(fb, Color(8, 10, 14));
    shader.model = Mat4::rotateY(0.4f);
    shader.mvp = shader.proj * shader.view * shader.model;
    shader.normalMatrix = shader.model.inversed().transposed();
    renderer.drawMesh(fb, sphere, shader);
    const double ms = msSince(t0);
    report("render_4k_sphere", fb.getPixel(W / 2, H / 2).r > 0, ms,
           std::to_string(W) + "x" + std::to_string(H) + " tris=" + std::to_string(sphere.triangleCount()));
}

// ---------------------------------------------------------------- 45
static void stress_text_many()
{
    // 大量不同字符串：考察字形光栅与状态缓存是否随文本量增长
    Framebuffer fb(512, 512, Color(0, 0, 0));
    Renderer2D r2d;
    r2d.begin(fb);
    r2d.setColor(sr::colors::white);
    const auto t0 = Clock::now();
    char buf[64];
    for (int i = 0; i < 5000; ++i)
    {
        std::snprintf(buf, sizeof(buf), "line-%05d-ABCxyz", i);
        r2d.text(4.0f, float((i * 7) % 500), buf);
    }
    r2d.end();
    int lit = 0;
    for (int y = 0; y < 512; ++y)
        for (int x = 0; x < 512; ++x)
            if (fb.getPixel(x, y).r > 0)
                ++lit;
    report("renderer2d_text_5k", lit > 1000, msSince(t0), "lit=" + std::to_string(lit));
}

// ---------------------------------------------------------------- 46
static void stress_tga_roundtrip()
{
    std::filesystem::create_directories("stress-out");
    const int W = 1920, H = 1080;
    Framebuffer fb(W, H, Color(10, 20, 30));
    for (int y = 0; y < H; y += 3)
        for (int x = 0; x < W; x += 3)
            fb.setPixel(x, y, Color(uint8_t(x), uint8_t(y), 120));

    const std::string path = "stress-out/rt.tga";
    const auto t0 = Clock::now();
    const bool wrote = sr::io::writeTga(path, fb);
    sr::Image back;
    const bool read = sr::io::readTga(path, back);
    const double ms = msSince(t0);
    const bool ok = wrote && read && back.width == W && back.height == H && back.at(W - 1, H - 1).a == 255;
    std::error_code ec;
    std::filesystem::remove(path, ec);
    report("tga_1920x1080_roundtrip", ok, ms, "bytes=" + std::to_string(size_t(W) * H * 3));
}

// ---------------------------------------------------------------- 47
static void stress_deferred_1080p()
{
    // 延迟渲染路径：G-Buffer 写入（含 NaN）+ 全屏光照
    const int W = 1920, H = 1080;
    GBuffer g;
    g.resize(W, H);
    g.clear();
    std::mt19937 rng(5);
    std::uniform_real_distribution<float> u01(0.0f, 1.0f);
    const auto t0 = Clock::now();

    int accepted = 0;
    const size_t tries = size_t(W) * size_t(H) / 3;
    for (size_t i = 0; i < tries; ++i)
    {
        const int x = int(rng() % unsigned(W));
        const int y = int(rng() % unsigned(H));
        // 每 1000 次掺一个 NaN 深度：必须被拒绝，否则深度测试整体失效
        const float z = (i % 1000 == 0) ? std::numeric_limits<float>::quiet_NaN() : u01(rng);
        if (g.write(x, y, z, Vec3f{u01(rng), u01(rng), u01(rng)}, Vec3f{0, 0, 1}, 0.5f,
                    Vec3f{0.8f, 0.8f, 0.8f}, 0.0f))
            ++accepted;
    }

    std::vector<Vec3f> color;
    sr::lightingPass(g, Vec3f{0.3f, 0.8f, 0.5f}.normalized(), Vec3f{1, 1, 1}, Vec3f{0, 0, 3},
                     Vec3f{0.05f, 0.05f, 0.06f}, color);

    int nonFinite = 0;
    for (const Vec3f& c : color)
        if (!allFinite(c))
            ++nonFinite;

    // NaN 写入必须被拒绝：否则 depth 会变成 NaN，后续所有写入都会“成功”
    GBuffer probe;
    probe.resize(2, 1);
    probe.clear();
    const bool nanRejected = !probe.write(0, 0, std::numeric_limits<float>::quiet_NaN(), Vec3f{0, 0, 0},
                                          Vec3f{0, 0, 1}, 0.5f, Vec3f{1, 1, 1}, 0.0f);
    const bool farAccepted = probe.write(0, 0, 0.9f, Vec3f{0, 0, 0}, Vec3f{0, 0, 1}, 0.5f, Vec3f{1, 1, 1}, 0.0f);
    const bool nearRejected = !probe.write(0, 0, 0.95f, Vec3f{0, 0, 0}, Vec3f{0, 0, 1}, 0.5f, Vec3f{1, 1, 1}, 0.0f);

    report("deferred_1080p", nonFinite == 0 && nanRejected && farAccepted && nearRejected, msSince(t0),
           "accepted=" + std::to_string(accepted) + " nonfinite=" + std::to_string(nonFinite) +
               " nanRejected=" + std::to_string(nanRejected) + " depthOrder=" + std::to_string(farAccepted && nearRejected));
}

// ---------------------------------------------------------------- 48
namespace
{
// 故意输出 NaN/Inf 的着色器：验证最后一道颜色闸门
struct NanoFragmentShader : Shader
{
    Mat4 mvp = Mat4::identity();
    Vec4f vertex(const Vertex& in, Varyings& out) override
    {
        out.worldPos = in.position;
        out.normal = Vec3f{0, 0, 1};
        out.uv = in.uv;
        return mvp * Vec4f(in.position, 1.0f);
    }
    bool fragment(const Varyings&, Vec3f& color) override
    {
        color = Vec3f{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                      -std::numeric_limits<float>::infinity()};
        return true;
    }
};
} // namespace

static void stress_nan_fragment_color()
{
    const int W = 64, H = 64;
    Framebuffer fb(W, H, Color(7, 7, 7));
    Renderer r(W, H);
    r.clear(fb, Color(7, 7, 7));
    r.cullBackface = false;

    // 两个屏幕大小的三角形（直接给 NDC，mvp = 单位矩阵）
    Mesh mesh;
    const Vec3f tris[2][3] = {{{-1, -1, 0.5f}, {1, -1, 0.5f}, {1, 1, 0.5f}},
                              {{-1, -1, 0.5f}, {1, 1, 0.5f}, {-1, 1, 0.5f}}};
    for (const auto& tri : tris)
        for (int k = 0; k < 3; ++k)
        {
            Vertex v;
            v.position = tri[k];
            mesh.vertices.push_back(v);
            mesh.indices.push_back(int(mesh.vertices.size()) - 1);
        }

    NanoFragmentShader shader;
    const auto t0 = Clock::now();
    r.drawMesh(fb, mesh, shader);

    // NaN→0、+Inf→255、−Inf→0：像素值必须正好是 (0,255,0)
    const Color c = fb.getPixel(W / 2, H / 2);
    const bool ok = c.r == 0 && c.g == 255 && c.b == 0;
    report("nan_fragment_color", ok, msSince(t0),
           "rgba=" + std::to_string(c.r) + "," + std::to_string(c.g) + "," + std::to_string(c.b) + "," +
               std::to_string(c.a));
}

// ---------------------------------------------------------------- 49
static void stress_nonfinite_2d_api()
{
    // 公共 2D/后处理 API 直接接到 NaN/Inf 参数：不得崩溃，也不得因浮转整 UB 画出垃圾
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    Framebuffer fb(64, 64, Color(0, 0, 0));

    const auto t0 = Clock::now();
    for (float v : {nan, inf, -inf})
    {
        sr::draw2d::fillCircle(fb, v, 32.0f, 20.0f, sr::colors::white);
        sr::draw2d::fillCircle(fb, 32.0f, v, 20.0f, sr::colors::white);
        sr::draw2d::fillCircle(fb, 32.0f, 32.0f, v, sr::colors::white);
        sr::draw2d::fillEllipse(fb, v, 32.0f, 10.0f, 8.0f, sr::colors::white);
        sr::draw2d::drawEllipse(fb, 32.0f, 32.0f, 1e30f, v, sr::colors::white);
        sr::draw2d::fillRoundedRect(fb, v, 8.0f, 20.0f, 20.0f, 4.0f, sr::colors::white);
        sr::draw2d::drawRoundedRect(fb, 8.0f, 8.0f, 20.0f, v, 4.0f, sr::colors::white);
    }

    FloatFramebuffer hdr(32, 32);
    hdr.clear();
    FloatFramebuffer out(32, 32);
    std::vector<float> depth(size_t(32) * 32, 0.5f);
    for (float v : {nan, inf, -inf})
    {
        sr::postprocess::chromaticAberration(hdr, 32, 32, v, out);
        sr::postprocess::radialMotionBlur(hdr, 32, 32, v, 4, out);
        sr::postprocess::depthOfField(hdr, depth, 32, 32, v, 0.5f, 3.0f, out);
    }

    // 上述调用应全部被闸门拦住：画面保持纯黑
    int painted = 0;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            if (fb.getPixel(x, y).r != 0)
                ++painted;
    report("nonfinite_2d_api", painted == 0, msSince(t0), "painted=" + std::to_string(painted));
}

// ================================================================
// 第二批：资产语义与并发安全
// ================================================================

// glTF 一个 mesh 含多个 primitive 时，每个 primitive 必须展平为独立 Mesh，
// 且节点通过 meshIndices 引到全部 primitive；材质索引逐个对应。
// 这类错误不会崩，只会让同 mesh 的第 2 个及以后 primitive 静默消失。
static void stress_gltf_multi_primitive()
{
    const auto t0 = Clock::now();
    const std::string dir = "stress-out";
    std::filesystem::create_directories(dir);

    // 9 个顶点 = 3 个三角形，坐标互不相等：映射错位时能区分出「都指向第一个」。
    const std::vector<float> positions = {
        0.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
        1.0f,  1.0f, 0.0f, 2.0f,  1.0f, 0.0f, 1.0f, 2.0f, 0.0f,
        -1.0f, 0.5f, 0.0f, -2.0f, 0.5f, 0.0f, -1.0f, 1.5f, 0.0f,
    };
    const bool binOk = writeFileBytes(dir + "/multi_prim.bin", positions.data(), positions.size() * sizeof(float));

    const std::string json = R"({
  "asset": {"version": "2.0"},
  "buffers": [{"uri": "multi_prim.bin", "byteLength": 108}],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 36},
    {"buffer": 0, "byteOffset": 36, "byteLength": 36},
    {"buffer": 0, "byteOffset": 72, "byteLength": 36}
  ],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]},
    {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3", "min": [1, 1, 0], "max": [2, 2, 0]},
    {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC3", "min": [-2, 0.5, 0], "max": [-1, 1.5, 0]}
  ],
  "materials": [{"name": "m0"}, {"name": "m1"}, {"name": "m2"}],
  "meshes": [
    {"name": "twoPrimitives", "primitives": [
      {"attributes": {"POSITION": 0}, "material": 0},
      {"attributes": {"POSITION": 1}, "material": 1}
    ]},
    {"name": "singlePrimitive", "primitives": [
      {"attributes": {"POSITION": 2}, "material": 2}
    ]}
  ],
  "nodes": [{"name": "n0", "mesh": 0}, {"name": "n1", "mesh": 1}],
  "scenes": [{"nodes": [0, 1]}],
  "scene": 0
})";
    const bool jsonOk = writeFileText(dir + "/multi_prim.gltf", json);

    const auto loaded = sr::io::loadGltfScene(dir + "/multi_prim.gltf");
    const SceneAsset& scene = loaded.scene;
    const bool ok = binOk && jsonOk && loaded.success && scene.meshes.size() == 3 &&
                    scene.meshMaterialIndices == std::vector<int>({0, 1, 2}) && scene.materials.size() == 3 &&
                    scene.nodes.size() == 2 && scene.nodes[0].meshIndices == std::vector<int>({0, 1}) &&
                    scene.nodes[1].meshIndices == std::vector<int>({2}) && scene.nodes[1].meshIndex == 2 &&
                    scene.meshes[0].indices.size() == 3 && scene.meshes[1].indices.size() == 3 &&
                    scene.meshes[2].indices.size() == 3 && scene.materialForMesh(1) == &scene.materials[1];
    report("gltf_multi_primitive", ok, msSince(t0),
           loaded.success ? "meshes=" + std::to_string(scene.meshes.size()) + " materials=" +
                                std::to_string(scene.materials.size()) + " node0Prims=" +
                                std::to_string(scene.nodes.empty() ? 0 : scene.nodes[0].meshIndices.size())
                          : "load failed: " + loaded.error);
}

// 非三角形 primitive（LINES 等）必须让整体加载失败：静默丢弃会让模型缺件。
static void stress_gltf_unsupported_mode()
{
    const auto t0 = Clock::now();
    const std::string dir = "stress-out";
    std::filesystem::create_directories(dir);
    const std::vector<float> positions = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    writeFileBytes(dir + "/lines.bin", positions.data(), positions.size() * sizeof(float));
    const std::string json = R"({
  "asset": {"version": "2.0"},
  "buffers": [{"uri": "lines.bin", "byteLength": 36}],
  "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 36}],
  "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0, 0, 0], "max": [1, 1, 0]}],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "mode": 1}]}],
  "nodes": [{"mesh": 0}],
  "scenes": [{"nodes": [0]}],
  "scene": 0
})";
    writeFileText(dir + "/lines.gltf", json);

    const auto loaded = sr::io::loadGltfScene(dir + "/lines.gltf");
    const bool rejected = !loaded.success && !loaded.error.empty();
    const bool named = loaded.error.find("triangle") != std::string::npos;
    report("gltf_unsupported_mode", rejected && named, msSince(t0),
           "success=" + std::to_string(loaded.success) + " error=" + loaded.error);
}

// ---------------------------------------------------------------- 52
// 骨骼层级：数组顺序无关、环/越界必须报错而非输出错误矩阵。
static void stress_skeleton_hierarchy()
{
    const auto t0 = Clock::now();
    int failures = 0;

    // 1) 逆序：子骨骼在父骨骼之前，结果应与数组顺序无关
    Skeleton reverse;
    reverse.bones.resize(3);
    reverse.bones[0].parent = 1;
    reverse.bones[1].parent = 2;
    reverse.bones[2].parent = -1;
    reverse.bones[0].localTransform = Mat4::translate(0.0f, 0.0f, 1.0f);
    reverse.bones[1].localTransform = Mat4::translate(0.0f, 1.0f, 0.0f);
    reverse.bones[2].localTransform = Mat4::translate(1.0f, 0.0f, 0.0f);
    const auto g = reverse.computeGlobalTransforms();
    if (g.size() != 3)
        ++failures;
    else
    {
        const Vec3f leaf = g[0].transformPoint({0, 0, 0});
        if (std::abs(leaf.x - 1.0f) > 1e-5f || std::abs(leaf.y - 1.0f) > 1e-5f || std::abs(leaf.z - 1.0f) > 1e-5f)
            ++failures;
    }

    auto rejects = [&](const std::vector<int>& parents) {
        Skeleton s;
        s.bones.resize(parents.size());
        for (size_t i = 0; i < parents.size(); ++i)
        {
            s.bones[i].parent = parents[i];
            s.bones[i].inverseBindMatrix = Mat4::identity();
        }
        return s.computeGlobalTransforms().empty() && s.computeSkinMatrices().empty();
    };
    // 2) 双骨骼环 / 3) 自环 / 4) 父索引越界
    if (!rejects({1, 0}))
        ++failures;
    if (!rejects({0}))
        ++failures;
    if (!rejects({99}))
        ++failures;

    // 5) 多根：三个独立根都必须被解析
    Skeleton forest;
    forest.bones.resize(3);
    for (int i = 0; i < 3; ++i)
    {
        forest.bones[size_t(i)].parent = -1;
        forest.bones[size_t(i)].localTransform = Mat4::translate(float(i), 0.0f, 0.0f);
    }
    const auto fg = forest.computeGlobalTransforms();
    if (fg.size() != 3 || std::abs(fg[2].transformPoint({0, 0, 0}).x - 2.0f) > 1e-5f)
        ++failures;

    // 6) 蒙皮：越界骨骼索引被忽略、零权重退回原位置
    std::vector<Mat4> skin = {Mat4::translate(0.0f, 1.0f, 0.0f)};
    VertexBoneWeights bw;
    bw.boneIndices[0] = 5;
    bw.weights[0] = 1.0f;
    const Vec3f untouched = skinPosition({1, 2, 3}, bw, skin);
    if (std::abs(untouched.x - 1.0f) > 1e-6f || std::abs(untouched.y - 2.0f) > 1e-6f)
        ++failures;
    bw.boneIndices[0] = 0;
    bw.weights[0] = 0.5f;
    const Vec3f half = skinPosition({0, 0, 0}, bw, skin);
    if (!allFinite(half) || std::abs(half.y - 0.5f) > 1e-6f)
        ++failures;

    report("skeleton_hierarchy", failures == 0, msSince(t0), "failures=" + std::to_string(failures));
}

// ---------------------------------------------------------------- 53
// Cubemap 粗糙度：粗糙度必须真的选到更低分辨率的层级（否则 PBR 反射恒定锐利）。
static void stress_cubemap_roughness()
{
    const auto t0 = Clock::now();
    sr::Image faces[6];
    for (sr::Image& face : faces)
    {
        face.width = 64;
        face.height = 64;
        face.pixels.resize(64 * 64);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                face.pixels[size_t(y) * 64 + x] = (((x / 8) + (y / 8)) & 1) ? Color(255, 255, 255) : Color(0, 0, 0);
    }
    Cubemap cube;
    cube.setFaces(faces);

    const Vec3f axes[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    int nonfinite = 0;
    int flatFaces = 0;
    for (const Vec3f& axis : axes)
    {
        const Vec3f c = cube.sample(axis);
        if (!allFinite(c))
            ++nonfinite;
        if (c.x < 0.0f || c.x > 1.0f)
            ++flatFaces;
    }

    // 采样方向刻意对准格子内部（面内像素 (12,1)、格 (1,0)）：
    // 面中心 (0.5,0.5) 落在四像素交界处，sharp/blur 会被插值平均成同一值。
    const Vec3f probe = Vec3f{1.0f, 0.953f, 0.609f}.normalized();
    const Vec3f sharp = cube.sampleReflection(probe, 0.0f);
    const Vec3f blurred = cube.sampleReflection(probe, 1.0f);
    const float delta = std::abs(sharp.x - blurred.x) + std::abs(sharp.y - blurred.y) + std::abs(sharp.z - blurred.z);
    const Vec3f nanRoughness = cube.sampleReflection(probe, std::numeric_limits<float>::quiet_NaN());
    const Vec3f zeroDir = cube.sampleReflection({0, 0, 0}, 0.5f);

    const bool ok = cube.valid() && nonfinite == 0 && flatFaces == 0 && delta > 0.5f && allFinite(nanRoughness) &&
                    zeroDir.x == 0.0f && zeroDir.y == 0.0f && zeroDir.z == 0.0f;
    report("cubemap_roughness", ok, msSince(t0),
           "sharp=" + std::to_string(sharp.x) + " blur=" + std::to_string(blurred.x) + " delta=" +
               std::to_string(delta) + " nonfinite=" + std::to_string(nonfinite));
}

// ---------------------------------------------------------------- 54
// ResourceCache：并发首次加载同一资源只能构建一次（single-flight）。
// 构建一次模型/纹理可能要几百毫秒，16 个线程各自 miss 一次就变成 16 倍。
static void stress_resource_cache_concurrent()
{
    const auto t0 = Clock::now();
    auto& cache = ResourceCache::instance();
    cache.clear();

    std::atomic<int> factoryCalls{0};
    std::atomic<int> nulls{0};
    std::atomic<int> wrongValues{0};
    std::mutex setMutex;
    std::set<const void*> instances;

    const int threads = 16;
    std::vector<std::shared_ptr<int>> keeps(static_cast<size_t>(threads));
    std::vector<std::thread> workers;
    workers.reserve(size_t(threads));
    for (int t = 0; t < threads; ++t)
        workers.emplace_back([&, t] {
            // 所有线程同时 miss：factory 故意变慢，把窗口拉到毫秒级
            keeps[size_t(t)] = cache.load<int>("stress-shared-int", [&] {
                factoryCalls.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                return std::make_shared<int>(7);
            });
            if (!keeps[size_t(t)])
            {
                nulls.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            if (*keeps[size_t(t)] != 7)
                wrongValues.fetch_add(1, std::memory_order_relaxed);
            std::lock_guard<std::mutex> lock(setMutex);
            instances.insert(keeps[size_t(t)].get());
        });
    for (std::thread& worker : workers)
        worker.join();

    const int callsAfterFirstWave = factoryCalls.load();
    const bool singleBuild = callsAfterFirstWave == 1;
    const bool singleInstance = instances.size() == 1;
    const size_t cacheSize = cache.size();

    // 唯一活跃引用释放后，条目必须过期并允许重建
    for (auto& keep : keeps)
        keep.reset();
    cache.purge();
    auto rebuilt = cache.load<int>("stress-shared-int", [&] {
        factoryCalls.fetch_add(1, std::memory_order_relaxed);
        return std::make_shared<int>(11);
    });
    const bool rebuiltOk = rebuilt && *rebuilt == 11 && factoryCalls.load() == callsAfterFirstWave + 1;

    const bool ok = nulls.load() == 0 && wrongValues.load() == 0 && singleBuild && singleInstance &&
                    cacheSize == 1 && rebuiltOk;
    report("resource_cache_concurrent", ok, msSince(t0),
           "builds=" + std::to_string(callsAfterFirstWave) + " instances=" + std::to_string(instances.size()) +
               " entries=" + std::to_string(cacheSize) + " rebuilt=" + std::to_string(rebuiltOk));
    cache.clear();
}

// ---------------------------------------------------------------- 55
// 纹理只读并发采样：同一 UV 在多线程下必须得到逐位相同的结果。
static void stress_texture_concurrent_sample()
{
    const auto t0 = Clock::now();
    const Texture tex(sr::texgen::checkerboard(128, 8));
    const int probes = 64;
    // 注意：不能写成 expected(size_t(probes))——那会被解析成函数声明（most vexing parse）
    std::vector<Vec3f> expected(static_cast<size_t>(probes));
    for (int i = 0; i < probes; ++i)
    {
        const float u = float((i * 37) % 128) / 128.0f + 0.001f;
        const float v = float((i * 53) % 128) / 128.0f + 0.001f;
        expected[size_t(i)] = tex.sample(u, v, 0.01f, 0.0f, 0.0f, 0.01f);
    }

    std::atomic<int> mismatches{0};
    std::atomic<int> nonfinite{0};
    const int threads = 8;
    std::vector<std::thread> workers;
    for (int t = 0; t < threads; ++t)
        workers.emplace_back([&, t] {
            for (int round = 0; round < 2000; ++round)
                for (int i = 0; i < probes; ++i)
                {
                    const float u = float((i * 37) % 128) / 128.0f + 0.001f;
                    const float v = float((i * 53) % 128) / 128.0f + 0.001f;
                    const Vec3f c = tex.sample(u, v, 0.01f, 0.0f, 0.0f, 0.01f + 0.001f * float(t));
                    if (!allFinite(c))
                        nonfinite.fetch_add(1, std::memory_order_relaxed);
                    // t=0 用与基准完全相同的参数，其余线程换了导数：只查有限性
                    if (t == 0 && (std::abs(c.x - expected[size_t(i)].x) > 1e-5f ||
                                   std::abs(c.y - expected[size_t(i)].y) > 1e-5f))
                        mismatches.fetch_add(1, std::memory_order_relaxed);
                }
        });
    for (std::thread& worker : workers)
        worker.join();

    const bool ok = mismatches.load() == 0 && nonfinite.load() == 0;
    report("texture_concurrent_sample", ok, msSince(t0),
           "samples=" + std::to_string(int64_t(threads) * 2000 * probes) + " mismatches=" +
               std::to_string(mismatches.load()) + " nonfinite=" + std::to_string(nonfinite.load()));
}

// ---------------------------------------------------------------- 56
// Profiler 多线程：thread_local 作用域栈 + 共享事件表必须在并发下不丢不重。
static void stress_profiler_concurrent()
{
    const auto t0 = Clock::now();
    ProfilerEnabledGuard profilerEnabled;
    auto& profiler = Profiler::instance();
    profiler.clear();
    profiler.setMaxEvents(1 << 20);

    const int threads = 8;
    const int perThread = 5000;
    std::vector<std::thread> workers;
    for (int t = 0; t < threads; ++t)
        workers.emplace_back([&profiler] {
            for (int i = 0; i < perThread; ++i)
            {
                profiler.beginScope("worker", "concurrent");
                profiler.endScope();
            }
        });
    for (std::thread& worker : workers)
        worker.join();

    std::filesystem::create_directories("stress-out");
    profiler.flush("stress-out/trace_concurrent.json");
    const size_t expected = size_t(threads) * size_t(perThread);
    const uintmax_t fileSize = std::filesystem::file_size("stress-out/trace_concurrent.json");
    const bool ok = profiler.eventCount() == expected && profiler.droppedEvents() == 0 && fileSize > 1024;
    report("profiler_concurrent", ok, msSince(t0),
           "events=" + std::to_string(profiler.eventCount()) + " expected=" + std::to_string(expected) +
               " dropped=" + std::to_string(profiler.droppedEvents()) + " file=" + std::to_string(fileSize) + "B");

    profiler.clear();
    profiler.setMaxEvents(1000000);
}

// ---------------------------------------------------------------- 57
// 巨大三角形：几何远超视口时仍必须正确覆盖屏幕。
// coverage 的坐标上限是 ±2^20，超出会整块丢弃——一个覆盖全屏的地板三角形
// 会因此完全消失，不会崩但画面缺一大块。
static void stress_huge_triangle_coverage()
{
    const auto t0 = Clock::now();
    const int W = 128, H = 128;

    // 路径 1：MSAA 软光栅（直接屏幕坐标）
    MSAAFlatRenderer<4> msaa(W, H);
    msaa.clear(Color(0, 0, 0));
    msaa.rasterize({-1e7f, -1e7f}, {1e7f, -1e7f}, {0.0f, 1e7f}, 0.5f, 0.5f, 0.5f, Color(255, 255, 255));
    Framebuffer resolved(W, H);
    msaa.resolveTo(resolved);
    int msaaLit = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            if (resolved.getPixel(x, y).r > 0)
                ++msaaLit;

    // 路径 2：主 Renderer（顶点在 NDC 已放大到屏幕巨大）
    Framebuffer fb(W, H, Color(0, 0, 0));
    Renderer renderer(W, H);
    renderer.cullBackface = false;
    renderer.clear(fb, Color(0, 0, 0));
    Mesh mesh;
    mesh.vertices.push_back({{-1e7f, -1e7f, 0.0f}, {0, 0, 1}, {0.0f, 0.0f}});
    mesh.vertices.push_back({{1e7f, -1e7f, 0.0f}, {0, 0, 1}, {1.0f, 0.0f}});
    mesh.vertices.push_back({{0.0f, 1e7f, 0.0f}, {0, 0, 1}, {0.5f, 1.0f}});
    mesh.indices = {0, 1, 2};
    SolidShader shader;
    shader.mvp = Mat4::identity();
    renderer.drawMesh(fb, mesh, shader);
    int rendererLit = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            if (fb.getPixel(x, y).r > 0)
                ++rendererLit;

    const int full = W * H;
    const bool ok = msaaLit == full && rendererLit == full;
    report("huge_triangle_coverage", ok, msSince(t0),
           "msaa=" + std::to_string(msaaLit) + "/" + std::to_string(full) + " renderer=" +
               std::to_string(rendererLit) + "/" + std::to_string(full));
}

// ---------------------------------------------------------------- 58
// 退化三角形（零面积、重复顶点、共线）不得产生像素，也不得除零。
static void stress_degenerate_triangles()
{
    const auto t0 = Clock::now();
    const int W = 64, H = 64;
    Framebuffer fb(W, H, Color(0, 0, 0));
    Renderer renderer(W, H);
    renderer.cullBackface = false;
    renderer.clear(fb, Color(0, 0, 0));

    Mesh mesh;
    auto addTriangle = [&mesh](Vec3f a, Vec3f b, Vec3f c) {
        const int base = int(mesh.vertices.size());
        mesh.vertices.push_back({a, {0, 0, 1}, {0.0f, 0.0f}});
        mesh.vertices.push_back({b, {0, 0, 1}, {1.0f, 0.0f}});
        mesh.vertices.push_back({c, {0, 0, 1}, {0.0f, 1.0f}});
        mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2});
    };
    addTriangle({-0.5f, -0.5f, 0}, {-0.5f, -0.5f, 0}, {-0.5f, -0.5f, 0}); // 三点重合
    addTriangle({-0.4f, 0.0f, 0}, {0.0f, 0.0f, 0}, {0.4f, 0.0f, 0});    // 共线
    addTriangle({0.1f, 0.1f, 0}, {0.1f, 0.1f, 0}, {0.5f, 0.5f, 0});    // 重复两点

    SolidShader shader;
    shader.mvp = Mat4::identity();
    renderer.drawMesh(fb, mesh, shader);

    int painted = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            if (fb.getPixel(x, y).r + fb.getPixel(x, y).g + fb.getPixel(x, y).b > 0)
                ++painted;
    report("degenerate_triangles", painted == 0, msSince(t0), "painted=" + std::to_string(painted));
}

// ---------------------------------------------------------------- 59
// 相机/投影参数极端值：矩阵必须有限；退化投影不得崩溃。
static void stress_extreme_projection_matrix()
{
    const auto t0 = Clock::now();
    int nonfiniteMatrices = 0;
    int crashes = 0;

    struct Params
    {
        float fov;
        float aspect;
        float nearZ;
        float farZ;
        float distance;
        bool requireFinite; // 退化输入的矩阵允许非有限，只要不崩；其余必须有限
    };
    const Params cases[] = {
        {3.14159265f, 1.0f, 0.1f, 100.0f, 4.0f, true}, // 180° 视场
        {3.14159265f * 0.999f, 1.0f, 1e-6f, 1e9f, 1e6f, true},
        {0.0f, 1.0f, 0.1f, 100.0f, 4.0f, false},          // 零视场（tan 为 0，合成失败可接受）
        {1.0f, 1e6f, 0.1f, 100.0f, 4.0f, true},           // 极端宽高比
        {1.0f, 1e-6f, 0.1f, 100.0f, 4.0f, true},
        {1.0f, 1.0f, 0.0f, 0.0f, 4.0f, false},            // 零 near/far
        {1.0f, 1.0f, 100.0f, 0.1f, 4.0f, false},          // near > far
        {1.0f, 1.0f, 0.1f, 100.0f, 1e30f, true},          // 相机极远
    };

    const Mesh sphere = sr::meshgen::sphere(1.0f, 16, 12);
    Framebuffer fb(32, 32, Color(0, 0, 0));
    Renderer renderer(32, 32);
    PBRShader shader;

    for (const Params& p : cases)
    {
        Camera cam;
        cam.fovY = p.fov;
        cam.aspect = p.aspect;
        cam.nearZ = p.nearZ;
        cam.farZ = p.farZ;
        cam.position = {0, 0, p.distance};
        const Mat4 view = cam.viewMatrix();
        const Mat4 proj = cam.projMatrix();
        auto finite = [](const Mat4& m) {
            for (const auto& row : m.m)
                for (float v : row)
                    if (!std::isfinite(v))
                        return false;
            return true;
        };
        if (p.requireFinite && (!finite(view) || !finite(proj)))
            ++nonfiniteMatrices;

        try
        {
            shader.view = view;
            shader.proj = proj;
            shader.model = Mat4::identity();
            shader.mvp = proj * view;
            shader.normalMatrix = Mat4::identity();
            shader.cameraPos = cam.position;
            shader.lightDir = {0, 0, 1};
            renderer.clear(fb, Color(0, 0, 0));
            renderer.drawMesh(fb, sphere, shader);
        }
        catch (const std::exception&)
        {
            ++crashes;
        }
    }

    report("extreme_projection", nonfiniteMatrices == 0 && crashes == 0, msSince(t0),
           "nonfinite=" + std::to_string(nonfiniteMatrices) + " crashes=" + std::to_string(crashes));
}

// ================================================================
// 第三批：IO 格式变体与吞吐基准
// ================================================================

// ---------------------------------------------------------------- 60
// TGA 变体：RLE 24/32 位、顶部原点、拒绝 16 位/调色板/截断。
static void stress_tga_variants()
{
    const auto t0 = Clock::now();
    const std::string dir = "stress-out";
    std::filesystem::create_directories(dir);

    // 2x2 图像（左下角原点）：底行 红、绿；顶行 蓝、白。TGA 通道顺序 BGR。
    const uint8_t pixels[4][3] = {
        {0, 0, 255}, {0, 255, 0},    // 底行：红、绿
        {255, 0, 0}, {255, 255, 255} // 顶行：蓝、白
    };
    auto makeHeader = [](uint8_t type, int w, int h, int bpp, uint8_t descriptor) {
        std::vector<uint8_t> header(18, 0);
        header[2] = type;
        header[12] = uint8_t(w & 0xFF);
        header[13] = uint8_t((w >> 8) & 0xFF);
        header[14] = uint8_t(h & 0xFF);
        header[15] = uint8_t((h >> 8) & 0xFF);
        header[16] = uint8_t(bpp);
        header[17] = descriptor;
        return header;
    };

    int failures = 0;
    auto expectColor = [&failures](const sr::Image& img, int x, int y, Color color) {
        if (!img.valid() || img.at(x, y).r != color.r || img.at(x, y).g != color.g || img.at(x, y).b != color.b)
            ++failures;
    };

    // 1) RLE 24 位，左下原点：4 个不同像素 -> 一个原始包 [0x03][4 像素]
    {
        std::vector<uint8_t> data = makeHeader(10, 2, 2, 24, 0);
        data.push_back(0x03);
        for (const auto& p : pixels)
            data.insert(data.end(), p, p + 3);
        writeFileBytes(dir + "/variants_rle24.tga", data.data(), data.size());
        sr::Image img;
        if (!sr::io::readTga(dir + "/variants_rle24.tga", img))
            ++failures;
        else
        {
            expectColor(img, 0, 0, Color(255, 0, 0));
            expectColor(img, 1, 0, Color(0, 255, 0));
            expectColor(img, 0, 1, Color(0, 0, 255));
            expectColor(img, 1, 1, Color(255, 255, 255));
        }
    }

    // 2) RLE 32 位：alpha 必须保留
    {
        std::vector<uint8_t> data = makeHeader(10, 2, 2, 32, 8);
        data.push_back(0x03);
        for (const auto& p : pixels)
        {
            data.insert(data.end(), p, p + 3);
            data.push_back(128);
        }
        writeFileBytes(dir + "/variants_rle32.tga", data.data(), data.size());
        sr::Image img;
        if (!sr::io::readTga(dir + "/variants_rle32.tga", img) || !img.valid() || img.at(0, 0).a != 128)
            ++failures;
    }

    // 3) 顶部原点（descriptor 0x20）：文件里首行是图像顶行
    {
        std::vector<uint8_t> data = makeHeader(2, 2, 2, 24, 0x20);
        for (int i = 2; i < 4; ++i) // 先写顶行（蓝、白）
            data.insert(data.end(), pixels[i], pixels[i] + 3);
        for (int i = 0; i < 2; ++i) // 再写底行（红、绿）
            data.insert(data.end(), pixels[i], pixels[i] + 3);
        writeFileBytes(dir + "/variants_top.tga", data.data(), data.size());
        sr::Image img;
        if (!sr::io::readTga(dir + "/variants_top.tga", img))
            ++failures;
        else
        {
            expectColor(img, 0, 0, Color(255, 0, 0)); // 行 0 仍是底行
            expectColor(img, 0, 1, Color(0, 0, 255));
        }
    }

    // 4) 拒绝：16 位、调色板、截断 RLE、零尺寸
    sr::Image untouched;
    untouched.width = 7;
    untouched.height = 5;
    auto rejected = [&](const std::string& name, const std::vector<uint8_t>& data) {
        writeFileBytes(dir + "/" + name + ".tga", data.data(), data.size());
        sr::Image img = untouched;
        const bool refused = !sr::io::readTga(dir + "/" + name + ".tga", img);
        // 失败不得发布半成品（尺寸被改成 2x2 就算错）
        return refused && img.width == 7 && img.height == 5;
    };

    std::vector<uint8_t> bpp16 = makeHeader(2, 2, 2, 16, 0);
    bpp16.resize(18 + 8, 0);
    if (!rejected("variants_16bit", bpp16))
        ++failures;

    std::vector<uint8_t> colormapped = makeHeader(1, 2, 2, 32, 0);
    colormapped.resize(18 + 16, 0);
    if (!rejected("variants_colormap", colormapped))
        ++failures;

    std::vector<uint8_t> truncated = makeHeader(10, 4, 4, 24, 0);
    truncated.push_back(0x01);
    if (!rejected("variants_truncated", truncated))
        ++failures;

    std::vector<uint8_t> zeroSize = makeHeader(2, 0, 0, 24, 0);
    if (!rejected("variants_zero", zeroSize))
        ++failures;

    report("tga_variants", failures == 0, msSince(t0), "failures=" + std::to_string(failures));
}

// ---------------------------------------------------------------- 61
// 声明尺寸与文件长度不一致：必须先拒绝，不能先按声明尺寸分配缓冲。
// 8192×8192×3 = 192 MB，而文件只有 118 字节——放大 170 万倍的外部输入。
static void stress_tga_oversized_header()
{
    const auto t0 = Clock::now();
    const std::string dir = "stress-out";
    std::filesystem::create_directories(dir);

    std::vector<uint8_t> data(18, 0);
    data[2] = 2; // 未压缩真彩色
    data[12] = 0x00;
    data[13] = 0x20; // 8192
    data[14] = 0x00;
    data[15] = 0x20; // 8192
    data[16] = 24;
    data.resize(18 + 100, 0);
    writeFileBytes(dir + "/oversized.tga", data.data(), data.size());

    sr::Image img;
    const size_t before = processPrivateBytes();
    const bool accepted = sr::io::readTga(dir + "/oversized.tga", img);
    const size_t after = processPrivateBytes();
    const size_t growthMb = (after > before ? after - before : 0) / (1024 * 1024);

    const bool ok = !accepted && !img.valid();
    report("tga_oversized_header", ok, msSince(t0),
           "accepted=" + std::to_string(accepted) + " memGrowth=" + std::to_string(growthMb) + "MB");
}

// ---------------------------------------------------------------- 62
// TGA 写出：超出 16 位头限制、不可写路径必须返回 false。
static void stress_tga_write_failures()
{
    const auto t0 = Clock::now();
    Framebuffer wide(70000, 1, Color(0, 0, 0)); // 宽 > 65535，头里放不下
    const bool tooWide = !sr::io::writeTga("stress-out/too_wide.tga", wide);
    Framebuffer small(4, 4, Color(1, 2, 3));
    const bool badPath = !sr::io::writeTga("stress-out/no_such_dir/x.tga", small);
    const bool ok = tooWide && badPath;
    report("tga_write_failures", ok, msSince(t0),
           "tooWideRejected=" + std::to_string(tooWide) + " badPathRejected=" + std::to_string(badPath));
}

// ---------------------------------------------------------------- 63
// OBJ 极端行长：单行 2 MB 的顶点/面数据不得导致栈溢出或平方级拷贝。
static void stress_obj_long_line()
{
    const auto t0 = Clock::now();
    const std::string path = "stress-out/long_line.obj";
    std::string content;
    content.reserve(2u << 20);
    content += "# 2MB single line\n#";
    while (content.size() < (1u << 20))
        content += 'x';
    content += "\nv 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    writeFileText(path, content);

    Mesh mesh;
    const bool loaded = sr::io::loadObjMesh(path, mesh);
    const bool ok = loaded && mesh.vertices.size() == 3 && mesh.indices.size() == 3;
    report("obj_long_line", ok, msSince(t0),
           "loaded=" + std::to_string(loaded) + " verts=" + std::to_string(mesh.vertices.size()) + " tris=" +
               std::to_string(mesh.indices.size() / 3));
}

// ---------------------------------------------------------------- 64
// 吞吐基线：2D 位图合成。best-of-3 降低噪声，baseline JSON 抓性能回退。
static void stress_bench_blit_throughput()
{
    const int W = 1024, H = 1024;
    Framebuffer fb(W, H, Color(0, 0, 0));
    const Texture tex(sr::texgen::checkerboard(512, 16));
    const auto t0 = Clock::now();

    double best = 1e30;
    for (int rep = 0; rep < 3; ++rep)
    {
        const auto start = Clock::now();
        sr::draw2d::blitTexture(fb, tex, 0, 0, W, H);
        best = std::min(best, msSince(start));
    }
    const double mpix = double(W) * H / (best * 1000.0);
#ifdef NDEBUG
    const bool ok = mpix > 1.0;
#else
    // Debug/ASAN 无优化且带红区，阈值只用于发现数量级崩坏
    const bool ok = mpix > 0.1;
#endif
    report("bench_blit_throughput", ok, msSince(t0),
           "best=" + std::to_string(best) + "ms " + std::to_string(mpix) + " Mpix/s");
}

// ---------------------------------------------------------------- 65
static void stress_bench_fill_throughput()
{
    const int W = 1024, H = 1024;
    Framebuffer fb(W, H, Color(0, 0, 0));
    const auto t0 = Clock::now();

    double best = 1e30;
    for (int rep = 0; rep < 3; ++rep)
    {
        const auto start = Clock::now();
        sr::draw2d::fillRect(fb, 0, 0, W, H, Color(10, 20, 30));
        sr::draw2d::fillRect(fb, 0, 0, W, H, Color(40, 50, 60));
        best = std::min(best, msSince(start) * 0.5);
    }
    const double mpix = double(W) * H / (best * 1000.0);
    report("bench_fill_throughput", mpix > 1.0, msSince(t0),
           "best=" + std::to_string(best) + "ms " + std::to_string(mpix) + " Mpix/s");
}

// ---------------------------------------------------------------- 66
// 纹理采样吞吐：直接反映 mip/lod 计算的每像素开销。
static void stress_bench_texture_sampling()
{
    const Texture tex(sr::texgen::checkerboard(256, 16));
    const int samples = 1 << 20;
    const auto t0 = Clock::now();

    auto run = [&](bool nearest, bool trilinear) {
        const auto start = Clock::now();
        double sink = 0.0;
        for (int i = 0; i < samples; ++i)
        {
            const float u = float(i & 1023) / 1024.0f;
            const float v = float((i >> 10) & 1023) / 1024.0f;
            const Vec3f c = nearest   ? tex.sampleNearest(u, v)
                            : trilinear ? tex.sample(u, v, 0.004f, 0.0f, 0.0f, 0.004f)
                                        : tex.sampleBilinear(u, v);
            sink += double(c.x);
        }
        const double ms = msSince(start);
        return std::pair<double, double>(ms, sink);
    };

    const auto nearest = run(true, false);
    const auto bilinear = run(false, false);
    const auto trilinear = run(false, true);
    // 用 sink 阻止整段循环被优化掉
    const bool sinkOk = nearest.second > 0.0 && bilinear.second > 0.0 && trilinear.second > 0.0;

    auto millions = [samples](double ms) { return double(samples) / (ms * 1000.0); };
    report("bench_texture_sampling", sinkOk, msSince(t0),
           "nearest=" + std::to_string(millions(nearest.first)) + "M/s bilinear=" +
               std::to_string(millions(bilinear.first)) + "M/s trilinear=" +
               std::to_string(millions(trilinear.first)) + "M/s");
}

// ---------------------------------------------------------------- 67
// 小三角形光栅吞吐：真实场景中三角形远小于屏幕，包围盒开销占比最高。
static void stress_bench_raster_small_tris()
{
    const int W = 256, H = 256;
    MSAAFlatRenderer<1> renderer(W, H);
    renderer.clear(Color(0, 0, 0));
    const auto t0 = Clock::now();

    std::mt19937 rng(2024);
    std::uniform_real_distribution<float> pos(0.0f, float(W));
    std::uniform_real_distribution<float> off(-3.0f, 3.0f);
    const int count = 20000;
    for (int i = 0; i < count; ++i)
    {
        const float x = pos(rng), y = pos(rng);
        renderer.rasterize({x, y}, {x + 2.0f + off(rng), y}, {x, y + 2.0f + off(rng)}, 0.5f, 0.5f, 0.5f,
                           Color(200, 100, 50));
    }
    const double ms = msSince(t0);
    const double mtris = double(count) / (ms * 1000.0);
    report("bench_raster_small_tris", mtris > 0.01, ms, std::to_string(mtris) + " Mtris/s (" +
                                                                     std::to_string(count) + " tris)");
}

// ================================================================
// 第四批：深度/覆盖/着色器/动画/粒子 的数值边界
// ================================================================

namespace
{
// 带颜色的平面着色器：验证深度测试时需要用不同颜色区分“谁赢了”。
struct FlatColorShader : Shader
{
    Mat4 mvp = Mat4::identity();
    Vec3f color{1, 1, 1};
    Vec4f vertex(const Vertex& in, Varyings& out) override
    {
        out.worldPos = in.position;
        out.normal = Vec3f{0, 0, 1};
        out.uv = in.uv;
        return mvp * Vec4f(in.position, 1.0f);
    }
    bool fragment(const Varyings&, Vec3f& out) override
    {
        out = color;
        return true;
    }
};
}  // namespace

// ---------------------------------------------------------------- 68
// 深度测试：近遮远、更远不覆盖、相等深度不覆盖（必须是严格小于）。
static void stress_depth_test_order()
{
    const auto t0 = Clock::now();
    const int W = 32, H = 32;
    Framebuffer fb(W, H, Color(0, 0, 0));
    Renderer renderer(W, H);
    renderer.cullBackface = false;
    renderer.clear(fb, Color(0, 0, 0));

    Mesh quad;
    quad.vertices.push_back({{-0.8f, -0.8f, 0.0f}, {0, 0, 1}, {0.0f, 0.0f}});
    quad.vertices.push_back({{0.8f, -0.8f, 0.0f}, {0, 0, 1}, {1.0f, 0.0f}});
    quad.vertices.push_back({{0.8f, 0.8f, 0.0f}, {0, 0, 1}, {1.0f, 1.0f}});
    quad.vertices.push_back({{-0.8f, 0.8f, 0.0f}, {0, 0, 1}, {0.0f, 1.0f}});
    quad.indices = {0, 1, 2, 0, 2, 3};

    FlatColorShader shader;
    shader.mvp = Mat4::identity();

    auto drawAt = [&](float ndcZ, Vec3f color) {
        shader.color = color;
        // 深度写在裁剪空间 z 上，w=1 时即 NDC z
        Mesh shifted = quad;
        for (Vertex& v : shifted.vertices)
            v.position.z = ndcZ;
        renderer.drawMesh(fb, shifted, shader);
    };

    const Color farColor(10, 20, 30), nearColor(200, 100, 50), tieColor(0, 255, 0);
    drawAt(0.7f, {farColor.r / 255.0f, farColor.g / 255.0f, farColor.b / 255.0f});
    drawAt(0.3f, {nearColor.r / 255.0f, nearColor.g / 255.0f, nearColor.b / 255.0f});
    drawAt(0.7f, {farColor.r / 255.0f, farColor.g / 255.0f, farColor.b / 255.0f}); // 更远，不得覆盖
    drawAt(0.3f, {tieColor.r / 255.0f, tieColor.g / 255.0f, tieColor.b / 255.0f}); // 相等，不得覆盖

    const Color center = fb.getPixel(W / 2, H / 2);
    const bool ok = center.r == nearColor.r && center.g == nearColor.g && center.b == nearColor.b;
    report("depth_test_order", ok, msSince(t0),
           "rgba=" + std::to_string(center.r) + "," + std::to_string(center.g) + "," + std::to_string(center.b));
}

// ---------------------------------------------------------------- 69
// MSAA 边缘：斜边的边界像素应是部分覆盖的混合值，而不是非黑即白。
static void stress_msaa_edge_coverage()
{
    const auto t0 = Clock::now();
    const int W = 64, H = 64;
    MSAAFlatRenderer<4> msaa(W, H);
    msaa.clear(Color(0, 0, 0));
    // 斜边穿过图面：对角线两侧应出现部分覆盖的灰阶
    msaa.rasterize({0.0f, 0.0f}, {float(W), float(H)}, {float(W), 0.0f}, 0.5f, 0.5f, 0.5f,
                   Color(255, 255, 255));
    Framebuffer out(W, H);
    msaa.resolveTo(out);

    int partial = 0, full = 0, empty = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            const int r = out.getPixel(x, y).r;
            if (r == 0)
                ++empty;
            else if (r == 255)
                ++full;
            else
                ++partial;
        }
    // 一条 64 像素的对角线至少有几十个部分覆盖像素
    const bool ok = partial > 20 && full > 100 && empty > 100;
    report("msaa_edge_coverage", ok, msSince(t0),
           "partial=" + std::to_string(partial) + " full=" + std::to_string(full) +
               " empty=" + std::to_string(empty));
}

// ---------------------------------------------------------------- 70
// PBR 极端材质：粗糙度 0/1、金属度 NaN、零法线/零视角/零光向都不得产出 NaN 像素。
static void stress_pbr_extreme_material()
{
    const auto t0 = Clock::now();
    const int W = 32, H = 32;
    Framebuffer fb(W, H, Color(0, 0, 0));
    Renderer renderer(W, H);
    renderer.clear(fb, Color(0, 0, 0));

    Mesh sphere = sr::meshgen::sphere(1.0f, 16, 12);
    // 零法线 + 非有限法线混入（加载器允许缺 NORMAL，但运行时不能崩）
    for (size_t i = 0; i < sphere.vertices.size(); i += 7)
        sphere.vertices[i].normal = Vec3f{0, 0, 0};

    PBRShader shader;
    Camera cam;
    cam.position = {0, 0, 3};
    cam.aspect = 1.0f;
    shader.view = cam.viewMatrix();
    shader.proj = cam.projMatrix();
    shader.model = Mat4::identity();
    shader.mvp = shader.proj * shader.view;
    shader.normalMatrix = Mat4::identity();
    shader.cameraPos = cam.position;

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    struct Case
    {
        float metallic;
        float roughness;
        Vec3f light;
    };
    const Case cases[] = {
        {0.0f, 0.0f, {0, 0, 1}},   {1.0f, 1.0f, {0, -1, 0}},  {nan, 0.5f, {0, 0, 1}},
        {0.5f, nan, {0, 0, 1}},    {0.5f, 0.5f, {0, 0, 0}},   {0.5f, 0.5f, {inf, 0, 0}},
        {-1.0f, 2.0f, {0, 0, 1}},
    };

    int painted = 0;
    for (const Case& c : cases)
    {
        shader.metallic = c.metallic;
        shader.roughness = c.roughness;
        shader.lightDir = c.light;
        renderer.clear(fb, Color(0, 0, 0));
        renderer.drawMesh(fb, sphere, shader);
        for (int y = 0; y < H; y += 3)
            for (int x = 0; x < W; x += 3)
                if (fb.getPixel(x, y).r + fb.getPixel(x, y).g + fb.getPixel(x, y).b > 0)
                    ++painted;
    }
    // 至少正常参数下要画出东西；所有情况不得崩溃
    const bool ok = painted > 0;
    report("pbr_extreme_material", ok, msSince(t0), "paintedProbes=" + std::to_string(painted));
}

// ---------------------------------------------------------------- 71
// 动画数据损坏：采样器/节点索引越界、时间轴非单调，必须被拦住且不产出 NaN。
static void stress_animation_malformed()
{
    const auto t0 = Clock::now();
    int nanPoses = 0;
    int thrown = 0;

    std::vector<SceneNode> nodes(1);
    const float nan = std::numeric_limits<float>::quiet_NaN();

    auto probe = [&](const AnimationClip& clip, float time) {
        try
        {
            const auto poses = sr::animation::sampleAnimation(nodes, clip, time);
            for (const auto& p : poses)
                if (!std::isfinite(p.translation.x) || !std::isfinite(p.translation.y) ||
                    !std::isfinite(p.translation.z))
                    ++nanPoses;
        }
        catch (const std::exception&)
        {
            ++thrown;
        }
    };

    // 1) samplerIndex 越界
    {
        AnimationClip clip;
        AnimationChannel channel;
        channel.samplerIndex = 3;
        channel.targetNode = 0;
        channel.path = AnimationChannel::Path::Translation;
        clip.channels.push_back(channel);
        probe(clip, 0.5f);
        probe(clip, nan);
    }
    // 2) targetNode 越界
    {
        AnimationClip clip;
        AnimationSampler sampler;
        sampler.times = {0.0f, 1.0f};
        sampler.values = {Vec4f{0, 0, 0, 0}, Vec4f{1, 0, 0, 0}};
        clip.samplers.push_back(sampler);
        AnimationChannel channel;
        channel.samplerIndex = 0;
        channel.targetNode = 99;
        channel.path = AnimationChannel::Path::Translation;
        clip.channels.push_back(channel);
        probe(clip, 0.5f);
    }
    // 3) 非单调时间轴 + 全 NaN 关键帧（validated=false 走校验路径）
    {
        AnimationClip clip;
        AnimationSampler sampler;
        sampler.times = {0.0f, 0.1f, 0.05f};
        sampler.values = {Vec4f{nan, 0, 0, 0}, Vec4f{0, nan, 0, 0}, Vec4f{0, 0, nan, 0}};
        clip.samplers.push_back(sampler);
        AnimationChannel channel;
        channel.samplerIndex = 0;
        channel.targetNode = 0;
        channel.path = AnimationChannel::Path::Translation;
        clip.channels.push_back(channel);
        clip.validated = false;
        probe(clip, 0.07f);
        probe(clip, 1e9f);
    }
    // 4) 空片段
    {
        AnimationClip clip;
        probe(clip, 0.0f);
    }

    const bool ok = nanPoses == 0 && thrown == 0;
    report("animation_malformed", ok, msSince(t0),
           "nanPoses=" + std::to_string(nanPoses) + " thrown=" + std::to_string(thrown));
}

// ---------------------------------------------------------------- 72
// 粒子系统极端配置：零容量/零速率/零生命/NaN 时间步。
static void stress_particles_extreme()
{
    const auto t0 = Clock::now();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    int failures = 0;

    struct Case
    {
        int maxParticles;
        float rate;
        float lifeMin;
        float lifeMax;
        float dt;
    };
    const Case cases[] = {
        {0, 100.0f, 0.1f, 1.0f, 0.016f},     {100, 0.0f, 0.1f, 1.0f, 0.016f},
        {100, 100.0f, 0.0f, 0.0f, 0.016f},    {100, 100.0f, 1.0f, 0.1f, 0.016f},
        {100, 100.0f, 0.1f, 1.0f, nan},       {100, nan, 0.1f, 1.0f, 0.016f},
        {-5, 100.0f, 0.1f, 1.0f, 0.016f},
    };

    for (const Case& c : cases)
    {
        EmitterConfig cfg;
        cfg.maxParticles = c.maxParticles;
        cfg.rate = c.rate;
        cfg.lifeMin = c.lifeMin;
        cfg.lifeMax = c.lifeMax;
        ParticleSystem ps(cfg);
        for (int frame = 0; frame < 10; ++frame)
            ps.update(c.dt);
        for (const Particle* p : ps.getAlive())
            if (!std::isfinite(p->position.x) || !std::isfinite(p->life) || !std::isfinite(p->size))
                ++failures;
        if (c.maxParticles <= 0 && ps.aliveCount() != 0)
            ++failures;
    }

    report("particles_extreme", failures == 0, msSince(t0), "pollutedParticles=" + std::to_string(failures));
}

// ---------------------------------------------------------------- 73
// 帧缓冲边界读：越界读返回默认色而不是越界内存。
static void stress_framebuffer_bounds()
{
    const auto t0 = Clock::now();
    int failures = 0;
    Framebuffer fb(8, 8, Color(11, 22, 33));

#ifdef NDEBUG
    // Release：越界读写静默拒绝，返回黑色、不改变图像。
    const int probes[][2] = {{-1, 0}, {0, -1}, {8, 0}, {0, 8}, {-1000, -1000}, {100000, 100000}};
    for (const auto& p : probes)
    {
        const Color c = fb.getPixel(p[0], p[1]);
        if (c.r != 0 || c.g != 0 || c.b != 0)
            ++failures;
        fb.setPixel(p[0], p[1], Color(255, 255, 255)); // 不得越界写
    }
#else
    // Debug：Framebuffer 的越界访问是“调用方错误”，库用 assert 强制契约。
    // 压测不应把这种预期断言当成缺陷，只验证合法范围内的读写。
    fb.setPixel(3, 3, Color(11, 22, 33));
    if (fb.getPixel(3, 3).r != 11)
        ++failures;
#endif

    // 写入后图像应保持不变
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x)
            if (fb.getPixel(x, y).r != 11)
                ++failures;

    report("framebuffer_bounds", failures == 0, msSince(t0), "failures=" + std::to_string(failures));
}

// ---------------------------------------------------------------- 74
// 线程池析构：队列里未完成的任务必须执行完再退出，不能静默丢弃。
static void stress_thread_pool_drain()
{
    const auto t0 = Clock::now();
    std::atomic<int> executed{0};
    const int tasks = 2000;
    {
        ThreadPool pool(4);
        for (int i = 0; i < tasks; ++i)
            pool.submit([&executed] {
                // 用 CPU 忙等（约微秒级）而不是 sleep：Windows 上 sleep_for(50us)
                // 实际会被定时器粒度放大到毫秒级，2000 个任务会拖到 6 秒。
                volatile unsigned int sink = 0;
                for (unsigned int k = 0; k < 2000; ++k)
                    sink += k;
                (void)sink;
                executed.fetch_add(1, std::memory_order_relaxed);
            });
        // 不调 waitAll：直接析构，队列非空
    }
    const bool drained = executed.load() == tasks;

    // 每轮都新建池：旧池的线程必须已 join，否则线程数会累积
    int threadsAfter = 0;
    {
        ThreadPool probe(3);
        threadsAfter = probe.threadCount();
    }

    report("thread_pool_drain", drained && threadsAfter == 3, msSince(t0),
           "executed=" + std::to_string(executed.load()) + "/" + std::to_string(tasks) + " threads=" +
               std::to_string(threadsAfter));
}

// ---------------------------------------------------------------- 75
// 图像解码吞吐：PNG 编码 → 反复解码，测编解码链路是否有数量级回归。
static void stress_image_decode_throughput()
{
    const auto t0 = Clock::now();
    const int size = 1024;
    Framebuffer fb(size, size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            fb.setPixel(x, y, Color(uint8_t(x), uint8_t(y), uint8_t((x ^ y) & 0xFF)));

    std::filesystem::create_directories("stress-out");
    const std::string path = "stress-out/decode_bench.png";
    const bool wrote = sr::io::writeImage(path, fb);

    double best = 1e30;
    size_t decodedPixels = 0;
    const int rounds = 5;
    for (int i = 0; i < rounds; ++i)
    {
        sr::Image image;
        const auto start = Clock::now();
        const bool ok = sr::io::loadImage(path, image);
        const double ms = msSince(start);
        if (!ok)
        {
            decodedPixels = 0;
            break;
        }
        decodedPixels = size_t(image.width) * size_t(image.height);
        best = std::min(best, ms);
    }

    const bool ok = wrote && decodedPixels == size_t(size) * size && best < 1e29;
    const double mpix = ok ? double(decodedPixels) / (best * 1000.0) : 0.0;
    report("image_decode_throughput", ok, msSince(t0),
           "best=" + std::to_string(best) + "ms " + std::to_string(mpix) + " Mpix/s");
}

// ================================================================
// 第五批：场景图 / 并发渲染 / 后处理链 / 资源生命周期
// ================================================================

// ---------------------------------------------------------------- 76
// 场景图更新：1 万节点深链，每帧重算局部矩阵与世界矩阵。
// 这是动画/场景系统每帧都要做的事，考验 O(n) 路径上的矩阵构造开销。
static void stress_scene_graph_update()
{
    const auto t0 = Clock::now();
    const int nodes = 10000;
    std::vector<SceneNode> graph(static_cast<size_t>(nodes));
    for (int i = 0; i < nodes; ++i)
    {
        graph[size_t(i)].translation = {0.001f, 0.0f, 0.0f};
        if (i + 1 < nodes)
            graph[size_t(i)].children.push_back(i + 1);
    }

    std::vector<Mat4> world(static_cast<size_t>(nodes));
    const int frames = 50;
    for (int frame = 0; frame < frames; ++frame)
    {
        Mat4 parent = Mat4::identity();
        for (int i = 0; i < nodes; ++i)
        {
            world[size_t(i)] = parent * graph[size_t(i)].localMatrix();
            parent = world[size_t(i)];
        }
    }

    // 深链累乘后叶子应偏移 10000 * 0.001，误差随帧数不能爆
    const Vec3f leaf = world.back().transformPoint({0, 0, 0});
    const float expected = float(nodes - 1) * 0.001f;
    const bool ok = std::isfinite(leaf.x) && std::abs(leaf.x - expected) < 0.01f;
    const double ms = msSince(t0);
    report("scene_graph_update", ok, ms,
           "nodes=" + std::to_string(nodes) + " frames=" + std::to_string(frames) + " leafX=" +
               std::to_string(leaf.x));
}

// ---------------------------------------------------------------- 77
// 并发渲染：每个线程持有自己的 Framebuffer/Renderer，共享只读 Mesh。
// 库内若有全局可变状态（缓存、Profiler 结构、静态 RNG），这里会暴露成
// 数据竞争、吞吐崩塌或像素不一致。
static void stress_render_multithreaded()
{
    const auto t0 = Clock::now();
    const int W = 256, H = 256;
    const int draws = 20;
    const Mesh sphere = sr::meshgen::sphere(1.0f, 32, 24);

    auto renderBatches = [&](int threads) {
        std::vector<std::thread> workers;
        workers.reserve(static_cast<size_t>(threads));
        const auto start = Clock::now();
        for (int t = 0; t < threads; ++t)
            workers.emplace_back([&sphere, W, H, draws] {
                Framebuffer fb(W, H, Color(10, 12, 16));
                Renderer renderer(W, H);
                PBRShader shader = makePbr(W, H);
                for (int i = 0; i < draws; ++i)
                {
                    const Mat4 model = Mat4::rotateY(0.2f * float(i));
                    shader.model = model;
                    shader.mvp = shader.proj * shader.view * model;
                    shader.normalMatrix = model.inversed().transposed();
                    renderer.clear(fb, Color(10, 12, 16));
                    renderer.drawMesh(fb, sphere, shader);
                }
                // 读一次像素，防止整段被优化掉
                volatile int guard = fb.getPixel(W / 2, H / 2).r;
                (void)guard;
            });
        for (std::thread& worker : workers)
            worker.join();
        return msSince(start);
    };

    const double singleMs = renderBatches(1);
    const double parallelMs = renderBatches(4);
    // 4 线程各自完成与单线程相同的一批，总工作量是 4 倍：
    // speedup = (单线程一批耗时 × 4) / 4 线程并行耗时。
    const double speedup = parallelMs > 0.0 ? singleMs * 4.0 / parallelMs : 0.0;
    // 4 核下应接近 4；低于 2 说明存在串行化热点（全局锁 / 共享分配器）。
    const bool ok = std::isfinite(speedup) && speedup > 2.0;
    report("render_multithreaded", ok, msSince(t0),
           "single=" + std::to_string(int(singleMs)) + "ms x4=" + std::to_string(int(parallelMs)) +
               "ms speedup=" + std::to_string(speedup));
}

// ---------------------------------------------------------------- 78
// 后处理链 1080p：亮部提取 → 双向高斯 → 相加 → ACES + gamma。
// 这是 bloom 的完整每帧成本，单项快不代表链路快（每步都是全屏 FloatFramebuffer）。
static void stress_postprocess_chain_1080p()
{
    const int W = 1920, H = 1080;
    const auto t0 = Clock::now();
    FloatFramebuffer scene(W, H);
    FloatFramebuffer bright(W, H);
    FloatFramebuffer blur(W, H);
    Framebuffer ldr(W, H);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            scene.setPixel(x, y, Vec3f{float(x) / float(W), float(y) / float(H), 0.4f});

    double best = 1e30;
    double brightMs = 0.0, blurMs = 0.0, addMs = 0.0, resolveMs = 0.0;
    for (int rep = 0; rep < 2; ++rep)
    {
        const auto start = Clock::now();
        const auto tBright = Clock::now();
        sr::postprocess::brightPass(scene, bright, 0.8f);
        brightMs = msSince(tBright);
        const auto tBlur = Clock::now();
        sr::postprocess::gaussianBlur(bright, blur, true, 8);
        sr::postprocess::gaussianBlur(blur, bright, false, 8);
        blurMs = msSince(tBlur);
        const auto tAdd = Clock::now();
        sr::postprocess::addBlend(scene, bright, 0.6f);
        addMs = msSince(tAdd);
        const auto tResolve = Clock::now();
        sr::postprocess::resolveToLdr(scene, ldr);
        resolveMs = msSince(tResolve);
        best = std::min(best, msSince(start));
    }
    const bool ok = best < 1e29 && ldr.getPixel(W / 2, H / 2).r + ldr.getPixel(W / 2, H / 2).g > 0;
    report("postprocess_chain_1080p", ok, msSince(t0),
           "best=" + std::to_string(int(best)) + "ms bright=" + std::to_string(int(brightMs)) + " blur=" +
               std::to_string(int(blurMs)) + " add=" + std::to_string(int(addMs)) + " resolve=" +
               std::to_string(int(resolveMs)));
}

// ---------------------------------------------------------------- 79
// 纹理生命周期：反复创建/销毁带 mip 链的纹理，进程私有内存不得持续增长。
static void stress_texture_lifecycle()
{
    const auto t0 = Clock::now();

    auto churn = [](int count) {
        for (int i = 0; i < count; ++i)
        {
            Texture texture(sr::texgen::checkerboard(128, 8));
            if (!texture.valid() || texture.mipLevels() < 8)
                return false;
        }
        return true;
    };

    // 先用 200 张预热分配器：第一轮的“增长”主要是堆保留策略，不是泄漏。
    // 真正能证明泄漏的是：同样的 500 张再跑一轮，内存还在涨。
    const bool warmup = churn(200);
    const size_t before = processPrivateBytes();
    const bool firstOk = churn(500);
    const size_t mid = processPrivateBytes();
    const bool secondOk = churn(500);
    const size_t after = processPrivateBytes();

    const size_t firstGrowthMb = (mid > before ? mid - before : 0) / (1024 * 1024);
    const size_t secondGrowthMb = (after > mid ? after - mid : 0) / (1024 * 1024);

    // 500 张 128² + mip ≈ 43 MB。若第二轮仍涨过 32 MB，就是每张都在漏。
    const bool ok = warmup && firstOk && secondOk && secondGrowthMb < 32;
    report("texture_lifecycle", ok, msSince(t0),
           "first=" + std::to_string(firstGrowthMb) + "MB second=" + std::to_string(secondGrowthMb) + "MB");
}

// ---------------------------------------------------------------- 80
// Profiler 关闭时的开销：业务代码可以到处包 SR_PROFILE_SCOPE，
// 关闭后必须是“几乎免费”，否则默认关闭也无法承受。
static void stress_profiler_disabled_overhead()
{
    auto& profiler = Profiler::instance();
    const bool wasEnabled = profiler.isEnabled();
    profiler.setEnabled(false);
    profiler.clear();

    const auto t0 = Clock::now();
    const int iterations = 1000000;
    for (int i = 0; i < iterations; ++i)
    {
        profiler.beginScope("disabled_scope");
        profiler.endScope();
    }
    const double ms = msSince(t0);
    const bool noEvents = profiler.eventCount() == 0;
    profiler.setEnabled(wasEnabled);

#ifdef NDEBUG
    // Release：关闭后只剩一次原子读，100 万对应在 20 ms 内。
    const double limitMs = 50.0;
#else
    // Debug 未优化，阈值只用于发现“开关失效”（失效时是亚秒级）。
    const double limitMs = 300.0;
#endif
    const bool ok = noEvents && ms < limitMs;
    report("profiler_disabled_overhead", ok, ms,
           std::to_string(iterations) + " pairs events=" + std::to_string(profiler.eventCount()));
}

// ---------------------------------------------------------------- 81
// MSAA resolve 吞吐：4x 1080p 全屏覆盖后解析，反映离屏合成的固定成本。
static void stress_msaa_resolve_throughput()
{
    const int W = 1920, H = 1080;
    const auto t0 = Clock::now();
    MSAAFlatRenderer<4> renderer(W, H);
    renderer.clear(Color(0, 0, 0));
    renderer.rasterize({0, 0}, {float(W), 0}, {0, float(H)}, 0.2f, 0.2f, 0.2f, Color(200, 60, 60));
    Framebuffer out(W, H);

    double best = 1e30;
    for (int rep = 0; rep < 3; ++rep)
    {
        const auto start = Clock::now();
        renderer.resolveTo(out);
        best = std::min(best, msSince(start));
    }
    const double mpix = double(W) * H / (best * 1000.0);
#ifdef NDEBUG
    const bool ok = mpix > 1.0;
#else
    const bool ok = mpix > 0.1;
#endif
    report("msaa_resolve_throughput", ok, msSince(t0),
           "best=" + std::to_string(best) + "ms " + std::to_string(mpix) + " Mpix/s");
}

// ================================================================
// 第六批：非法网格 / Arena 边界 / 粒子运行期容量
// ================================================================

// ---------------------------------------------------------------- 82
// 非法索引的 Mesh：Mesh 是公共结构，用户可以手拼任何内容。
// 负索引、越界索引、残余顶点都必须被丢弃，而不是越界读。
static void stress_renderer_invalid_mesh()
{
    const auto t0 = Clock::now();
    const int W = 32, H = 32;
    int painted = 0;

    auto runCase = [&](const Mesh& mesh) {
        Framebuffer fb(W, H, Color(0, 0, 0));
        Renderer renderer(W, H);
        renderer.cullBackface = false;
        renderer.clear(fb, Color(0, 0, 0));
        FlatColorShader shader;
        shader.mvp = Mat4::identity();
        renderer.drawMesh(fb, mesh, shader);
        int lit = 0;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                if (fb.getPixel(x, y).r > 0)
                    ++lit;
        painted += lit;
    };

    auto makeQuad = [] {
        Mesh mesh;
        mesh.vertices.push_back({{-0.8f, -0.8f, 0.0f}, {0, 0, 1}, {0.0f, 0.0f}});
        mesh.vertices.push_back({{0.8f, -0.8f, 0.0f}, {0, 0, 1}, {1.0f, 0.0f}});
        mesh.vertices.push_back({{0.8f, 0.8f, 0.0f}, {0, 0, 1}, {1.0f, 1.0f}});
        mesh.indices = {0, 1, 2};
        return mesh;
    };

    Mesh negative = makeQuad();
    negative.indices = {-1, 1, 2};
    runCase(negative);

    Mesh outOfRange = makeQuad();
    outOfRange.indices = {0, 1, 300};
    runCase(outOfRange);

    Mesh incomplete = makeQuad();
    incomplete.indices = {0, 1}; // 不完整的三角形
    runCase(incomplete);

    Mesh empty;
    runCase(empty);

    Mesh noIndices = makeQuad();
    noIndices.indices.clear(); // 无索引：不得越界读
    runCase(noIndices);

    // 以上全部不得画出像素，也不得崩溃
    const bool ok = painted == 0;
    report("renderer_invalid_mesh", ok, msSince(t0), "painted=" + std::to_string(painted));
}

// ---------------------------------------------------------------- 83
// Arena 边界：可增长率分配器的真实契约——
// 超容量是“自动扩容”而不是拒绝；非法对齐与溢出请求必须拒绝。
static void stress_arena_boundaries()
{
    const auto t0 = Clock::now();
    int failures = 0;

    ArenaAllocator arena(1024);
    const size_t initialCapacity = arena.capacity();

    // 1) 超过当前容量：扩容并返回有效指针（不是失败）
    void* big = arena.alloc(4096, 64);
    if (big == nullptr || arena.capacity() < 4096 || (reinterpret_cast<uintptr_t>(big) % 64) != 0)
        ++failures;

    // 2) 非法对齐：0 / 非 2 次幂必须拒绝
    if (arena.alloc(32, 0) != nullptr)
        ++failures;
    if (arena.alloc(32, 3) != nullptr)
        ++failures;

    // 3) 溢出保护：size 接近 SIZE_MAX 时拒绝而不是回绕
    if (arena.alloc(std::numeric_limits<size_t>::max()) != nullptr)
        ++failures;
    if (arena.alloc(std::numeric_limits<size_t>::max() - 8) != nullptr)
        ++failures;

    // 4) 容量内的各种对齐都必须满足
    for (size_t align : {size_t(1), size_t(8), size_t(16), size_t(64), size_t(256)})
    {
        void* p = arena.alloc(32, align);
        if (p == nullptr || (reinterpret_cast<uintptr_t>(p) % align) != 0)
            ++failures;
    }

    // 5) reset 后 used 清零、容量保留（不把已拥有内存还回去）
    arena.reset();
    if (arena.used() != 0 || arena.capacity() < initialCapacity)
        ++failures;
    if (arena.alloc(16) == nullptr)
        ++failures;

    report("arena_boundaries", failures == 0, msSince(t0),
           "capacity=" + std::to_string(arena.capacity()) + " failures=" + std::to_string(failures));
}

// ---------------------------------------------------------------- 84
// 粒子运行期改 maxParticles：扩容后能发射新粒子，缩容后不得继续增长、
// 也不得泄漏或留下非有限值。
static void stress_particles_resize_runtime()
{
    const auto t0 = Clock::now();
    int failures = 0;

    EmitterConfig cfg;
    cfg.maxParticles = 100;
    cfg.rate = 5000.0f;
    cfg.lifeMin = 1.0f;
    cfg.lifeMax = 2.0f;
    ParticleSystem ps(cfg);

    for (int i = 0; i < 30; ++i)
        ps.update(1.0f / 60.0f);
    const int grownBefore = ps.aliveCount();
    if (grownBefore > cfg.maxParticles || grownBefore <= 0)
        ++failures;

    // 扩容到 5000
    ps.config().maxParticles = 5000;
    for (int i = 0; i < 30; ++i)
        ps.update(1.0f / 60.0f);
    const int grownAfter = ps.aliveCount();
    if (grownAfter <= grownBefore)
        ++failures; // 扩容必须真的多发射

    // 缩容到 50：存活数必须被压到上限附近，且后续不再增长
    ps.config().maxParticles = 50;
    ps.update(1.0f / 60.0f);
    const int shrunk = ps.aliveCount();
    for (int i = 0; i < 10; ++i)
        ps.update(1.0f / 60.0f);
    if (ps.aliveCount() > 50 * 2) // 允许一帧内的在途发射
        ++failures;

    for (const Particle* p : ps.getAlive())
        if (!std::isfinite(p->position.x) || !std::isfinite(p->life) || !std::isfinite(p->size))
            ++failures;

    report("particles_resize_runtime", failures == 0, msSince(t0),
           "before=" + std::to_string(grownBefore) + " grown=" + std::to_string(grownAfter) + " shrunk=" +
               std::to_string(shrunk) + " failures=" + std::to_string(failures));
}

// ================================================================
// 第七批：大场景导入 / draw call 固定开销 / 纹理边缘寻址
// ================================================================

// ---------------------------------------------------------------- 85
// 大场景 glTF：500 个 mesh + 500 个 node。
// 验证：每个 primitive 都展平为独立 Mesh、节点映射不错位、
// 以及转换阶段没有随节点数平方增长的隐藏循环。
static void stress_gltf_large_scene()
{
    const auto t0 = Clock::now();
    const std::string dir = "stress-out";
    std::filesystem::create_directories(dir);

    const int count = 500;
    std::vector<float> positions;
    positions.reserve(static_cast<size_t>(count) * 9);
    for (int i = 0; i < count; ++i)
    {
        // 每个 mesh 一个独立三角形，坐标逐网格偏移（便于发现错位）
        const float x = float(i);
        positions.insert(positions.end(), {x, 0.0f, 0.0f, x + 1.0f, 0.0f, 0.0f, x, 1.0f, 0.0f});
    }
    const bool binOk = writeFileBytes(dir + "/large.bin", positions.data(), positions.size() * sizeof(float));

    std::string json;
    json.reserve(static_cast<size_t>(count) * 260);
    char line[192];
    json += "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"uri\":\"large.bin\",\"byteLength\":";
    json += std::to_string(count * 36);
    json += "}],\"bufferViews\":[";
    for (int i = 0; i < count; ++i)
    {
        std::snprintf(line, sizeof(line), "%s{\"buffer\":0,\"byteOffset\":%d,\"byteLength\":36}",
                      i == 0 ? "" : ",", i * 36);
        json += line;
    }
    json += "],\"accessors\":[";
    for (int i = 0; i < count; ++i)
    {
        std::snprintf(line, sizeof(line),
                      "%s{\"bufferView\":%d,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\","
                      "\"min\":[0,0,0],\"max\":[600,1,0]}",
                      i == 0 ? "" : ",", i);
        json += line;
    }
    json += "],\"meshes\":[";
    for (int i = 0; i < count; ++i)
    {
        std::snprintf(line, sizeof(line), "%s{\"primitives\":[{\"attributes\":{\"POSITION\":%d}}]}",
                      i == 0 ? "" : ",", i);
        json += line;
    }
    json += "],\"nodes\":[";
    for (int i = 0; i < count; ++i)
    {
        std::snprintf(line, sizeof(line), "%s{\"mesh\":%d}", i == 0 ? "" : ",", i);
        json += line;
    }
    json += "],\"scenes\":[{\"nodes\":[";
    for (int i = 0; i < count; ++i)
    {
        std::snprintf(line, sizeof(line), "%s%d", i == 0 ? "" : ",", i);
        json += line;
    }
    json += "]}],\"scene\":0}";
    const bool jsonOk = writeFileText(dir + "/large.gltf", json);

    const auto tLoad = Clock::now();
    const auto loaded = sr::io::loadGltfScene(dir + "/large.gltf");
    const double loadMs = msSince(tLoad);

    int mismatchedNodes = 0;
    for (int i = 0; i < count && loaded.success; ++i)
    {
        if (size_t(i) >= loaded.scene.nodes.size())
        {
            ++mismatchedNodes;
            continue;
        }
        const SceneNode& node = loaded.scene.nodes[size_t(i)];
        if (node.meshIndices.size() != 1 || node.meshIndices.front() != i)
            ++mismatchedNodes;
        if (size_t(i) < loaded.scene.meshes.size() && loaded.scene.meshes[size_t(i)].vertices.size() != 3)
            ++mismatchedNodes;
    }

    const bool ok = binOk && jsonOk && loaded.success && loaded.scene.meshes.size() == size_t(count) &&
                    loaded.scene.nodes.size() == size_t(count) && mismatchedNodes == 0;
    report("gltf_large_scene", ok, msSince(t0),
           loaded.success ? "meshes=" + std::to_string(loaded.scene.meshes.size()) + " load=" +
                                std::to_string(int(loadMs)) + "ms mismatched=" + std::to_string(mismatchedNodes)
                          : "load failed: " + loaded.error);
}

// ---------------------------------------------------------------- 86
// draw call 固定开销：每帧 clear + 一次极小三角形，重复 2000 次。
// 真实场景里小物件多，这个数字决定“一个 draw 值多少钱”。
static void stress_draw_call_overhead()
{
    const auto t0 = Clock::now();
    const int W = 64, H = 64;
    Framebuffer fb(W, H, Color(0, 0, 0));
    Renderer renderer(W, H);
    FlatColorShader shader;
    shader.mvp = Mat4::identity();
    shader.color = {1, 1, 1};

    Mesh tri;
    tri.vertices.push_back({{-0.05f, -0.05f, 0.0f}, {0, 0, 1}, {0.0f, 0.0f}});
    tri.vertices.push_back({{0.05f, -0.05f, 0.0f}, {0, 0, 1}, {1.0f, 0.0f}});
    tri.vertices.push_back({{0.0f, 0.05f, 0.0f}, {0, 0, 1}, {0.5f, 1.0f}});
    tri.indices = {0, 1, 2};

    const int draws = 2000;
    const auto tDraws = Clock::now();
    for (int i = 0; i < draws; ++i)
    {
        renderer.clear(fb, Color(0, 0, 0));
        renderer.drawMesh(fb, tri, shader);
    }
    const double ms = msSince(tDraws);
    const double usPerDraw = ms * 1000.0 / double(draws);
    report("draw_call_overhead", usPerDraw < 200.0, msSince(t0),
           std::to_string(draws) + " draws " + std::to_string(usPerDraw) + " us/draw(含 clear)");
}

// ---------------------------------------------------------------- 87
// 纹理边缘寻址：放大 2×2 四色纹理。最右列必须仍以右边缘颜色为主。
// 旧实现用 i/(w-1) 取 UV，最后一列得到 u=1 → Repeat 绕回左侧，
// 图像右边会凭空出现左边缘的颜色（整列串色）。
static void stress_texture_atlas_edges()
{
    const auto t0 = Clock::now();
    sr::Image img;
    img.width = 2;
    img.height = 2;
    // 行 0（底）：左红、右绿；行 1（顶）：左蓝、右白
    img.pixels = {Color(255, 0, 0), Color(0, 255, 0), Color(0, 0, 255), Color(255, 255, 255)};
    const Texture tex(img);

    const int W = 8, H = 8;
    Framebuffer fb(W, H, Color(0, 0, 0));
    sr::draw2d::blitTexture(fb, tex, 0, 0, W, H);

    const Color leftBottom = fb.getPixel(0, 0);
    const Color rightBottom = fb.getPixel(W - 1, 0);
    const Color leftTop = fb.getPixel(0, H - 1);
    const Color rightTop = fb.getPixel(W - 1, H - 1);

    // 四角必须是各自的颜色：左列不被右边缘污染，反之亦然。
    // 2×2 放大到 8×8 时，ClampToEdge 下四角应近乎纯色（允许 1 px 级的双线性过度）。
    const bool ok = leftBottom.r > 200 && leftBottom.g < 60 && leftBottom.b < 60 &&
                    rightBottom.g > 200 && rightBottom.r < 60 && rightBottom.b < 60 &&
                    leftTop.b > 200 && leftTop.r < 60 && leftTop.g < 60 && rightTop.r > 200 &&
                    rightTop.g > 200 && rightTop.b > 200;
    report("texture_atlas_edges", ok, msSince(t0),
           "lb=" + std::to_string(leftBottom.r) + "/" + std::to_string(leftBottom.g) + " rb=" +
               std::to_string(rightBottom.r) + "/" + std::to_string(rightBottom.g) + " rt=" +
               std::to_string(rightTop.r) + "/" + std::to_string(rightTop.b));
}

// ---------------------------------------------------------------- 88
// SSAO 的合成输入：视图空间平面上立一根方柱，用真实透视投影，
// depth 由同一矩阵得出（投影与深度必须自洽，否则深度比较无意义）。
struct SsaoInputs
{
    std::vector<Vec3f> viewPos;
    std::vector<Vec3f> viewNormal;
    std::vector<float> depth;
    Mat4 proj = Mat4::identity();
};

static SsaoInputs makeSsaoInputs(int W, int H)
{
    SsaoInputs in;
    in.proj = sr::transform::perspective(1.1f, float(W) / float(H), 0.1f, 100.0f);
    const size_t count = size_t(W) * size_t(H);
    in.viewPos.resize(count);
    in.viewNormal.assign(count, Vec3f{0, 0, 1});
    in.depth.resize(count);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            const size_t i = size_t(y) * size_t(W) + size_t(x);
            const bool pillar = std::abs(x - W / 2) < W / 10 && std::abs(y - H / 2) < H / 10;
            const float z = pillar ? -1.0f : -2.0f;
            const Vec3f pos{float(x - W / 2) * 0.005f, float(y - H / 2) * 0.005f, z};
            in.viewPos[i] = pos;
            const Vec4f clip = in.proj * Vec4f(pos, 1.0f);
            in.depth[i] = clip.z / clip.w * 0.5f + 0.5f;
        }
    return in;
}

// SSAO 1080p 成本：每像素 16 次半球采样 + 投影，是后处理里最重的一项。
// 同时验证输出 AO 在 [0,1]、无 NaN，且柱边确实产生了遮蔽（否则测的是空跑）。
static void stress_ssao_1080p()
{
    const int W = 1920, H = 1080;
    const auto t0 = Clock::now();
    SsaoInputs in = makeSsaoInputs(W, H);

    sr::postprocess::SSAO ssao;
    sr::postprocess::SSAOConfig cfg;
    cfg.sampleCount = 16;
    ssao.configure(cfg);

    std::vector<float> ao;
    const auto tCompute = Clock::now();
    ssao.compute(in.viewPos, in.viewNormal, in.depth, W, H, in.proj, ao);
    const double ms = msSince(tCompute);

    int nonfinite = 0;
    int outOfRange = 0;
    float minAo = 1.0f;
    for (float value : ao)
    {
        if (!std::isfinite(value))
            ++nonfinite;
        else if (value < 0.0f || value > 1.0f)
            ++outOfRange;
        else
            minAo = std::min(minAo, value);
    }
    // 柱边应出现可观测的遮蔽（minAo 明显低于 1）
    const bool ok = ao.size() == size_t(W) * size_t(H) && nonfinite == 0 && outOfRange == 0 && minAo < 0.95f;
    report("ssao_1080p", ok, msSince(t0), "compute=" + std::to_string(int(ms)) + "ms minAo=" +
                                                  std::to_string(minAo) + " nonfinite=" +
                                                  std::to_string(nonfinite));
}

// ---------------------------------------------------------------- 89
// 半分辨率 SSAO：样本数降到 1/4，上采样回全分辨率。
// 除了测速度，也把半分辨率结果与全分辨率对比（平均偏差应在几个 AO 级别内）。
static void stress_ssao_half_res_1080p()
{
    const int W = 1920, H = 1080;
    const auto t0 = Clock::now();
    SsaoInputs in = makeSsaoInputs(W, H);

    sr::postprocess::SSAO ssao;
    sr::postprocess::SSAOConfig cfg;
    cfg.sampleCount = 16;
    ssao.configure(cfg);

    std::vector<float> halfAo;
    const auto tHalf = Clock::now();
    ssao.computeHalfResolution(in.viewPos, in.viewNormal, in.depth, W, H, in.proj, halfAo);
    const double halfMs = msSince(tHalf);

    std::vector<float> fullAo;
    const auto tFull = Clock::now();
    ssao.compute(in.viewPos, in.viewNormal, in.depth, W, H, in.proj, fullAo);
    const double fullMs = msSince(tFull);

    double diff = 0.0;
    int nonfinite = 0;
    for (size_t i = 0; i < halfAo.size() && i < fullAo.size(); ++i)
    {
        if (!std::isfinite(halfAo[i]) || !std::isfinite(fullAo[i]))
        {
            ++nonfinite;
            continue;
        }
        diff += std::abs(double(halfAo[i]) - double(fullAo[i]));
    }
    const double avgDiff = halfAo.empty() ? 1.0 : diff / double(halfAo.size());
    const bool ok = halfAo.size() == fullAo.size() && nonfinite == 0 && avgDiff < 0.05 &&
                    halfMs < fullMs * 0.6;
    report("ssao_half_res_1080p", ok, msSince(t0),
           "half=" + std::to_string(int(halfMs)) + "ms full=" + std::to_string(int(fullMs)) +
               "ms avgDiff=" + std::to_string(avgDiff));
}

// ================================================================
// 第八批：帧间状态隔离 / 图像格式往返 / 阴影贴图成本
// ================================================================

// ---------------------------------------------------------------- 90
// Renderer 帧间与实例间隔离：
//   1) clear() 必须同时重置深度，否则第二帧的远端几何会被上一帧遮挡；
//   2) 两个 Renderer 各自持有深度缓冲，互不影响。
static void stress_renderer_frame_isolation()
{
    const auto t0 = Clock::now();
    const int W = 32, H = 32;
    int failures = 0;

    FlatColorShader shader;
    shader.mvp = Mat4::identity();

    auto makeQuad = [](float z) {
        Mesh mesh;
        mesh.vertices.push_back({{-0.8f, -0.8f, z}, {0, 0, 1}, {0.0f, 0.0f}});
        mesh.vertices.push_back({{0.8f, -0.8f, z}, {0, 0, 1}, {1.0f, 0.0f}});
        mesh.vertices.push_back({{0.8f, 0.8f, z}, {0, 0, 1}, {1.0f, 1.0f}});
        mesh.vertices.push_back({{-0.8f, 0.8f, z}, {0, 0, 1}, {0.0f, 1.0f}});
        mesh.indices = {0, 1, 2, 0, 2, 3};
        return mesh;
    };
    const Mesh nearQuad = makeQuad(0.2f);
    const Mesh farQuad = makeQuad(0.8f);

    Framebuffer fb(W, H, Color(0, 0, 0));
    Renderer renderer(W, H);
    renderer.cullBackface = false;

    // 1) 先画近，再 clear，再画远：远的必须可见
    renderer.clear(fb, Color(0, 0, 0));
    shader.color = {0.8f, 0.0f, 0.0f};
    renderer.drawMesh(fb, nearQuad, shader);
    if (fb.getPixel(W / 2, H / 2).r < 150)
        ++failures;

    renderer.clear(fb, Color(0, 0, 0));
    shader.color = {0.0f, 0.8f, 0.0f};
    renderer.drawMesh(fb, farQuad, shader);
    if (fb.getPixel(W / 2, H / 2).g < 150)
        ++failures; // 深度没被 clear 重置

    // 2) 两个 Renderer 的深度互不干涉
    Framebuffer other(W, H, Color(0, 0, 0));
    Renderer otherRenderer(W, H);
    otherRenderer.cullBackface = false;
    otherRenderer.clear(other, Color(0, 0, 0));
    shader.color = {0.0f, 0.0f, 0.8f};
    otherRenderer.drawMesh(other, farQuad, shader);

    renderer.clear(fb, Color(0, 0, 0));
    shader.color = {0.8f, 0.8f, 0.0f};
    renderer.drawMesh(fb, nearQuad, shader);
    const Color mine = fb.getPixel(W / 2, H / 2);
    if (mine.r < 150 || mine.g < 150 || mine.b > 40)
        ++failures;

    report("renderer_frame_isolation", failures == 0, msSince(t0), "failures=" + std::to_string(failures));
}

// ---------------------------------------------------------------- 91
// 图像格式往返：PNG/BMP/TGA 必须无损（含 alpha 写不写随格式），
// JPG 有损但尺寸与主色必须对；不可写路径必须返回 false。
static void stress_image_formats_roundtrip()
{
    const auto t0 = Clock::now();
    const std::string dir = "stress-out";
    std::filesystem::create_directories(dir);
    int failures = 0;

    // 64×64（左半红、右半绿）：色块要足够大，否则 JPEG 的 8×8 DCT 会把小图整体糊掉，
    // 那是格式特性而不是编码器缺陷。
    Framebuffer fb(64, 64, Color(20, 40, 60, 255));
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            fb.setPixel(x, y, x < 32 ? Color(255, 0, 0, 255) : Color(0, 255, 0, 255));

    struct Case
    {
        const char* ext;
        bool lossless;
    };
    std::string failedFormats;
    for (const Case& c : {Case{"png", true}, Case{"bmp", true}, Case{"tga", true}, Case{"jpg", false}})
    {
        const std::string path = dir + "/roundtrip." + c.ext;
        const bool wrote = sr::io::writeImage(path, fb);
        sr::Image img;
        const bool loaded = wrote && sr::io::loadImage(path, img) && img.width == 64 && img.height == 64;
        bool colorsOk = false;
        if (loaded)
        {
            if (c.lossless)
            {
                colorsOk = img.at(8, 8).r == 255 && img.at(56, 56).g == 255;
            }
            else
            {
                // JPG 有损 + 色度子采样：只用大色块中心的均值判断主色
                int sumLeftR = 0, sumRightG = 0;
                for (int y = 24; y < 40; ++y)
                {
                    for (int x = 8; x < 24; ++x)
                        sumLeftR += img.at(x, y).r;
                    for (int x = 40; x < 56; ++x)
                        sumRightG += img.at(x, y).g;
                }
                colorsOk = (sumLeftR / 256) > 200 && (sumRightG / 256) > 200;
            }
        }
        if (!wrote || !loaded || !colorsOk)
        {
            ++failures;
            failedFormats += std::string(c.ext) + (wrote ? "" : "(write)") + (loaded ? "" : "(load)") +
                             (colorsOk ? "" : "(color)") + " ";
        }
    }

    // 不可写路径（目录不存在）
    if (sr::io::writeImage(dir + "/no_such_dir/x.png", fb))
    {
        ++failures;
        failedFormats += "badpath ";
    }

    report("image_formats_roundtrip", failures == 0, msSince(t0),
           failedFormats.empty() ? "all ok" : failedFormats);
}

// ---------------------------------------------------------------- 92
// 阴影贴图成本：1024² 深度图渲染 + 512² 全屏 3×3 PCF。
// 这是阴影开启时每帧的固定开销，也是 DepthBuffer 读写的实测。
static void stress_shadow_map_pcf()
{
    const auto t0 = Clock::now();
    const int mapSize = 1024;
    const int screen = 512;

    const Mat4 lightProj = sr::transform::ortho(-2.0f, 2.0f, -2.0f, 2.0f, 0.1f, 10.0f);
    const Mat4 lightView = sr::transform::lookAt(Vec3f{3, 5, 4}, Vec3f{0, 0, 0}, Vec3f{0, 1, 0});
    const Mat4 lightMVP = lightProj * lightView;

    FlatColorShader depthShader;
    depthShader.mvp = lightMVP;

    Framebuffer shadowColor(mapSize, mapSize, Color(255, 255, 255));
    Renderer shadowRenderer(mapSize, mapSize);
    shadowRenderer.clear(shadowColor, Color(255, 255, 255));

    const Mesh sphere = sr::meshgen::sphere(0.8f, 24, 16);
    const auto tMap = Clock::now();
    for (int i = 0; i < 4; ++i)
    {
        Mesh moved = sphere;
        const float offset = float(i - 1) * 0.8f;
        for (Vertex& v : moved.vertices)
        {
            v.position.x += offset;
            v.position.z += (i % 2 == 0) ? 0.3f : -0.3f;
        }
        shadowRenderer.drawMesh(shadowColor, moved, depthShader);
    }
    const double mapMs = msSince(tMap);

    DepthBuffer& shadowMap = shadowRenderer.depth();
    const auto tPcf = Clock::now();
    double lit = 0.0;
    for (int y = 0; y < screen; ++y)
    {
        for (int x = 0; x < screen; ++x)
        {
            const Vec3f world{float(x - screen / 2) * 0.008f, float(y - screen / 2) * 0.008f, 0.0f};
            const Vec4f clip = lightMVP * Vec4f(world, 1.0f);
            const int lx = int((clip.x * 0.5f + 0.5f) * float(mapSize));
            const int ly = int((clip.y * 0.5f + 0.5f) * float(mapSize));
            const float bias = clip.z * 0.5f + 0.5f - 0.002f;
            int visible = 0;
            int tested = 0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                {
                    const int sx = lx + dx, sy = ly + dy;
                    if (sx < 0 || sx >= mapSize || sy < 0 || sy >= mapSize)
                        continue;
                    ++tested;
                    if (bias <= shadowMap.row(sy)[sx])
                        ++visible;
                }
            if (tested > 0)
                lit += double(visible) / double(tested);
        }
    }
    const double pcfMs = msSince(tPcf);

    // 场景里球体只遮住部分区域，平均受光率应在 (0.3, 1) 之间
    const double average = lit / double(screen) / double(screen);
    const bool ok = average > 0.3 && average < 1.0;
    report("shadow_map_pcf", ok, msSince(t0),
           "map=" + std::to_string(int(mapMs)) + "ms pcf=" + std::to_string(int(pcfMs)) + "ms avgLit=" +
               std::to_string(average));
}

// ---------------------------------------------------------------- 93
// 异步任务与捕获资源的生命周期：
//   1) CommandBuffer 析构时未执行的命令必须释放它捕获的 shared_ptr；
//   2) ThreadPool 析构时未取的 future 也要把任务执行完；
//   3) future 里的异常没人取，不得让析构崩溃。
static void stress_async_lifecycle()
{
    const auto t0 = Clock::now();
    int failures = 0;

    // 1) 命令未执行就被丢弃
    {
        auto payload = std::make_shared<int>(7);
        {
            CommandBuffer buffer;
            buffer.record([payload] { (void)*payload; });
            if (payload.use_count() != 2)
                ++failures; // 命令列表应持有一份
        }
        if (payload.use_count() != 1)
            ++failures; // 析构后应释放
    }

    // 2) 命令执行完成后也应释放捕获
    {
        auto payload = std::make_shared<int>(9);
        {
            CommandBuffer buffer;
            buffer.record([payload] { (void)*payload; });
            buffer.executeSequential();
            if (payload.use_count() != 2)
                ++failures; // 执行不会消费命令
        }
        if (payload.use_count() != 1)
            ++failures;
    }

    // 3) future 未取 + 异常未取，池析构仍要把任务跑完且不崩
    std::atomic<int> finished{0};
    {
        ThreadPool pool(2);
        for (int i = 0; i < 100; ++i)
            (void)pool.submitFuture([&finished]() -> int {
                finished.fetch_add(1, std::memory_order_relaxed);
                return 1;
            });
        (void)pool.submitFuture([]() -> int { throw std::runtime_error("unobserved"); });
    }
    if (finished.load() != 100)
        ++failures;

    report("async_lifecycle", failures == 0, msSince(t0), "failures=" + std::to_string(failures));
}

// ================================================================
// 第九批：动画 → 蒙皮 → 渲染端到端
// ================================================================

// ---------------------------------------------------------------- 94
// 把采样、骨骼全局矩阵、逐顶点蒙皮、光栅化串成一条链。
// 分段测试无法发现接缝问题（例如 pose 的 nodeIndex 与骨骼数组不对应、
// 蒙皮矩阵顺序反了、权重未归一化），这里一次性验证。
static void stress_animation_skinning_pipeline()
{
    const auto t0 = Clock::now();
    int failures = 0;

    // 1) 三根骨骼的链：0(root) → 1 → 2
    Skeleton skeleton;
    skeleton.bones.resize(3);
    for (int i = 0; i < 3; ++i)
    {
        skeleton.bones[size_t(i)].parent = i - 1;
        skeleton.bones[size_t(i)].localTransform = Mat4::identity();
        skeleton.bones[size_t(i)].inverseBindMatrix = Mat4::identity();
    }

    // 2) 动画：每根骨骼绕 Z 从 0° 转到 90°
    const Quat start = Quat::identity();
    const Quat end = Quat::fromAxisAngle(Vec3f{0, 0, 1}, 1.5707963f);
    AnimationClip clip;
    for (int i = 0; i < 3; ++i)
    {
        AnimationSampler sampler;
        sampler.interpolation = AnimationSampler::Interpolation::Linear;
        sampler.times = {0.0f, 1.0f};
        sampler.values = {Vec4f{start.x, start.y, start.z, start.w}, Vec4f{end.x, end.y, end.z, end.w}};
        clip.samplers.push_back(sampler);
        AnimationChannel channel;
        channel.samplerIndex = i;
        channel.targetNode = i;
        channel.path = AnimationChannel::Path::Rotation;
        clip.channels.push_back(channel);
    }
    clip.validated = true;

    // 3) 一条沿 Y 的条带，权重从底部骨骼渐变到顶部骨骼
    Mesh mesh;
    std::vector<VertexBoneWeights> weights;
    const int segments = 6;
    for (int i = 0; i <= segments; ++i)
    {
        const float y = float(i) / float(segments) * 1.0f;
        for (int side = 0; side < 2; ++side)
        {
            Vertex v;
            v.position = {side == 0 ? -0.15f : 0.15f, y, 0.0f};
            v.normal = {0, 0, 1};
            v.uv = {float(side), y};
            mesh.vertices.push_back(v);

            VertexBoneWeights bw;
            if (y < 0.5f)
            {
                bw.boneIndices[0] = 0;
                bw.weights[0] = 1.0f - y;
                bw.boneIndices[1] = 1;
                bw.weights[1] = y;
            }
            else
            {
                bw.boneIndices[0] = 1;
                bw.weights[0] = 1.0f - (y - 0.5f);
                bw.boneIndices[1] = 2;
                bw.weights[1] = y - 0.5f;
            }
            weights.push_back(bw);
        }
    }
    for (int i = 0; i < segments; ++i)
    {
        const int a = i * 2, b = a + 1, c = a + 2, d = a + 3;
        mesh.indices.insert(mesh.indices.end(), {a, b, c, b, d, c});
    }

    // 4) 每个时间点：采样 → 写回骨骼 → 蒙皮 → 渲染
    const int W = 64, H = 64;
    auto renderAt = [&](float time, Framebuffer& fb, uint64_t& hash) {
        std::vector<SceneNode> nodes(3);
        const auto poses = sr::animation::sampleAnimation(nodes, clip, time);
        for (const sr::animation::NodePose& pose : poses)
        {
            if (pose.nodeIndex < 0 || size_t(pose.nodeIndex) >= skeleton.bones.size() || !pose.hasRotation)
                continue;
            skeleton.bones[size_t(pose.nodeIndex)].localTransform = pose.rotation.toMat4();
        }
        const auto skin = skeleton.computeSkinMatrices();
        if (skin.size() != 3)
        {
            ++failures;
            return;
        }

        Mesh deformed = mesh;
        for (size_t i = 0; i < deformed.vertices.size(); ++i)
        {
            const Vec3f p = skinPosition(mesh.vertices[i].position, weights[i], skin);
            deformed.vertices[i].position = p;
            deformed.vertices[i].normal = skinNormal(mesh.vertices[i].normal, weights[i], skin).normalizedOr({0, 0, 1});
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
                ++failures;
        }

        Renderer renderer(W, H);
        renderer.cullBackface = false;
        renderer.clear(fb, Color(0, 0, 0));
        FlatColorShader shader;
        Camera cam;
        cam.position = {0, 0.5f, 3.0f};
        cam.aspect = 1.0f;
        shader.mvp = cam.projMatrix() * cam.viewMatrix();
        shader.color = {1, 1, 1};
        renderer.drawMesh(fb, deformed, shader);

        hash = 1469598103934665603ull;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
            {
                const Color c = fb.getPixel(x, y);
                hash = (hash ^ c.r) * 1099511628211ull;
            }
    };

    Framebuffer fbA(W, H, Color(0, 0, 0));
    Framebuffer fbB(W, H, Color(0, 0, 0));
    uint64_t hashA = 0, hashB = 0;
    renderAt(0.0f, fbA, hashA);
    renderAt(1.0f, fbB, hashB);

    // 动画必须真的改变画面；且两个时间点都画出了东西
    int litA = 0, litB = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
        {
            if (fbA.getPixel(x, y).r > 0)
                ++litA;
            if (fbB.getPixel(x, y).r > 0)
                ++litB;
        }
    const bool ok = failures == 0 && litA > 50 && litB > 50 && hashA != hashB;
    report("animation_skinning_pipeline", ok, msSince(t0),
           "lit=" + std::to_string(litA) + "/" + std::to_string(litB) + " distinct=" +
               std::to_string(hashA != hashB) + " failures=" + std::to_string(failures));
}

// ---------------------------------------------------------------- 95
// Cubemap 面映射：六个面各涂一种颜色，五个轴向与面内四个象限
// 必须采样到对应的面。faceFor 的面选择或 u/v 翻转写错都会在这里暴露。
static void stress_cubemap_face_mapping()
{
    const auto t0 = Clock::now();
    int failures = 0;

    sr::Image faces[6];
    for (int f = 0; f < 6; ++f)
    {
        faces[f].width = 16;
        faces[f].height = 16;
        // 每面一个可区分的单色
        const Color color = Color(uint8_t(30 + f * 35), uint8_t(200 - f * 25), uint8_t(60 + f * 20));
        faces[f].pixels.assign(16 * 16, color);
    }
    Cubemap cube;
    cube.setFaces(faces);
    if (!cube.valid())
    {
        report("cubemap_face_mapping", false, msSince(t0), "invalid cubemap");
        return;
    }

    // 面顺序：PosX, NegX, PosY, NegY, PosZ, NegZ
    const Vec3f axes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int f = 0; f < 6; ++f)
    {
        const Color expected = faces[f].pixels[0];
        const Color sampled = Color::fromFloat(cube.sample(axes[f]).x, cube.sample(axes[f]).y,
                                               cube.sample(axes[f]).z);
        const int delta = std::abs(int(sampled.r) - int(expected.r)) + std::abs(int(sampled.g) - int(expected.g)) +
                          std::abs(int(sampled.b) - int(expected.b));
        if (delta > 6)
            ++failures;

        // 面内四个象限（略偏离中心）仍应属于同一面
        const Vec3f n = axes[f];
        const Vec3f helpers[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        Vec3f u = cross(n, std::abs(n.y) > 0.5f ? Vec3f{1, 0, 0} : Vec3f{0, 1, 0}).normalizedOr({1, 0, 0});
        Vec3f v = cross(n, u).normalizedOr({0, 1, 0});
        (void)helpers;
        for (const float su : {-0.6f, 0.6f})
            for (const float sv : {-0.6f, 0.6f})
            {
                const Vec3f dir = (n + u * su + v * sv).normalizedOr(n);
                const Vec3f c = cube.sample(dir);
                const Color got = Color::fromFloat(c.x, c.y, c.z);
                const int d2 = std::abs(int(got.r) - int(expected.r)) + std::abs(int(got.g) - int(expected.g)) +
                               std::abs(int(got.b) - int(expected.b));
                if (d2 > 24)
                    ++failures;
            }
    }

    // 天空生成公式：+Y 取天顶色、水平方向取地平线色
    Cubemap sky;
    generateSkyCubemap(sky, 64, Vec3f{0.0f, 0.0f, 1.0f}, Vec3f{1.0f, 0.0f, 0.0f});
    const Vec3f up = sky.sample({0, 1, 0});
    const Vec3f side = sky.sample({1, 0, 0});
    if (!(up.z > 0.9f && up.x < 0.1f))
        ++failures;
    if (!(side.x > 0.9f && side.z < 0.1f))
        ++failures;

    report("cubemap_face_mapping", failures == 0, msSince(t0), "failures=" + std::to_string(failures));
}

// ---------------------------------------------------------------- 96
// Renderer2D 状态隔离：begin() 必须清掉上一帧的 scissor，
// 否则第二帧的图元会被上一帧的裁剪区默默截断。
// 同时验证空栈 pop / 重复 end 不会崩。
static void stress_renderer2d_state_reset()
{
    const auto t0 = Clock::now();
    const int W = 32, H = 32;
    Framebuffer fb(W, H, Color(0, 0, 0));
    Renderer2D r;

    auto countLit = [&fb] {
        int lit = 0;
        for (int y = 0; y < fb.height(); ++y)
            for (int x = 0; x < fb.width(); ++x)
                if (fb.getPixel(x, y).r > 0)
                    ++lit;
        return lit;
    };

    // 1) 限定 4×4 的 scissor
    r.begin(fb);
    r.setScissor(Recti(0, 0, 4, 4));
    r.setBlend(BlendMode::Opaque);
    r.setColor(sr::colors::white);
    r.fillRect(0.0f, 0.0f, float(W), float(H));
    const int clipped = countLit();
    r.end();

    // 2) 同一个渲染器再次 begin：scissor 必须已清除
    fb.clear(Color(0, 0, 0));
    r.begin(fb);
    r.fillRect(0.0f, 0.0f, float(W), float(H));
    const int full = countLit();
    r.end();

    // 3) 空栈 pop、重复 end 不得崩
    r.pop();
    r.pop();
    r.end();

    const bool ok = clipped == 16 && full == W * H;
    report("renderer2d_state_reset", ok, msSince(t0),
           "clipped=" + std::to_string(clipped) + " full=" + std::to_string(full) + "/" +
               std::to_string(W * H));
}

// ---------------------------------------------------------------- 97
// glTF 节点层级：200 级链式 children，每个节点平移 0.01。
// 验证 convertModel 对 children 的映射，以及沿链累乘的世界矩阵。
static void stress_gltf_node_hierarchy()
{
    const auto t0 = Clock::now();
    const std::string dir = "stress-out";
    std::filesystem::create_directories(dir);

    // 只需一个三角形承载 mesh（最后一个节点引用它）
    const std::vector<float> positions = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    const bool binOk = writeFileBytes(dir + "/chain.bin", positions.data(), positions.size() * sizeof(float));

    const int depth = 200;
    std::string json;
    json.reserve(static_cast<size_t>(depth) * 64);
    char line[160];
    json += "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"uri\":\"chain.bin\",\"byteLength\":36}],";
    json += "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36}],";
    json += "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\","
            "\"min\":[0,0,0],\"max\":[1,1,0]}],";
    json += "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0}}]}],";
    json += "\"nodes\":[";
    for (int i = 0; i < depth; ++i)
    {
        if (i + 1 < depth)
            std::snprintf(line, sizeof(line), "%s{\"children\":[%d],\"translation\":[0.01,0,0]}",
                          i == 0 ? "" : ",", i + 1);
        else
            std::snprintf(line, sizeof(line), "%s{\"mesh\":0,\"translation\":[0.01,0,0]}",
                          i == 0 ? "" : ",");
        json += line;
    }
    json += "],\"scenes\":[{\"nodes\":[0]}],\"scene\":0}";
    const bool jsonOk = writeFileText(dir + "/chain.gltf", json);

    const auto loaded = sr::io::loadGltfScene(dir + "/chain.gltf");
    int failures = 0;
    if (!(binOk && jsonOk && loaded.success) || loaded.scene.nodes.size() != size_t(depth))
    {
        report("gltf_node_hierarchy", false, msSince(t0),
               loaded.success ? "nodes=" + std::to_string(loaded.scene.nodes.size()) : loaded.error);
        return;
    }

    // children 映射必须逐层对应
    for (int i = 0; i + 1 < depth; ++i)
    {
        const SceneNode& node = loaded.scene.nodes[size_t(i)];
        if (node.children.size() != 1 || node.children.front() != i + 1)
            ++failures;
    }
    if (!loaded.scene.nodes[size_t(depth - 1)].children.empty())
        ++failures;
    if (loaded.scene.nodes[size_t(depth - 1)].meshIndex != 0)
        ++failures;

    // 沿链累乘世界矩阵（迭代式，避免深递归）
    const auto& nodes = loaded.scene.nodes;
    std::vector<Mat4> world(nodes.size(), Mat4::identity());
    std::vector<Mat4> parentWorld(nodes.size(), Mat4::identity());
    std::vector<int> stack = {0};
    while (!stack.empty())
    {
        const int index = stack.back();
        stack.pop_back();
        world[size_t(index)] = parentWorld[size_t(index)] * nodes[size_t(index)].localMatrix();
        for (int child : nodes[size_t(index)].children)
        {
            if (child < 0 || size_t(child) >= nodes.size())
            {
                ++failures;
                continue;
            }
            parentWorld[size_t(child)] = world[size_t(index)];
            stack.push_back(child);
        }
    }

    const Vec3f leaf = world[size_t(depth - 1)].transformPoint({0, 0, 0});
    const float expected = float(depth) * 0.01f;
    if (!std::isfinite(leaf.x) || std::abs(leaf.x - expected) > 0.05f)
        ++failures;

    report("gltf_node_hierarchy", failures == 0, msSince(t0),
           "depth=" + std::to_string(depth) + " leafX=" + std::to_string(leaf.x) + " failures=" +
               std::to_string(failures));
}

// ---------------------------------------------------------------- 98
// 整帧预算：1080p 下“3D 渲染 + 2D UI + FXAA”三段各占多少。
// 单项数字好看不代表整帧能承受，这里是使用者真正感受到的耗时。
static void stress_frame_budget_1080p()
{
    const int W = 1920, H = 1080;
    const auto t0 = Clock::now();

    Framebuffer fb(W, H, Color(10, 12, 16));
    Renderer renderer(W, H);
    const Mesh sphere = sr::meshgen::sphere(1.0f, 48, 32);
    auto shader = makePbr(W, H);

    // 1) 3D：一帧一个 PBR 球（三角形数随细分上升）
    const auto t3d = Clock::now();
    renderer.clear(fb, Color(10, 12, 16));
    shader.model = Mat4::rotateY(0.4f);
    shader.mvp = shader.proj * shader.view * shader.model;
    shader.normalMatrix = shader.model.inversed().transposed();
    renderer.drawMesh(fb, sphere, shader);
    const double ms3d = msSince(t3d);

    // 2) 2D：20 个面板 + 5 条文本
    const auto t2d = Clock::now();
    Renderer2D ui;
    ui.begin(fb);
    ui.setBlend(BlendMode::AlphaBlend);
    for (int i = 0; i < 20; ++i)
    {
        ui.setColor(Color(uint8_t(40 + i * 8), 60, uint8_t(120 - i * 4), 180));
        ui.fillRect(20.0f + float(i % 5) * 200.0f, 20.0f + float(i / 5) * 120.0f, 180.0f, 100.0f);
    }
    ui.setColor(sr::colors::white);
    for (int i = 0; i < 5; ++i)
        ui.text(30.0f, 40.0f + float(i) * 24.0f, "FRAME BUDGET 1080p", 1.5f);
    ui.end();
    const double ms2d = msSince(t2d);

    // 3) 后处理：FXAA 全屏
    Framebuffer post(W, H);
    const auto tPost = Clock::now();
    sr::postprocess::fxaa(fb, post);
    const double msPost = msSince(tPost);

    const double total = ms3d + ms2d + msPost;
    // 这里不设绝对时间阈值：Debug/ASAN 下 FXAA 会慢 10 倍以上，而机器差异更大。
    // 性能回归交给 `--baseline`（同构建对比同构建），本场景只保证：
    // 三段都真的跑了、输出有效、图像非全黑。
    const Color center = post.getPixel(W / 2, H / 2);
    const bool ok = ms3d > 0.0 && ms2d > 0.0 && msPost > 0.0 && center.r + center.g + center.b > 0;
    report("frame_budget_1080p", ok, msSince(t0),
           "3d=" + std::to_string(int(ms3d)) + "ms 2d=" + std::to_string(int(ms2d)) + "ms post=" +
               std::to_string(int(msPost)) + "ms total=" + std::to_string(int(total)) + "ms tris=" +
               std::to_string(sphere.triangleCount()));
}

// 场景注册表
//
// 新增场景只需：写一个 static void stress_xxx() + 在本表加一行。
// id 同时用于 --filter 匹配与基线键；heavy 标记让 --quick 能在几秒内跑完。
// heavy 的判定标准：单独耗时 ≳ 100 ms（与硬件无关的粗分类）。
struct Scenario
{
    const char* id;
    void (*run)();
    bool heavy;
};

const Scenario kScenarios[] = {
    {"render_heavy", stress_render_heavy, false},
    {"render_hostile", stress_render_hostile, false},
    {"texture_extremes", stress_texture_extremes, false},
    {"texture_tiny", stress_texture_tiny, false},
    {"postprocess_edges", stress_postprocess_edges, false},
    {"render_queue", stress_render_queue, false},
    {"thread_pool", stress_thread_pool, true},
    {"skeleton_depth", stress_skeleton_depth, false},
    {"particles", stress_particles, false},
    {"profiler", stress_profiler, true},
    {"arena", stress_arena, false},
    {"draw2d", stress_draw2d, true},
    {"resource_cache", stress_resource_cache, false},
    {"msaa", stress_msaa, false},
    {"obj_load", stress_obj_load, true},
    {"texture_odd_sizes", stress_texture_odd_sizes, false},
    {"cubemap_edges", stress_cubemap_edges, false},
    {"camera_extremes", stress_camera_extremes, false},
    {"postprocess_cost", stress_postprocess_cost, false},
    {"texture_creation", stress_texture_creation, false},
    {"renderer_tiny_target", stress_renderer_tiny_target, false},
    {"deep_transform_stack", stress_deep_transform_stack, false},
    {"msaa8", stress_msaa8, false},
    {"nested_submit", stress_nested_submit, false},
    {"profiler_bad_path", stress_profiler_bad_path, false},
    {"gltf_corrupt", stress_gltf_corrupt, false},
    {"obj_hostile", stress_obj_hostile, false},
    {"image_corrupt", stress_image_corrupt, false},
    {"dof_1080p", stress_dof_1080p, false},
    {"profiler_overflow", stress_profiler_overflow, false},
    {"draw2d_states", stress_draw2d_states, false},
    {"math_singular", stress_math_singular, false},
    {"msaa_resolve_mismatch", stress_msaa_resolve_mismatch, false},
    {"thread_pool_many_threads", stress_thread_pool_many_threads, false},
    {"gltf_empty_model", stress_gltf_empty_model, false},
    {"soak", stress_soak, true},
    {"render_path_consistency", stress_render_path_consistency, false},
    {"renderer2d_rotated_texture", stress_renderer2d_rotated_texture, false},
    {"command_buffer", stress_command_buffer, false},
    {"animation", stress_animation, false},
    {"msaa_1080p", stress_msaa_1080p, true},
    {"renderer2d_transforms", stress_renderer2d_transforms, true},
    {"scene_asset_copy", stress_scene_asset_copy, false},
    {"render_4k", stress_render_4k, false},
    {"text_many", stress_text_many, false},
    {"tga_roundtrip", stress_tga_roundtrip, false},
    {"deferred_1080p", stress_deferred_1080p, true},
    {"nan_fragment_color", stress_nan_fragment_color, false},
    {"nonfinite_2d_api", stress_nonfinite_2d_api, false},
    {"gltf_multi_primitive", stress_gltf_multi_primitive, false},
    {"gltf_unsupported_mode", stress_gltf_unsupported_mode, false},
    {"skeleton_hierarchy", stress_skeleton_hierarchy, false},
    {"cubemap_roughness", stress_cubemap_roughness, false},
    {"resource_cache_concurrent", stress_resource_cache_concurrent, false},
    {"texture_concurrent_sample", stress_texture_concurrent_sample, false},
    {"profiler_concurrent", stress_profiler_concurrent, false},
    {"huge_triangle_coverage", stress_huge_triangle_coverage, false},
    {"degenerate_triangles", stress_degenerate_triangles, false},
    {"extreme_projection", stress_extreme_projection_matrix, false},
    {"tga_variants", stress_tga_variants, false},
    {"tga_oversized_header", stress_tga_oversized_header, false},
    {"tga_write_failures", stress_tga_write_failures, false},
    {"obj_long_line", stress_obj_long_line, false},
    {"bench_blit_throughput", stress_bench_blit_throughput, true},
    {"bench_fill_throughput", stress_bench_fill_throughput, true},
    {"bench_texture_sampling", stress_bench_texture_sampling, true},
    {"bench_raster_small_tris", stress_bench_raster_small_tris, false},
    {"depth_test_order", stress_depth_test_order, false},
    {"msaa_edge_coverage", stress_msaa_edge_coverage, false},
    {"pbr_extreme_material", stress_pbr_extreme_material, false},
    {"animation_malformed", stress_animation_malformed, false},
    {"particles_extreme", stress_particles_extreme, false},
    {"framebuffer_bounds", stress_framebuffer_bounds, false},
    {"thread_pool_drain", stress_thread_pool_drain, false},
    {"image_decode_throughput", stress_image_decode_throughput, false},
    {"scene_graph_update", stress_scene_graph_update, true},
    {"render_multithreaded", stress_render_multithreaded, true},
    {"postprocess_chain_1080p", stress_postprocess_chain_1080p, true},
    {"texture_lifecycle", stress_texture_lifecycle, true},
    {"profiler_disabled_overhead", stress_profiler_disabled_overhead, false},
    {"msaa_resolve_throughput", stress_msaa_resolve_throughput, true},
    {"renderer_invalid_mesh", stress_renderer_invalid_mesh, false},
    {"arena_boundaries", stress_arena_boundaries, false},
    {"particles_resize_runtime", stress_particles_resize_runtime, false},
    {"gltf_large_scene", stress_gltf_large_scene, false},
    {"draw_call_overhead", stress_draw_call_overhead, false},
    {"texture_atlas_edges", stress_texture_atlas_edges, false},
    {"ssao_1080p", stress_ssao_1080p, true},
    {"ssao_half_res_1080p", stress_ssao_half_res_1080p, true},
    {"renderer_frame_isolation", stress_renderer_frame_isolation, false},
    {"image_formats_roundtrip", stress_image_formats_roundtrip, false},
    {"shadow_map_pcf", stress_shadow_map_pcf, true},
    {"async_lifecycle", stress_async_lifecycle, false},
    {"animation_skinning_pipeline", stress_animation_skinning_pipeline, false},
    {"cubemap_face_mapping", stress_cubemap_face_mapping, false},
    {"renderer2d_state_reset", stress_renderer2d_state_reset, false},
    {"gltf_node_hierarchy", stress_gltf_node_hierarchy, false},
    {"frame_budget_1080p", stress_frame_budget_1080p, true},
};

constexpr size_t kScenarioCount = sizeof(kScenarios) / sizeof(kScenarios[0]);

// 基线文件就是上一轮 --json 的输出，每场景一行固定格式：
//   {"id":"x","name":"x","ok":true,"ms":12.34,"detail":"..."}
// 逐行取 name 与 ms，避免引入 JSON 解析依赖。
std::map<std::string, double> loadBaseline(const std::string& path)
{
    std::map<std::string, double> out;
    std::ifstream in(path);
    if (!in)
    {
        std::printf("[WARN] baseline not readable: %s\n", path.c_str());
        return out;
    }
    std::string line;
    while (std::getline(in, line))
    {
        const size_t nameKey = line.find("\"name\":\"");
        const size_t msKey = line.find("\"ms\":");
        if (nameKey == std::string::npos || msKey == std::string::npos)
            continue;
        const size_t nameBegin = nameKey + 8;
        const size_t nameEnd = line.find('"', nameBegin);
        if (nameEnd == std::string::npos)
            continue;
        try
        {
            out[line.substr(nameBegin, nameEnd - nameBegin)] = std::stod(line.substr(msKey + 5));
        }
        catch (const std::exception&)
        {
            // 格式异常的行直接忽略：基线只影响提示，不影响判定
        }
    }
    return out;
}

void writeJson(const std::string& path)
{
    std::ofstream out(path, std::ios::trunc);
    if (!out)
    {
        std::printf("[WARN] cannot write json: %s\n", path.c_str());
        return;
    }
    out << "{\n  \"scenarios\": [\n";
    for (size_t i = 0; i < g_results.size(); ++i)
    {
        const ScenarioResult& r = g_results[i];
        out << "    {\"id\":\"" << jsonEscape(r.id) << "\",\"name\":\"" << jsonEscape(r.name) << "\",\"ok\":"
            << (r.ok ? "true" : "false") << ",\"ms\":" << r.ms << ",\"detail\":\"" << jsonEscape(r.detail) << "\"}"
            << (i + 1 < g_results.size() ? ",\n" : "\n");
    }
    out << "  ],\n  \"failures\": " << g_failures << ",\n  \"regressions\": " << g_regressions << "\n}\n";
}

bool contains(const std::string& haystack, const std::string& needle)
{
    return needle.empty() || haystack.find(needle) != std::string::npos;
}

void printUsage()
{
    std::printf("usage: renderer_stress [options]\n");
    std::printf("  --list                 list scenario ids and exit\n");
    std::printf("  --filter=<substr>      run only scenarios whose id contains <substr> (see --list)\n");
    std::printf("  --quick                skip heavy scenarios (total well under a second per core)\n");
    std::printf("  --json=<path>          write machine-readable results\n");
    std::printf("  --baseline=<path>      compare against a previous --json output, print [SLOW] on regressions\n");
    std::printf("  --strict-baseline      make regressions fail the run (for CI on fixed hardware)\n");
}

int main(int argc, char** argv)
{
    std::string filter;
    std::string jsonPath;
    bool quick = false;
    bool list = false;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--list")
            list = true;
        else if (arg == "--quick")
            quick = true;
        else if (arg == "--strict-baseline")
            g_strictBaseline = true;
        else if (arg.rfind("--filter=", 0) == 0)
            filter = arg.substr(9);
        else if (arg.rfind("--json=", 0) == 0)
            jsonPath = arg.substr(7);
        else if (arg.rfind("--baseline=", 0) == 0)
            g_baseline = loadBaseline(arg.substr(11));
        else if (arg == "--help" || arg == "-h")
        {
            printUsage();
            return 0;
        }
        else
        {
            std::printf("[WARN] unknown argument: %s\n", arg.c_str());
            printUsage();
            return 2;
        }
    }

    if (list)
    {
        for (const Scenario& s : kScenarios)
            std::printf("%s%s\n", s.id, s.heavy ? "  (heavy)" : "");
        return 0;
    }

    std::printf("%-5s %-28s %12s  %s\n", "状态", "场景", "耗时", "度量");
    std::printf("--------------------------------------------------------------------------\n");

    size_t ran = 0;
    for (const Scenario& s : kScenarios)
    {
        if (quick && s.heavy)
            continue;
        if (!contains(s.id, filter))
            continue;
        g_currentScenario = s.id;
        // 单个场景抛异常（如 bad_alloc）不得终止整个探针：84 个场景共用
        // 一个进程，死在中间会让后面的场景完全不被覆盖。
        try
        {
            s.run();
        }
        catch (const std::exception& e)
        {
            report(s.id, false, 0.0, std::string("threw: ") + e.what());
        }
        catch (...)
        {
            report(s.id, false, 0.0, "threw: non-standard exception");
        }
        ++ran;
    }

    if (ran == 0)
    {
        std::printf("[WARN] no scenario matched filter '%s' (use --list)\n", filter.c_str());
        return 2;
    }

    std::printf("--------------------------------------------------------------------------\n");
    std::printf("failures=%d  regressions=%d  scenarios=%zu/%zu  total=%.1f s\n", g_failures, g_regressions, ran,
                kScenarioCount, msSince(g_start) / 1000.0);

    if (!jsonPath.empty())
    {
        writeJson(jsonPath);
        std::printf("json written: %s\n", jsonPath.c_str());
    }

    if (g_strictBaseline && g_regressions > 0)
        return 1;
    return g_failures == 0 ? 0 : 1;
}
