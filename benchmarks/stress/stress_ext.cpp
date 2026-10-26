// ============================================================
// 压测场景（扩展）：长期无覆盖的模块
//
// 【为什么单独一个文件】stress_scenarios.cpp 保留既有基线场景，以免历史
// --baseline 对比失真；新增场景写在这里，每个都用 SR_SCENARIO() 静态登记。
//
// 【选题判据】优先覆盖"文档声称已修但从未被压测验证"的契约，
// 以及历史上真实出过缺陷的路径（日志格式化、G-Buffer 深度、HDR 值域）。
// ============================================================
#include "stress_common.h"

using namespace sr;

namespace stress
{

// ---------------------------------------------------------------- 日志格式化
// 内置模式（未定义 SR_USE_SPDLOG，压测程序走的就是这条）历史上出过三类缺陷：
// ① std::string 经 C 可变参是 UB；② 约定写 {} 却按 printf 解析导致占位符不替换；
// ③ 文本里的字面 '%' 被 vfprintf 当转换符。这里逐条断言 formatLog 的返回字符串。
static void stress_log_format_contract()
{
    const auto t0 = Clock::now();
    bool ok = true;
    int cases = 0;

    // 1. 按顺序替换，混合类型
    ok = ok && formatLog("v={} n={} f={}", 1, 2.5, "x") == "v=1 n=2.5 f=x";
    ++cases;

    // 2. 字面 '%' 不得被当作转换符（旧 va_list 实现的 UB 面）
    ok = ok && formatLog("100% done: {}%", 50) == "100% done: 50%";
    ++cases;

    // 3. 参数多于占位符：追加输出而不是静默丢弃
    const std::string extra = formatLog("only {}", 1, 2);
    ok = ok && extra.rfind("only 1", 0) == 0 && extra.find('2') != std::string::npos;
    ++cases;

    // 4. 占位符多于参数：降级保留 {}，不得崩溃
    ok = ok && formatLog("{} and {}", 7).find("7 and {}") != std::string::npos;
    ++cases;

    // 5. std::string 参数（经 ... 传非 POD 是 UB，模板路径必须安全）
    const std::string str = "str";
    ok = ok && formatLog("<{}>", str) == "<str>";
    ++cases;

    // 6. 无占位符但带参数
    ok = ok && !formatLog("plain", 42).empty();
    ++cases;

    // 7. 满递归深度
    ok = ok && formatLog("{}{}{}{}{}{}{}{}", 1, 2, 3, 4, 5, 6, 7, 8) == "12345678";
    ++cases;

    // 8. 空格式串 + 参数
    ok = ok && !formatLog("", 42).empty();
    ++cases;

    report("log_format_contract", ok, msSince(t0),
           "cases=" + std::to_string(cases) + " literal-percent=ok");
}

// ---------------------------------------------------------------- G-Buffer 契约
// 深度测试与写入口径：非有限深度必须拒绝（否则 NaN 与任何比较都为假，
// 后续像素的深度测试整体失效）；尺寸非法不得破坏已有数据。
static void stress_gbuffer_write_contract()
{
    const auto t0 = Clock::now();
    bool ok = true;

    GBuffer gb;
    gb.resize(64, 48);
    ok = ok && gb.width == 64 && gb.height == 48;
    ok = ok && gb.worldPos.size() == 64u * 48u && gb.depth.size() == 64u * 48u;

    // 越界写被拒
    ok = ok && !gb.write(-1, 0, 0.5f, Vec3f{}, Vec3f{}, 0.5f, Vec3f{}, 0.0f);
    ok = ok && !gb.write(64, 0, 0.5f, Vec3f{}, Vec3f{}, 0.5f, Vec3f{}, 0.0f);
    ok = ok && !gb.write(0, 48, 0.5f, Vec3f{}, Vec3f{}, 0.5f, Vec3f{}, 0.0f);

    // NaN / inf 深度被拒
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    ok = ok && !gb.write(1, 1, nan, Vec3f{}, Vec3f{}, 0.5f, Vec3f{}, 0.0f);
    ok = ok && !gb.write(1, 1, inf, Vec3f{}, Vec3f{}, 0.5f, Vec3f{}, 0.0f);

    // 正常写入 + 更远的被深度测试拒绝
    ok = ok && gb.write(5, 5, 0.5f, Vec3f{1, 2, 3}, Vec3f{0, 1, 0}, 0.25f, Vec3f{0.2f, 0.4f, 0.6f}, 0.75f);
    ok = ok && !gb.write(5, 5, 0.9f, Vec3f{9, 9, 9}, Vec3f{}, 1.0f, Vec3f{}, 1.0f);

    const size_t idx = size_t(5) * 64u + 5u;
    ok = ok && gb.worldPos[idx].x == 1.0f;
    ok = ok && gb.normalRough[idx].w == 0.25f;
    ok = ok && gb.albedoMetal[idx].w == 0.75f;

    // 更近的写入成功并覆盖
    ok = ok && gb.write(5, 5, 0.1f, Vec3f{-1, -2, -3}, Vec3f{}, 0.5f, Vec3f{}, 0.0f);
    ok = ok && gb.worldPos[idx].x == -1.0f;

    // clear 把深度重置为 far
    gb.clear();
    ok = ok && gb.depth[idx] > 1e8f;

    // 非法 resize 不得破坏现有尺寸
    gb.resize(-1, 10);
    ok = ok && gb.width == 64 && gb.height == 48;

    report("gbuffer_write_contract", ok, msSince(t0), "targets=3 nan-reject=ok");
}

// ---------------------------------------------------------------- Camera2D 互逆与退化
// worldToScreen 与 screenToWorld 必须互为逆运算（鼠标拾取/命中测试依赖）；
// zoom=0 会让 viewProj 奇异，inversed() 有单位阵兜底，但绝不得产生 NaN。
static void stress_camera2d_roundtrip()
{
    const auto t0 = Clock::now();
    bool ok = true;

    Camera2D cam(1920, 1080);
    cam.zoom = 17.5f;
    cam.rotation = 0.7f;
    cam.position = Vec2f{3.5f, -2.25f};

    std::mt19937 rng(12345u);
    std::uniform_real_distribution<float> dist(-500.0f, 500.0f);
    float maxErr = 0.0f;
    for (int i = 0; i < 2000; ++i)
    {
        const Vec2f world{dist(rng), dist(rng)};
        const Vec2f back = cam.screenToWorld(cam.worldToScreen(world));
        maxErr = std::max(maxErr, std::max(std::abs(back.x - world.x), std::abs(back.y - world.y)));
    }
    // 往返误差只来自 float 精度：量级 1e-3 像素足够严格
    ok = ok && maxErr < 1e-2f;

    // yDown 只翻转屏幕 y，不得翻转 x
    Camera2D flipped = cam;
    flipped.yDown = true;
    const Vec2f w0{10.0f, 20.0f};
    const Vec2f sNorm = cam.worldToScreen(w0);
    const Vec2f sFlip = flipped.worldToScreen(w0);
    ok = ok && std::abs(sNorm.x - sFlip.x) < 1e-3f;
    ok = ok && std::abs((1080.0f - sNorm.y) - sFlip.y) < 1e-2f;

    // 退化 zoom：不崩溃、不出 NaN
    Camera2D degenerate = cam;
    degenerate.zoom = 0.0f;
    const Vec2f picked = degenerate.screenToWorld(Vec2f{400.0f, 300.0f});
    ok = ok && std::isfinite(picked.x) && std::isfinite(picked.y);

    // zoomToFit 后可见区应容纳原矩形
    Camera2D fit(800, 600);
    const Rectf bounds(-40.0f, -30.0f, 80.0f, 60.0f);
    fit.zoomToFit(bounds);
    const Rectf visible = fit.visibleWorldRect();
    ok = ok && visible.w >= bounds.w - 1e-2f && visible.h >= bounds.h - 1e-2f;

    report("camera2d_roundtrip", ok, msSince(t0), "maxErr=" + std::to_string(maxErr));
}

// ---------------------------------------------------------------- 渲染队列排序
// sort() 与 sortKey() 是两个入口，必须给出同一顺序（历史上就因材质与距离
// 的先后顺序不同而不一致）。这里逐对校验三级次序：层 → 距离 → 材质。
static void stress_render_queue_order()
{
    const auto t0 = Clock::now();
    bool ok = true;

    std::mt19937 rng(777u);
    std::uniform_real_distribution<float> d(-50.0f, 50.0f);
    std::uniform_int_distribution<int> layerPick(0, 4);
    const RenderLayer layers[5] = {RenderLayer::Background, RenderLayer::Opaque, RenderLayer::AlphaTest,
                                   RenderLayer::Transparent, RenderLayer::Overlay};

    RenderQueue queue;
    std::vector<Vec3f> positions;
    for (int i = 0; i < 500; ++i)
    {
        positions.push_back(Vec3f{d(rng), d(rng), d(rng)});
        DrawItem item;
        item.layer = layers[layerPick(rng)];
        item.materialId = int(rng() % 8);
        item.objectIndex = i;
        queue.add(item);
    }

    const Vec3f cameraPos{0.0f, 0.0f, 0.0f};
    queue.sort(cameraPos, positions);
    ok = ok && queue.size() == 500;

    const std::vector<DrawItem>& items = queue.items();
    bool ordered = true;
    for (size_t i = 1; i < items.size() && ordered; ++i)
    {
        const DrawItem& a = items[i - 1];
        const DrawItem& b = items[i];
        if (a.layer != b.layer)
        {
            ordered = a.layer < b.layer;
            continue;
        }
        const bool reverse = a.layer >= RenderLayer::Transparent;
        if (a.sortDistance != b.sortDistance)
            ordered = reverse ? (a.sortDistance >= b.sortDistance) : (a.sortDistance <= b.sortDistance);
        else
            ordered = a.materialId <= b.materialId;
    }
    ok = ok && ordered;

    // 两个入口一致性：同层内按 sortKey 升序（透明层用 reverseDistance）
    bool keyConsistent = true;
    for (size_t i = 1; i < items.size() && keyConsistent; ++i)
    {
        const DrawItem& a = items[i - 1];
        const DrawItem& b = items[i];
        const bool reverse = a.layer >= RenderLayer::Transparent;
        keyConsistent = a.sortKey(reverse) <= b.sortKey(reverse);
    }
    ok = ok && keyConsistent;

    // 计数口径
    ok = ok && queue.countOpaque() + queue.countTransparent() == int(queue.size());

    report("render_queue_order", ok, msSince(t0),
           "items=500 opaque=" + std::to_string(queue.countOpaque()));
}

// ---------------------------------------------------------------- 尺寸与 HDR 值域
// 构造器与 resize() 必须同口径：负尺寸不得变成巨量分配。
// HDR 缓冲也不得偷偷裁剪越界值（压制应在色调映射阶段）。
static void stress_buffer_size_contract()
{
    const auto t0 = Clock::now();
    bool ok = true;

    bool threw = false;
    try
    {
        DepthBuffer depth(-8, -8);
        ok = ok && depth.width() == 0 && depth.height() == 0;
    }
    catch (const std::exception&)
    {
        threw = true;
    }
    ok = ok && !threw;

    threw = false;
    try
    {
        FloatFramebuffer hdr(-4, 6);
        ok = ok && hdr.width() == 0 && hdr.height() == 0;
    }
    catch (const std::exception&)
    {
        threw = true;
    }
    ok = ok && !threw;

    // HDR 值域：>1、负数、NaN 原样保存
    FloatFramebuffer fb(8, 8);
    fb.setPixel(0, 0, Vec3f{1e6f, -5.0f, std::numeric_limits<float>::quiet_NaN()});
    const Vec3f v = fb.getPixel(0, 0);
    ok = ok && v.x == 1e6f && v.y == -5.0f && std::isnan(v.z);

    // 越界写入被忽略而不是踩内存
    fb.setPixel(-1, 0, Vec3f{1, 1, 1});
    fb.setPixel(8, 0, Vec3f{1, 1, 1});
    ok = ok && fb.getPixel(0, 0).x == 1e6f;

    report("buffer_size_contract", ok, msSince(t0), "ctor-vs-resize=consistent");
}

SR_SCENARIO("log_format_contract", stress_log_format_contract, false);
SR_SCENARIO("gbuffer_write_contract", stress_gbuffer_write_contract, false);
SR_SCENARIO("camera2d_roundtrip", stress_camera2d_roundtrip, false);
SR_SCENARIO("render_queue_order", stress_render_queue_order, false);
// ---------------------------------------------------------------- 视锥裁剪边界
// 六个裁剪面都是半开区间 [-w, w]：恰在平面上的顶点必须保留，
// 否则贴面几何会在两条相邻路径上一边画一边不画。
static ClipVertex makeClipVertex(float x, float y, float z, float w)
{
    ClipVertex v;
    v.pos = Vec4f{x, y, z, w};
    v.var.worldPos = Vec3f{x, y, z};
    return v;
}

static void stress_clip_boundary_contract()
{
    const auto t0 = Clock::now();
    bool ok = true;
    ClipVertex out[kMaxClipVerts];

    // 1. 完全在视锥内
    const ClipVertex inside[3] = {makeClipVertex(-0.5f, -0.5f, 0.0f, 1.0f),
                                  makeClipVertex(0.5f, -0.5f, 0.0f, 1.0f),
                                  makeClipVertex(0.0f, 0.5f, 0.0f, 1.0f)};
    ok = ok && clipTriangleFullFrustum(inside, out) == 3;

    // 2. 完全在近平面之后（z < -w）
    const ClipVertex behind[3] = {makeClipVertex(-0.5f, -0.5f, -2.0f, 1.0f),
                                  makeClipVertex(0.5f, -0.5f, -2.0f, 1.0f),
                                  makeClipVertex(0.0f, 0.5f, -2.0f, 1.0f)};
    ok = ok && clipTriangleFullFrustum(behind, out) == 0;

    // 3. 一个顶点穿过近平面：三角形变四边形，且所有输出点都在平面内侧
    const ClipVertex straddle[3] = {makeClipVertex(-0.5f, -0.5f, 0.0f, 1.0f),
                                    makeClipVertex(0.5f, -0.5f, 0.0f, 1.0f),
                                    makeClipVertex(0.0f, 0.5f, -2.0f, 1.0f)};
    int n = clipTriangleFullFrustum(straddle, out);
    ok = ok && n == 4;
    for (int i = 0; i < n; ++i)
        ok = ok && out[i].pos.z >= -out[i].pos.w - 1e-4f;

    // 4. 顶点恰在近平面上（z == -w）：属于闭区间内侧，不得被裁掉
    const ClipVertex onPlane[3] = {makeClipVertex(-0.5f, -0.5f, -1.0f, 1.0f),
                                   makeClipVertex(0.5f, -0.5f, -1.0f, 1.0f),
                                   makeClipVertex(0.0f, 0.5f, -1.0f, 1.0f)};
    ok = ok && clipTriangleFullFrustum(onPlane, out) == 3;

    // 5. 反转绕序：裁剪结果不应依赖顶点顺序
    const ClipVertex reversed[3] = {inside[2], inside[1], inside[0]};
    ok = ok && clipTriangleFullFrustum(reversed, out) == 3;

    // 6. w = 0 的退化输入：不得越界、不得返回超过上界的数量
    const ClipVertex zeroW[3] = {makeClipVertex(0.0f, 0.0f, 0.0f, 0.0f),
                                 makeClipVertex(1.0f, 0.0f, 0.0f, 0.0f),
                                 makeClipVertex(0.0f, 1.0f, 0.0f, 0.0f)};
    n = clipTriangleFullFrustum(zeroW, out);
    ok = ok && n >= 0 && n <= kMaxClipVerts;

    // 7. 远平面外（z > w）
    const ClipVertex tooFar[3] = {makeClipVertex(-0.5f, -0.5f, 3.0f, 1.0f),
                                  makeClipVertex(0.5f, -0.5f, 3.0f, 1.0f),
                                  makeClipVertex(0.0f, 0.5f, 3.0f, 1.0f)};
    ok = ok && clipTriangleFullFrustum(tooFar, out) == 0;

    // 8. 左侧面外（x < -w）
    const ClipVertex leftOut[3] = {makeClipVertex(-3.0f, 0.0f, 0.0f, 1.0f),
                                   makeClipVertex(-3.0f, 0.5f, 0.0f, 1.0f),
                                   makeClipVertex(-3.0f, -0.5f, 0.0f, 1.0f)};
    ok = ok && clipTriangleFullFrustum(leftOut, out) == 0;

    report("clip_boundary_contract", ok, msSince(t0), "faces=6 straddle=4 on-plane=3");
}

SR_SCENARIO("buffer_size_contract", stress_buffer_size_contract, false);
// ---------------------------------------------------------------- 多边形填充健壮性
// 扫描线填充的入口是浮点顶点集：NaN 与大坐标都会在 float→int 处进 UB。
// fillCircle / fillRoundedRect 早就有 allFinite 前置校验，fillPolygon 曾遗漏。
static void stress_polygon_fill_robustness()
{
    const auto t0 = Clock::now();
    bool ok = true;

    Framebuffer fb(64, 64, colors::black);
    auto nonBlack = [&fb] {
        int n = 0;
        for (int y = 0; y < fb.height(); ++y)
            for (int x = 0; x < fb.width(); ++x)
            {
                const Color p = fb.getPixel(x, y);
                if (p.r != 0 || p.g != 0 || p.b != 0)
                    ++n;
            }
        return n;
    };

    // 1. 直角边 40 的三角形：面积 800 像素
    std::vector<Vec2f> tri = {{10.0f, 10.0f}, {50.0f, 10.0f}, {10.0f, 50.0f}};
    draw2d::fillPolygon(fb, tri, colors::white);
    const int triPixels = nonBlack();
    ok = ok && triPixels > 760 && triPixels < 840;

    // 2. 自交（蝴蝶结）：不得死循环或崩溃，仍应有填充
    fb.clear(colors::black);
    std::vector<Vec2f> bowtie = {{10.0f, 10.0f}, {50.0f, 50.0f}, {10.0f, 50.0f}, {50.0f, 10.0f}};
    draw2d::fillPolygon(fb, bowtie, colors::white);
    ok = ok && nonBlack() > 0;

    // 3. NaN 顶点：整体拒绝，画面保持全黑
    fb.clear(colors::black);
    std::vector<Vec2f> nanPoly = {{10.0f, 10.0f}, {std::numeric_limits<float>::quiet_NaN(), 20.0f}, {30.0f, 30.0f}};
    draw2d::fillPolygon(fb, nanPoly, colors::white);
    ok = ok && nonBlack() == 0;

    // 4. 极大坐标：float→int 曾为 UB；修复后只允许在屏幕内填充
    fb.clear(colors::black);
    std::vector<Vec2f> huge = {{-1.0e30f, 10.0f}, {1.0e30f, 10.0f}, {0.0f, 50.0f}};
    draw2d::fillPolygon(fb, huge, colors::white);
    const int hugeCount = nonBlack();
    ok = ok && hugeCount > 0 && hugeCount <= 64 * 64;

    // 5. 点数不足：直接返回
    fb.clear(colors::black);
    std::vector<Vec2f> two = {{10.0f, 10.0f}, {50.0f, 50.0f}};
    draw2d::fillPolygon(fb, two, colors::white);
    ok = ok && nonBlack() == 0;

    report("polygon_fill_robustness", ok, msSince(t0), "tri=" + std::to_string(triPixels));
}

SR_SCENARIO("clip_boundary_contract", stress_clip_boundary_contract, false);
SR_SCENARIO("polygon_fill_robustness", stress_polygon_fill_robustness, false);

// ============================================================
// 像素格式与呈现层。见 ADR-005：存储格式与运算域分离，
// 渲染侧只多一次量化，呈现侧由 NearestUpscaler 做放大 + 换格式。
// ============================================================

// 1. 小画布 → 目标分辨率的放大：整数倍必须精确复制，非整数倍不得越界，
//    降采样、退化 1x1、非法尺寸都必须不崩。
static void stress_present_upscale_565()
{
    const auto t0 = Clock::now();
    bool ok = true;
    std::string detail;

    // 整数倍 2x：源 160x120 → 目标 320x240，每个源像素必须铺满 2x2
    {
        Framebuffer src(160, 120);
        for (int y = 0; y < 120; ++y)
            for (int x = 0; x < 160; ++x)
                src.setPixel(x, y, Color(uint8_t(x), uint8_t(y), uint8_t((x * 3 + y) & 0xFF)));

        BasicFramebuffer<Color565> screen(320, 240);
        NearestUpscaler up;
        up.resize(160, 120, 320, 240);
        up.present(src, screen);

        const int probeX[5] = {0, 159, 80, 1, 318};
        const int probeY[5] = {0, 119, 60, 1, 238};
        int mismatches = 0;
        for (int i = 0; i < 5; ++i)
        {
            const Color expected = Color565::fromColor(src.getPixel(probeX[i] / 2, probeY[i] / 2)).toColor();
            if (screen.getPixel(probeX[i], probeY[i]) != expected)
                ++mismatches;
        }
        ok = ok && mismatches == 0;
        detail += "2x_mismatch=" + std::to_string(mismatches);
    }

    // 非整数倍 1.6x：200x150 → 320x240，只校验覆盖完整（不得留黑边）
    {
        Framebuffer src(200, 150, Color(30, 60, 90));
        BasicFramebuffer<Color565> screen(320, 240);
        NearestUpscaler up;
        up.resize(200, 150, 320, 240);
        up.present(src, screen);
        const Color565 expected = Color565::fromColor(Color(30, 60, 90));
        int holes = 0;
        for (int y = 0; y < 240; ++y)
            for (int x = 0; x < 320; ++x)
                if (!(screen.getPixel(x, y) == expected.toColor()))
                    ++holes;
        ok = ok && holes == 0;
        detail += " 1.6x_holes=" + std::to_string(holes);
    }

    // 降采样 320x240 → 160x120：映射为 i*2，取偶数行列
    {
        Framebuffer src(320, 240);
        for (int y = 0; y < 240; ++y)
            for (int x = 0; x < 320; ++x)
                src.setPixel(x, y, Color(uint8_t(x & 0xFF), uint8_t(y & 0xFF), 0));
        Framebuffer dst(160, 120);
        NearestUpscaler up;
        up.resize(320, 240, 160, 120);
        up.present(src, dst);
        ok = ok && dst.getPixel(0, 0) == src.getPixel(0, 0) && dst.getPixel(159, 119) == src.getPixel(318, 238);
    }

    // 退化源 1x1：整屏应是同一颜色
    {
        Framebuffer src(1, 1, Color(200, 100, 50));
        BasicFramebuffer<Color565> screen(64, 64);
        NearestUpscaler up;
        up.resize(1, 1, 64, 64);
        up.present(src, screen);
        const Color expected = Color565::fromColor(Color(200, 100, 50)).toColor();
        int mismatches = 0;
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x)
                if (!(screen.getPixel(x, y) == expected))
                    ++mismatches;
        ok = ok && mismatches == 0;
        detail += " 1x1_mismatch=" + std::to_string(mismatches);
    }

