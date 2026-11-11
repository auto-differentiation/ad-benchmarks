// The built-in benchmark columns, in CSV order: plain, one per AD library whose
// column is on (ADB_WITH_<LIB>, defined by adb_link_ad_libraries in
// cmake/deps.cmake), and fd. A column turned off is not offered.

#pragma once

#include <string>
#include <vector>

namespace harness {

inline std::vector<std::string> builtin_column_names()
{
    std::vector<std::string> names{"plain"};
#if defined(ADB_WITH_XAD)
    names.push_back("xad");
#endif
#if defined(ADB_WITH_CPPAD)
    names.push_back("cppad");
#endif
#if defined(ADB_WITH_ADEPT)
    names.push_back("adept");
#endif
#if defined(ADB_WITH_AUTODIFF)
    names.push_back("autodiff");
#endif
    names.push_back("fd");
    return names;
}

}  // namespace harness
