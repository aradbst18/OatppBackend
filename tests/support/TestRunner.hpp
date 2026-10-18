#pragma once

// Minimal dependency-free test harness (no gtest) so tests build offline.
// Usage:
//   #include "support/TestRunner.hpp"
//   TEST_CASE("suite: description") { EXPECT_TRUE(...); }
// main() is in test_main.cpp and runs all registered cases.

#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace app::tests
{

struct Failure : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

struct Case
{
    std::string name;
    std::function<void()> func;
};

inline std::vector<Case>& registry()
{
    static std::vector<Case> cases;

    return cases;
}

inline void registerCase(const std::string& name, std::function<void()> func)
{
    registry().push_back({name, std::move(func)});
}

inline int run(int argc = 0, char** argv = nullptr)
{
    std::string filter;
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--filter" && i + 1 < argc)
        {
            filter = argv[++i];
        }
        else if (arg.rfind("--filter=", 0) == 0)
        {
            filter = arg.substr(9);
        }
        else if (!arg.empty() && arg[0] != '-')
        {
            filter = arg;
        }
    }

    int passed = 0;
    int failed = 0;
    int matched = 0;

    for (const auto& c : registry())
    {
        if (!filter.empty() && c.name.find(filter) == std::string::npos)
        {
            continue;
        }

        ++matched;

        try
        {
            c.func();
            ++passed;
            std::cout << "[PASS] " << c.name << "\n";
        }
        catch (const Failure& ex)
        {
            ++failed;
            std::cout << "[FAIL] " << c.name << ": " << ex.what() << "\n";
        }
        catch (const std::exception& ex)
        {
            ++failed;
            std::cout << "[FAIL] " << c.name << ": unexpected std::exception: " << ex.what() << "\n";
        }
        catch (...)
        {
            ++failed;
            std::cout << "[FAIL] " << c.name << ": unknown exception\n";
        }
    }

    if (!filter.empty() && matched == 0)
    {
        std::cout << "Error: no test cases matched filter '" << filter << "'\n";
        return 1;
    }

    std::cout << "----\n" << passed << " passed, " << failed << " failed, " << (passed + failed) << " total\n";

    return failed == 0 ? 0 : 1;
}

inline int runAll()
{
    return run();
}

[[noreturn]] inline void fail(const std::string& expr, const char* file, int line, const std::string& detail = "")
{
    std::ostringstream out;
    out << file << ":" << line << ": expectation failed: " << expr;
    if (!detail.empty())
    {
        out << " (" << detail << ")";
    }

    throw Failure(out.str());
}

}  // namespace app::tests

#define APP_TEST_CONCAT2(a, b) a##b
#define APP_TEST_CONCAT(a, b) APP_TEST_CONCAT2(a, b)
#define TEST_CASE(name) TEST_CASE_IMPL(name, __COUNTER__)
#define TEST_CASE_IMPL(name, n)                                                \
    static void APP_TEST_CONCAT(app_test_body_, n)();                          \
    static const bool APP_TEST_CONCAT(app_test_reg_, n) = []()                 \
    {                                                                          \
        ::app::tests::registerCase(name, &APP_TEST_CONCAT(app_test_body_, n)); \
        return true;                                                           \
    }();                                                                       \
    static void APP_TEST_CONCAT(app_test_body_, n)()

#define EXPECT_TRUE(expr)                                  \
    do                                                     \
    {                                                      \
        if (!(expr))                                       \
        {                                                  \
            ::app::tests::fail(#expr, __FILE__, __LINE__); \
        }                                                  \
    } while (0)

#define EXPECT_FALSE(expr) EXPECT_TRUE(!(expr))

#define EXPECT_EQ(a, b)                                                                      \
    do                                                                                       \
    {                                                                                        \
        const auto _a = (a);                                                                 \
        const auto _b = (b);                                                                 \
        if (!(_a == _b))                                                                     \
        {                                                                                    \
            std::ostringstream _o;                                                           \
            _o << "lhs=" << _a << " rhs=" << _b;                                             \
            ::app::tests::fail(std::string(#a) + " == " + #b, __FILE__, __LINE__, _o.str()); \
        }                                                                                    \
    } while (0)

#define EXPECT_THROW_AS(stmt, ExcType)                                                         \
    do                                                                                         \
    {                                                                                          \
        bool _caught = false;                                                                  \
        try                                                                                    \
        {                                                                                      \
            stmt;                                                                              \
        }                                                                                      \
        catch (const ExcType&)                                                                 \
        {                                                                                      \
            _caught = true;                                                                    \
        }                                                                                      \
        if (!_caught)                                                                          \
        {                                                                                      \
            ::app::tests::fail(std::string("expected throw ") + #ExcType, __FILE__, __LINE__); \
        }                                                                                      \
    } while (0)