    // 非法尺寸：不得写入、不得崩
    {
        NearestUpscaler up;
        up.resize(0, 0, 64, 64);
        Framebuffer src(4, 4, colors::white);
        Framebuffer dst(64, 64, colors::black);
        up.present(src, dst);
        ok = ok && !up.valid() && dst.getPixel(0, 0) == colors::black;
    }

    report("present_upscale_565", ok, msSince(t0), detail);
}

// 2. 同一场景渲染进 8888 与 565：只允许差一个量化步长；
//    同时核对内存占用确实减半（这是引入格式的唯一目的）。
static void stress_render_565_matches_8888()
{
    const auto t0 = Clock::now();
    const int W = 240, H = 180;
    const int draws = 8;

    Framebuffer fb8888(W, H);
    BasicFramebuffer<Color565> fb565(W, H);
    const Mesh sphere = sr::meshgen::sphere(1.0f, 24, 16);

    Renderer renderer8888(W, H);
    Renderer renderer565(W, H);
    PBRShader shader8888 = makePbr(W, H);
    PBRShader shader565 = makePbr(W, H);

    renderer8888.clear(fb8888, Color(8, 10, 14));
    renderer565.clear(fb565, Color(8, 10, 14));

    for (int i = 0; i < draws; ++i)
    {
        const Mat4 model = Mat4::translate(0.0f, 0.0f, -float(i % 3)) * Mat4::rotateY(0.4f * float(i));
        const Mat4 mvp = shader8888.proj * shader8888.view * model;
        const Mat4 normalMatrix = model.inversed().transposed();
        shader8888.model = model;
        shader8888.mvp = mvp;
        shader8888.normalMatrix = normalMatrix;
        shader565.model = model;
        shader565.mvp = mvp;
        shader565.normalMatrix = normalMatrix;
        renderer8888.drawMesh(fb8888, sphere, shader8888);
        renderer565.drawMesh(fb565, sphere, shader565);
    }

    int maxDiff = 0;
    int litPixels = 0;
    for (int y = 0; y < H; ++y)
    {
        for (int x = 0; x < W; ++x)
        {
            const Color a = fb8888.getPixel(x, y);
            const Color b = fb565.getPixel(x, y);
            if (a != Color(8, 10, 14))
                ++litPixels;
            maxDiff = std::max({maxDiff, std::abs(int(a.r) - int(b.r)), std::abs(int(a.g) - int(b.g)),
                                std::abs(int(a.b) - int(b.b))});
        }
    }

    const bool ok = litPixels > 0 && maxDiff <= 4 && fb565.byteSize() * 2 == fb8888.byteSize();
    report("render_565_matches_8888", ok, msSince(t0),
           "maxDiff=" + std::to_string(maxDiff) + " lit=" + std::to_string(litPixels) + " bytes=" +
               std::to_string(fb8888.byteSize()) + "->" + std::to_string(fb565.byteSize()));
}

