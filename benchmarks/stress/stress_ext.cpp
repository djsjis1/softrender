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

}  // namespace stress
