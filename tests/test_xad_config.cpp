// The public suite benchmarks plain XAD: a build without ADB_EXTRA_DIR has
// XAD's optional XAD_ENABLE_CODEGEN off (cmake/deps.cmake defaults it to OFF),
// read from XAD's generated configuration header.

#include <XAD/Config.hpp>

#include <gtest/gtest.h>

TEST(XadConfig, PublicBuildHasXadOptionalBackendOff)
{
#ifdef XAD_ENABLE_CODEGEN
    FAIL() << "XAD/Config.hpp defines XAD_ENABLE_CODEGEN in a build without ADB_EXTRA_DIR";
#else
    SUCCEED();
#endif
}