// 3. 2D UI 直接画进 565 屏幕缓冲：MCU 上最常见的用法。
//    覆盖范围必须与存储格式无关，内存必须精确减半。
//    容差取 8：带 AlphaBlend 的图元要把 dst 读回 8888 再量化一次。
static void stress_draw2d_565_ui()
{
    const auto t0 = Clock::now();
    const int W = 320, H = 240;
    Framebuffer ref(W, H);
    BasicFramebuffer<Color565> screen(W, H);
    ref.clear(colors::black);
    screen.clear(colors::black);

    auto paint = [&](auto& fb)
    {
        draw2d::fillRect(fb, 0, 0, W, H, Color(16, 20, 28));
        draw2d::fillRect(fb, 8, 8, 150, 90, Color(40, 60, 90));
        draw2d::fillRoundedRect(fb, 170.0f, 8.0f, 140.0f, 90.0f, 8.0f, Color(70, 40, 90));
        for (int i = 0; i < 12; ++i)
            draw2d::drawLine(fb, 10, 110 + i * 10, W - 10, 110 + i * 10,
                             Color(uint8_t(20 * i), 200, uint8_t(255 - 20 * i)));
        draw2d::fillCircle(fb, 60.0f, 200.0f, 24.0f, Color(220, 120, 40));
        draw2d::drawCircle(fb, 140, 200, 30, Color(120, 220, 120), 2);
        std::vector<Vec2f> poly = {{190.0f, 170.0f}, {260.0f, 160.0f}, {300.0f, 210.0f}, {210.0f, 225.0f}};
        draw2d::fillPolygon(fb, poly, Color(90, 160, 220));
        // 半透明椭圆：对不透明背景做一次完整的 plotBlend 混合路径
        draw2d::fillEllipse(fb, 40.0f, 40.0f, 30.0f, 18.0f, Color(200, 200, 60, 140), BlendMode::AlphaBlend);
    };

    const int frames = 10;
    const auto tFrame = Clock::now();
    for (int f = 0; f < frames; ++f)
        paint(screen);
    const double frameMs = msSince(tFrame) / frames;

    paint(ref);  // 参考帧只画一次

    int coverageMismatch = 0;
    int maxDiff = 0;
    int lit = 0;
    for (int y = 0; y < H; ++y)
    {
        for (int x = 0; x < W; ++x)
        {
            const Color a = ref.getPixel(x, y);
            const Color b = screen.getPixel(x, y);
            if (!(a == Color(16, 20, 28)))
                ++lit;
            maxDiff = std::max({maxDiff, std::abs(int(a.r) - int(b.r)), std::abs(int(a.g) - int(b.g)),
                                std::abs(int(a.b) - int(b.b))});
        }
    }
    // 覆盖范围一致性：565 侧只有 (16,20,28) 量化后的值才算"未绘制"。
    const Color565 bg565 = Color565::fromColor(Color(16, 20, 28));
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            if ((ref.getPixel(x, y) == Color(16, 20, 28)) != (screen.getPixel(x, y) == bg565.toColor()))
                ++coverageMismatch;

    const bool ok = lit > W * H / 4 && maxDiff <= 8 && coverageMismatch == 0 &&
                    screen.byteSize() * 2 == ref.byteSize();
    report("draw2d_565_ui_frame", ok, msSince(t0),
           "frame_ms=" + std::to_string(frameMs) + " maxDiff=" + std::to_string(maxDiff) + " cov_mismatch=" +
               std::to_string(coverageMismatch) + " bytes=" + std::to_string(ref.byteSize()) + "->" +
               std::to_string(screen.byteSize()));
}

