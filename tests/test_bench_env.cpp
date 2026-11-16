// harness/bench_env.hpp: the CPU label of the CSV rows, and the options every
// bench program shares, with their errors.
//
// Contract checked here:
//   harness::cpu_label(cpuinfo)
//   - "<vendor> <brand> (<microarchitecture>)" from /proc/cpuinfo text, without
//     the model's SKU or trademark marks.
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
#include <utility>
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

// /proc/cpuinfo text of one processor.
std::string cpuinfo(const std::string& vendor, int family, int model, const std::string& model_name)
{
    return "processor\t: 0\nvendor_id\t: " + vendor + "\ncpu family\t: " + std::to_string(family) +
           "\nmodel\t\t: " + std::to_string(model) + "\nmodel name\t: " + model_name + "\nstepping\t: 1\n";
}

}  // namespace

TEST(CpuLabel, AmdFamily26IsEpycZen5WithoutTheSku)
{
    EXPECT_EQ(harness::cpu_label(cpuinfo("AuthenticAMD", 26, 2, "AMD EPYC 9655 96-Core Processor")),
              "AMD EPYC (Zen 5)");
}

TEST(CpuLabel, IntelModel0x8FIsXeonSapphireRapidsWithoutTrademarks)
{
    EXPECT_EQ(harness::cpu_label(cpuinfo("GenuineIntel", 6, 0x8F, "Intel(R) Xeon(R) Platinum 8480+")),
              "Intel Xeon (Sapphire Rapids)");
}

TEST(CpuLabel, AmdFamily25Model0x61IsRyzenZen4)
{
    EXPECT_EQ(harness::cpu_label(cpuinfo("AuthenticAMD", 25, 0x61, "AMD Ryzen 9 7950X 16-Core Processor")),
              "AMD Ryzen (Zen 4)");
}

TEST(CpuLabel, AmdFamily25IsZen4OnlyInsideItsModelRangesElseZen3)
{
    for (int model : {0x10, 0x1F, 0x60, 0x7F, 0xA0, 0xAF}) {
        SCOPED_TRACE(model);
        EXPECT_EQ(harness::cpu_label(cpuinfo("AuthenticAMD", 25, model, "AMD EPYC 9654")), "AMD EPYC (Zen 4)");
    }
    for (int model : {0x01, 0x0F, 0x20, 0x5F, 0x80, 0x9F, 0xB0}) {
        SCOPED_TRACE(model);
        EXPECT_EQ(harness::cpu_label(cpuinfo("AuthenticAMD", 25, model, "AMD EPYC 7763")), "AMD EPYC (Zen 3)");
    }
}

TEST(CpuLabel, AmdFamily23IsZen2FromModel0x30ElseZen)
{
    EXPECT_EQ(harness::cpu_label(cpuinfo("AuthenticAMD", 23, 0x31, "AMD EPYC 7742")), "AMD EPYC (Zen 2)");
    EXPECT_EQ(harness::cpu_label(cpuinfo("AuthenticAMD", 23, 0x30, "AMD EPYC 7742")), "AMD EPYC (Zen 2)");
    EXPECT_EQ(harness::cpu_label(cpuinfo("AuthenticAMD", 23, 0x2F, "AMD EPYC 7601")), "AMD EPYC (Zen)");
    EXPECT_EQ(harness::cpu_label(cpuinfo("AuthenticAMD", 23, 0x01, "AMD EPYC 7601")), "AMD EPYC (Zen)");
}

TEST(CpuLabel, IntelFamily6ServerModelsNameTheirMicroarchitecture)
{
    const std::pair<int, const char*> models[] = {{0x55, "Skylake/Cascade Lake"}, {0x6A, "Ice Lake"},
                                                  {0x6C, "Ice Lake"},  {0xCF, "Emerald Rapids"},
                                                  {0xAD, "Granite Rapids"}, {0xAE, "Granite Rapids"}};
    for (const auto& [model, uarch] : models) {
        SCOPED_TRACE(model);
        EXPECT_EQ(harness::cpu_label(cpuinfo("GenuineIntel", 6, model, "Intel(R) Xeon(R) Gold 6338 CPU @ 2.00GHz")),
                  std::string("Intel Xeon (") + uarch + ")");
    }
}

TEST(CpuLabel, UnknownFamilyOrModelIsNamedByNumberInDecimal)
{
    EXPECT_EQ(harness::cpu_label(cpuinfo("AuthenticAMD", 27, 1, "AMD EPYC 9999")), "AMD EPYC (family 27, model 1)");
    EXPECT_EQ(harness::cpu_label(cpuinfo("GenuineIntel", 6, 0x8E, "Intel(R) Xeon(R) CPU")),
              "Intel Xeon (family 6, model 142)");
    EXPECT_EQ(harness::cpu_label(cpuinfo("GenuineIntel", 19, 1, "Intel(R) Xeon(R) 6980P")),
              "Intel Xeon (family 19, model 1)");
}

TEST(CpuLabel, CpuinfoWithoutAnAmdOrIntelVendorIsUnknown)
{
    const std::string aarch64 = "processor\t: 0\nBogoMIPS\t: 2000.00\nCPU implementer\t: 0x41\n"
                                "CPU architecture: 8\nCPU part\t: 0xd40\n";
    EXPECT_EQ(harness::cpu_label(aarch64), "unknown");
    EXPECT_EQ(harness::cpu_label(""), "unknown");
    EXPECT_EQ(harness::cpu_label(cpuinfo("HygonGenuine", 24, 1, "Hygon C86 7285")), "unknown");
}

TEST(CpuLabel, BrandIsThreadripperOrCoreWithoutTheSku)
{
    EXPECT_EQ(harness::cpu_label(cpuinfo("AuthenticAMD", 25, 0x18, "AMD Ryzen Threadripper PRO 7995WX 96-Cores")),
              "AMD Ryzen Threadripper (Zen 4)");
    EXPECT_EQ(harness::cpu_label(cpuinfo("GenuineIntel", 6, 0xB7, "13th Gen Intel(R) Core(TM) i9-13900K")),
              "Intel Core (family 6, model 183)");
}

TEST(CpuLabel, ModelNameWithoutAKnownBrandGivesNoBrandWord)
{
    EXPECT_EQ(harness::cpu_label(cpuinfo("AuthenticAMD", 25, 1, "AMD Eng Sample: 100-000000314-04_30/20_N")),
              "AMD (Zen 3)");
}

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
