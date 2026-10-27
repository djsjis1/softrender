// ============================================================
// SoftRenderer 压测程序入口（只用公共头）
//
// 【职责】只做 CLI 解析、场景调度与汇总；场景实现按主题分在 stress/ 下，
// 各自用 SR_SCENARIO() 静态登记，本文件不维护场景清单。
//
// 【退出码】0 = 全通过；1 = 有失败（或 --strict-baseline 下有回退）；
// 2 = 参数错误或 --filter 无匹配（避免"什么都没跑"被当成通过）。
//
// 【基线】--baseline 读取上一轮 --json 输出，逐行取 name/ms，不引入 JSON 依赖。
// ============================================================
#include "stress/stress_common.h"

namespace
{

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
    for (size_t i = 0; i < stress::g_results.size(); ++i)
    {
        const stress::ScenarioResult& r = stress::g_results[i];
        out << "    {\"id\":\"" << stress::jsonEscape(r.id) << "\",\"name\":\"" << stress::jsonEscape(r.name)
            << "\",\"ok\":" << (r.ok ? "true" : "false") << ",\"ms\":" << r.ms << ",\"detail\":\""
            << stress::jsonEscape(r.detail) << "\"}" << (i + 1 < stress::g_results.size() ? ",\n" : "\n");
    }
    out << "  ],\n  \"failures\": " << stress::g_failures << ",\n  \"regressions\": " << stress::g_regressions
        << "\n}\n";
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

}  // namespace

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
            stress::g_strictBaseline = true;
        else if (arg.rfind("--filter=", 0) == 0)
            filter = arg.substr(9);
        else if (arg.rfind("--json=", 0) == 0)
            jsonPath = arg.substr(7);
        else if (arg.rfind("--baseline=", 0) == 0)
            stress::g_baseline = loadBaseline(arg.substr(11));
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

    const std::vector<stress::Scenario>& scenarios = stress::registry();

    if (list)
    {
        for (const stress::Scenario& s : scenarios)
            std::printf("%s%s\n", s.id, s.heavy ? "  (heavy)" : "");
        return 0;
    }

    std::printf("%-5s %-28s %12s  %s\n", "状态", "场景", "耗时", "度量");
    std::printf("--------------------------------------------------------------------------\n");

    size_t ran = 0;
    for (const stress::Scenario& s : scenarios)
    {
        if (quick && s.heavy)
            continue;
        if (!contains(s.id, filter))
            continue;
        stress::g_currentScenario = s.id;
        // 单个场景抛异常（如 bad_alloc）不得终止整个探针：上百个场景共用
        // 一个进程，死在中间会让后面的场景完全不被覆盖。
        try
        {
            s.run();
        }
        catch (const std::exception& e)
        {
            stress::report(s.id, false, 0.0, std::string("threw: ") + e.what());
        }
        catch (...)
        {
            stress::report(s.id, false, 0.0, "threw: non-standard exception");
        }
        ++ran;
    }

    if (ran == 0)
    {
        std::printf("[WARN] no scenario matched filter '%s' (use --list)\n", filter.c_str());
        return 2;
    }

    std::printf("--------------------------------------------------------------------------\n");
    if (stress::g_baseline.empty())
    {
        // 没给 --baseline 时这个计数器恒为 0。直接打 “regressions=0” 会让人（和 CI 日志的
        // 阅读者）以为做过性能对比——那是假信号。这里显式写成 n/a。
        std::printf("failures=%d  regressions=n/a（未指定 --baseline，未做性能对比）  scenarios=%zu/%zu  total=%.1f s\n",
                    stress::g_failures, ran, scenarios.size(), stress::msSince(stress::g_start) / 1000.0);
    }
    else
    {
        std::printf("failures=%d  regressions=%d  scenarios=%zu/%zu  total=%.1f s\n", stress::g_failures,
                    stress::g_regressions, ran, scenarios.size(), stress::msSince(stress::g_start) / 1000.0);
    }

    if (!jsonPath.empty())
    {
        writeJson(jsonPath);
        std::printf("json written: %s\n", jsonPath.c_str());
    }

    if (stress::g_strictBaseline && stress::g_regressions > 0)
        return 1;
    return stress::g_failures == 0 ? 0 : 1;
}