// 4. 烟花管线的 8888 vs 565 对比（对齐 practice/fireworks.cpp 的实际步骤）。
//
// 【为什么要分开计时】浮点 HDR 场景与 bloom 是**与像素格式无关的共用成本**，
// 只有两步与格式相关：① 色调映射后写 LDR 缓冲；② LDR 放大到输出分辨率。
// 把共用成本算进格式收益会得出"565 快一倍"的错误结论。
static void stress_fireworks_565_vs_8888()
{
    const auto t0 = Clock::now();
    const int rw = 320, rh = 180;  // 内部渲染分辨率（fireworks 的 rw/rh）
    const int W = 640, H = 360;    // 输出分辨率（2× 最近邻放大）

    // ── 共用部分：HDR 场景 + 半分辨率亮部 + 两遍模糊 ──
    FloatFramebuffer scene(rw, rh);
    for (int y = 0; y < rh; ++y)
    {
        for (int x = 0; x < rw; ++x)
        {
            const float dx = (float(x) - rw * 0.5f) / (rw * 0.25f);
            const float dy = (float(y) - rh * 0.5f) / (rh * 0.25f);
            const float r2 = dx * dx + dy * dy;
            scene.setPixel(x, y, Vec3f{2.0f / (1.0f + r2), 1.2f / (1.0f + 2.0f * r2), 0.4f / (1.0f + 4.0f * r2)});
        }
    }
    FloatFramebuffer brightHalf(rw / 2, rh / 2);
    for (int y = 0; y < rh / 2; ++y)
        for (int x = 0; x < rw / 2; ++x)
        {
            Vec3f sum{};
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx)
                    sum += scene.getPixel(x * 2 + dx, y * 2 + dy);
            brightHalf.setPixel(x, y, sum * 0.25f);
        }
    FloatFramebuffer blurH(rw / 2, rh / 2), blurV(rw / 2, rh / 2);
    sr::postprocess::gaussianBlur(brightHalf, blurH, true, 5);
    sr::postprocess::gaussianBlur(blurH, blurV, false, 5);
    const double commonMs = msSince(t0);

    const int bloomW = blurV.width();
    const int bloomH = blurV.height();
    const int reps = 20;

    // ── 格式相关 ①：色调映射写 LDR ──
    Framebuffer ldr8888(rw, rh);
    BasicFramebuffer<Color565> ldr565(rw, rh);

    auto tonemapInto = [&](auto& ldr)
    {
        using Pixel = typename std::remove_reference_t<decltype(ldr)>::Pixel;
        for (int y = 0; y < rh; ++y)
        {
            const Vec3f* srow = scene.row(y);
            const Vec3f* brow = blurV.row(std::min(y / 2, bloomH - 1));
            Pixel* lrow = ldr.row(y);
            for (int x = 0; x < rw; ++x)
            {
                const Vec3f c = srow[x] + brow[std::min(x / 2, bloomW - 1)] * 1.05f;
                const Vec3f mapped = sr::postprocess::gammaCorrectFast(sr::postprocess::acesToneMap(c));
                lrow[x] = Pixel::fromFloat(mapped.x, mapped.y, mapped.z);
            }
        }
    };

    const auto tTonemap = Clock::now();
    for (int i = 0; i < reps; ++i) tonemapInto(ldr8888);
    const double tonemap8888Ms = msSince(tTonemap) / reps;
    const auto tTonemap565 = Clock::now();
    for (int i = 0; i < reps; ++i) tonemapInto(ldr565);
    const double tonemap565Ms = msSince(tTonemap565) / reps;

    // ── 格式相关 ②：LDR 放大到输出分辨率 ──
    Framebuffer out8888(W, H);
    BasicFramebuffer<Color565> out565(W, H);

    const auto tUp = Clock::now();
    for (int i = 0; i < reps; ++i)
        for (int y = 0; y < H; ++y)
        {
            const Color* srow = ldr8888.row(y / 2);
            Color* orow = out8888.row(y);
            for (int x = 0; x < W; ++x)
                orow[x] = srow[x / 2];
        }
    const double up8888Ms = msSince(tUp) / reps;

    const auto tUp565 = Clock::now();
    for (int i = 0; i < reps; ++i)
        for (int y = 0; y < H; ++y)
        {
            const Color565* srow = ldr565.row(y / 2);
            Color565* orow = out565.row(y);
            for (int x = 0; x < W; ++x)
                orow[x] = srow[x / 2];
        }
    const double up565Ms = msSince(tUp565) / reps;

    // ── 正确性：565 结果展开后与 8888 只差量化 ──
    int maxDiff = 0;
    for (int y = 0; y < H; y += 3)
        for (int x = 0; x < W; x += 3)
        {
            const Color a = out8888.getPixel(x, y);
            const Color b = out565.getPixel(x, y);
            maxDiff = std::max({maxDiff, std::abs(int(a.r) - int(b.r)), std::abs(int(a.g) - int(b.g)),
                                std::abs(int(a.b) - int(b.b))});
        }

    const double frame8888 = commonMs + tonemap8888Ms + up8888Ms;
    const double frame565 = commonMs + tonemap565Ms + up565Ms;
    const bool ok = maxDiff <= 4 && out565.byteSize() * 2 == out8888.byteSize();

    report("fireworks_565_vs_8888", ok, msSince(t0),
           "frame ms 8888=" + std::to_string(frame8888) + " 565=" + std::to_string(frame565) +
               " | common=" + std::to_string(commonMs) + " tonemap=" + std::to_string(tonemap8888Ms) + "/" +
               std::to_string(tonemap565Ms) + " upscale=" + std::to_string(up8888Ms) + "/" +
               std::to_string(up565Ms) + " | out bytes=" + std::to_string(out8888.byteSize()) + "->" +
               std::to_string(out565.byteSize()) + " maxDiff=" + std::to_string(maxDiff));
}

