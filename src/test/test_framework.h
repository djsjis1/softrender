#pragma once
// ============================================================
// 极简单元测试框架（零依赖，header-only）
//
// 用法：
//   #include "test/test_framework.h"
//   TEST(math, vec_dot_product) {
//       Vec3f a{1,0,0}, b{0,1,0};
//       ASSERT_FLOAT_EQ(dot(a,b), 0.0f, 1e-6f);
//   }
//   int main() { return RUN_ALL_TESTS(); }
//
// 参考 Google Test 的 API 风格，但只有 ~100 行实现。
// ============================================================
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace minitest
{

    struct TestCase
    {
        std::string suite;
        std::string name;
        std::function<void()> fn;
    };

    inline std::vector<TestCase> &registry()
    {
        static std::vector<TestCase> r;
        return r;
    }

    struct Registrar
    {
        Registrar(const char *suite, const char *name, std::function<void()> fn)
        {
            registry().push_back({suite, name, std::move(fn)});
        }
    };

    struct AssertFailure
    {
        std::string msg;
    };

    inline void assertTrue(bool cond, const char *expr, const char *file, int line)
    {
        if (!cond)
        {
            char buf[512];
            std::snprintf(buf, sizeof(buf), "  FAIL %s:%d: %s", file, line, expr);
            throw AssertFailure{buf};
        }
    }

    inline void assertFloatEq(float a, float b, float eps, const char *file, int line)
    {
        if (std::abs(a - b) > eps)
        {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "  FAIL %s:%d: |%.6f - %.6f| > %.6f", file, line, a, b, eps);
            throw AssertFailure{buf};
        }
    }

    inline int runAll()
    {
        int passed = 0, failed = 0;
        for (auto &tc : registry())
        {
            try
            {
                tc.fn();
                ++passed;
                std::printf("[ PASS ] %s.%s\n", tc.suite.c_str(), tc.name.c_str());
            }
            catch (const AssertFailure &e)
            {
                ++failed;
                std::printf("[ FAIL ] %s.%s\n%s\n", tc.suite.c_str(), tc.name.c_str(), e.msg.c_str());
            }
            catch (...)
            {
                ++failed;
                std::printf("[ CRASH] %s.%s\n", tc.suite.c_str(), tc.name.c_str());
            }
        }
        std::printf("\n%d passed, %d failed, %d total\n", passed, failed, passed + failed);
        return failed > 0 ? 1 : 0;
    }

} // namespace minitest

#define TEST(suite, name)                                                                  \
    void test_##suite##_##name();                                                          \
    static minitest::Registrar reg_##suite##_##name(#suite, #name, test_##suite##_##name); \
    void test_##suite##_##name()

#define ASSERT_TRUE(expr) minitest::assertTrue((expr), #expr, __FILE__, __LINE__)
#define ASSERT_FALSE(expr) minitest::assertTrue(!(expr), "!(" #expr ")", __FILE__, __LINE__)
#define ASSERT_EQ(a, b) minitest::assertTrue((a) == (b), #a " == " #b, __FILE__, __LINE__)
#define ASSERT_NE(a, b) minitest::assertTrue((a) != (b), #a " != " #b, __FILE__, __LINE__)
#define ASSERT_FLOAT_EQ(a, b, eps) minitest::assertFloatEq((a), (b), (eps), __FILE__, __LINE__)
#define ASSERT_NEAR(a, b, eps) minitest::assertFloatEq((a), (b), (eps), __FILE__, __LINE__)
#define RUN_ALL_TESTS() minitest::runAll()
