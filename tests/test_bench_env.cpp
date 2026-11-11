// harness::parse_cli (harness/bench_env.hpp): the options every bench program
// shares, and its errors.
//
// Contract checked here:
//   harness::parse_cli(argc, argv, cli, workload_option)
//   - --plain-median S must be a positive, normal number, with nothing after
//     it; anything else throws std::invalid_argument naming --plain-median;
//   - --column, --run-column, --plain-median or --out as the last argument
//     throws std::invalid_argument "missing value for <option>".
//   harness::count_of(option, value)
//   - value as a whole is a non-negative decimal integer: returns it;
//     anything else throws std::invalid_argument naming the option.

#include "harness/bench_env.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace {

// Runs parse_cli on "prog" + args with a workload that takes no options.
harness::BenchCli parse(std::vector<std::string> args)
{
    args.insert(args.begin(), "prog");
    std::vector<char*> argv;
    for (std::string& a : args) argv.push_back(a.data());
    harness::BenchCli cli;
    harness::parse_cli(static_cast<int>(argv.size()), argv.data(), cli,
                       [](const std::string&, int, char**, int&) { return false; });
    return cli;
}

// The message of the std::invalid_argument parse throws, "" if none.
std::string error_of(const std::vector<std::string>& args)
{
    try {
        parse(args);
    } catch (const std::invalid_argument& e) {
        return e.what();
    }
    return "";
}

}  // namespace

TEST(ParseCli, PositivePlainMedianIsTaken)
{
    EXPECT_EQ(parse({"--plain-median", "0.125"}).plain_median, 0.125);
}

TEST(ParseCli, PlainMedianThatIsNotPositiveAndFiniteIsRejected)
{
    for (const char* v : {"0", "-0.5", "nan", "inf", "-inf", "5e-324"}) {
        SCOPED_TRACE(v);
        EXPECT_NE(error_of({"--plain-median", v}).find("--plain-median"), std::string::npos);
    }
}

TEST(ParseCli, SharedOptionWithoutValueIsReportedAsMissingValue)
{
    for (const char* option : {"--column", "--run-column", "--plain-median", "--out"}) {
        SCOPED_TRACE(option);
        EXPECT_EQ(error_of({"--column", "xad", option}), std::string("missing value for ") + option);
    }
}

TEST(ParseCli, PlainMedianThatIsNotANumberIsRejectedByName)
{
    for (const char* v : {"abc", "", "0.1s"}) {
        SCOPED_TRACE(v);
        EXPECT_NE(error_of({"--plain-median", v}).find("--plain-median"), std::string::npos);
    }
}

TEST(CountOf, DecimalCountIsTaken)
{
    EXPECT_EQ(harness::count_of("--paths", "0"), 0u);
    EXPECT_EQ(harness::count_of("--paths", "4096"), 4096u);
}

TEST(CountOf, ValueThatIsNotACountIsRejectedByName)
{
    for (const char* v : {"abc", "", "-1", "10x", "1e3", " 5", "99999999999999999999999"}) {
        SCOPED_TRACE(v);
        try {
            harness::count_of("--iters", v);
            ADD_FAILURE() << "accepted";
        } catch (const std::invalid_argument& e) {
            EXPECT_NE(std::string(e.what()).find("--iters"), std::string::npos) << e.what();
        }
    }
}