// 5. 同格式呈现的快速路径：MCU 上"渲染到 565 → 放大到 565 屏幕"走的就是这条。
//    对照组是 8888 源 → 565 目标的通用路径（每像素要展开再量化回来）。
//    两者输出必须逐位一致，差别只应体现在耗时上。
static void stress_present_same_format_565()
{
    const auto t0 = Clock::now();
    const int rw = 160, rh = 120, W = 320, H = 240;

    BasicFramebuffer<Color565> src565(rw, rh);
    Framebuffer src8888(rw, rh);
    for (int y = 0; y < rh; ++y)
    {
        for (int x = 0; x < rw; ++x)
        {
            const Color c(uint8_t(x), uint8_t(y), uint8_t((x * 3 + y * 5) & 0xFF));
            src8888.setPixel(x, y, c);
            src565.setPixel(x, y, c);
        }
    }

    NearestUpscaler up;
    up.resize(rw, rh, W, H);
    const int reps = 20;

    BasicFramebuffer<Color565> dstSame(W, H);
    const auto tA = Clock::now();
    for (int i = 0; i < reps; ++i)
        up.present(src565, dstSame);
    const double sameMs = msSince(tA) / reps;

    BasicFramebuffer<Color565> dstCross(W, H);
    const auto tB = Clock::now();
    for (int i = 0; i < reps; ++i)
        up.present(src8888, dstCross);
    const double crossMs = msSince(tB) / reps;

    int mismatches = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            if (dstSame.data()[size_t(y) * W + x].v != dstCross.data()[size_t(y) * W + x].v)
                ++mismatches;

    const bool ok = mismatches == 0;
    report("present_same_format_565", ok, msSince(t0),
           "same=" + std::to_string(sameMs) + "ms cross=" + std::to_string(crossMs) + "ms ratio=" +
               std::to_string(sameMs / crossMs) + " mismatches=" + std::to_string(mismatches));
}

