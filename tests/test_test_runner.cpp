#include "support/TestRunner.hpp"

#include <utility>

namespace
{

struct RunResult
{
    int status;
    std::string output;
};

RunResult runCases(std::vector<app::tests::Case> cases, std::string filter = "")
{
    app::tests::registry() = std::move(cases);
    struct Capture
    {
        std::ostringstream output;
        std::streambuf* previous = std::cout.rdbuf(output.rdbuf());

        ~Capture()
        {
            std::cout.rdbuf(previous);
        }
    } capture;

    char program[] = "test_runner_tests";
    char* argv[] = {program, filter.data()};
    const int status = app::tests::run(2, argv);

    return {status, capture.output.str()};
}

}  // namespace

int main()
{
    try
    {
        const auto pass = []()
        {
        };
        const auto skip = []()
        {
            app::tests::skip("optional prerequisite absent");
        };
        const auto fail = []()
        {
            EXPECT_TRUE(false);
        };

        const auto passed = runCases({{"pass", pass}});
        EXPECT_EQ(passed.status, 0);
        EXPECT_EQ(passed.output, std::string("[PASS] pass\n----\n1 passed, 0 failed, 0 skipped, 1 total\n"));

        const auto skipped = runCases({{"skip", skip}});
        EXPECT_EQ(skipped.status, 77);
        EXPECT_EQ(skipped.output, std::string("[SKIP] skip: optional prerequisite absent\n----\n"
                                              "0 passed, 0 failed, 1 skipped, 1 total\n"));

        const auto mixed = runCases({{"skip", skip}, {"pass", pass}});
        EXPECT_EQ(mixed.status, 0);
        EXPECT_EQ(mixed.output, std::string("[SKIP] skip: optional prerequisite absent\n[PASS] pass\n----\n"
                                            "1 passed, 0 failed, 1 skipped, 2 total\n"));

        const auto failed = runCases({{"skip", skip},
                                      {"assertion", fail},
                                      {"exception",
                                       []()
                                       {
                                           throw std::runtime_error("provisioning failed");
                                       }},
                                      {"unknown",
                                       []()
                                       {
                                           throw 42;
                                       }},
                                      {"pass", pass}});
        EXPECT_EQ(failed.status, 1);
        EXPECT_TRUE(failed.output.find("[FAIL] assertion:") != std::string::npos);
        EXPECT_TRUE(failed.output.find("[FAIL] exception: unexpected std::exception: provisioning failed\n") !=
                    std::string::npos);
        EXPECT_TRUE(failed.output.find("[FAIL] unknown: unknown exception\n") != std::string::npos);
        EXPECT_TRUE(failed.output.find("1 passed, 3 failed, 1 skipped, 5 total\n") != std::string::npos);
        EXPECT_TRUE(failed.output.find("[PASS] skip") == std::string::npos);

        const auto failedAndSkipped = runCases({{"skip", skip}, {"assertion", fail}});
        EXPECT_EQ(failedAndSkipped.status, 1);
        EXPECT_TRUE(failedAndSkipped.output.find("0 passed, 1 failed, 1 skipped, 2 total\n") != std::string::npos);

        const auto filtered = runCases({{"pass", pass}, {"skip", skip}, {"assertion", fail}}, "skip");
        EXPECT_EQ(filtered.status, 77);
        EXPECT_EQ(filtered.output, skipped.output);

        const auto unmatched = runCases({{"skip", skip}}, "missing");
        EXPECT_EQ(unmatched.status, 1);
        EXPECT_EQ(unmatched.output, std::string("Error: no test cases matched filter 'missing'\n"));

        std::cout << "[PASS] test runner reporting regressions\n";
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[FAIL] test runner reporting regressions: " << ex.what() << "\n";
        return 1;
    }
}