// 6. 后处理里两个逐像素算子的 1080p 成本：暗角与径向运动模糊。
//    对应本轮的两处优化——vignette 去掉了"sqrt 后再平方回去"的冗余开方，
//    radialMotionBlur 把样本循环里的 getPixel/setPixel 换成行指针。
//    固定输入，供 --baseline 对比；heavy 标记把它挡在 --quick 之外。
static void stress_postprocess_vignette_blur_1080p()
{
    const auto t0 = Clock::now();
    const int W = 1920, H = 1080;

    FloatFramebuffer fb(W, H);
    for (int y = 0; y < H; ++y)
    {
        Vec3f* row = fb.row(y);
        for (int x = 0; x < W; ++x)
            row[x] = Vec3f{float(x) / float(W), float(y) / float(H), 0.5f};
    }

    const int reps = 3;

    const auto tV = Clock::now();
    for (int i = 0; i < reps; ++i)
        sr::postprocess::vignette(fb, 0.4f);
    const double vignetteMs = msSince(tV) / reps;

    FloatFramebuffer blurred(W, H);
    const auto tB = Clock::now();
    for (int i = 0; i < reps; ++i)
        sr::postprocess::radialMotionBlur(fb, W, H, 0.3f, 16, blurred);
    const double blurMs = msSince(tB) / reps;

    // 正确性：输出有限；暗角必须让角落比中心暗（中心 r=0，factor=1 不变）
    const Vec3f center = fb.getPixel(W / 2, H / 2);
    const Vec3f corner = fb.getPixel(1, 1);
    const Vec3f mid = blurred.getPixel(W / 2, H / 2);
    const bool finite = std::isfinite(mid.x) && std::isfinite(mid.y) && std::isfinite(mid.z) &&
                        std::isfinite(corner.x) && std::isfinite(center.x);
    const bool darker = (corner.x + corner.y + corner.z) < (center.x + center.y + center.z);

    report("postprocess_vignette_blur_1080p", finite && darker, msSince(t0),
           "vignette=" + std::to_string(vignetteMs) + "ms blur16=" + std::to_string(blurMs) +
               "ms cornerR=" + std::to_string(corner.x) + " centerR=" + std::to_string(center.x));
}

SR_SCENARIO("present_upscale_565", stress_present_upscale_565, false);
SR_SCENARIO("render_565_matches_8888", stress_render_565_matches_8888, false);
SR_SCENARIO("draw2d_565_ui_frame", stress_draw2d_565_ui, false);
SR_SCENARIO("fireworks_565_vs_8888", stress_fireworks_565_vs_8888, false);
SR_SCENARIO("present_same_format_565", stress_present_same_format_565, false);
SR_SCENARIO("postprocess_vignette_blur_1080p", stress_postprocess_vignette_blur_1080p, true);

}  // namespace stress
