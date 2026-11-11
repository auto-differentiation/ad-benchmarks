// Validation aid: with ADB_GRAD_OUT set, each timed gradient is also written
// to that file, so columns can be compared at the published size. That
// writing is timed, so a process says on stderr, once, that it is on. If the
// file cannot be opened, it says that once too, and removes a regular file
// there if the directory is writable; a later gradient of the run may then
// create the file anew. All of this happens at a gradient: a column that
// never calls note (plain, an extra column without the hook) prints nothing
// and leaves an old file as it is, and so does a failed open in a read-only
// directory, apart from the stderr line.
#pragma once
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <system_error>
namespace adbg {
inline const char* grad_out_path()
{
    static const char* const path = [] {
        const char* p = std::getenv("ADB_GRAD_OUT");
        if (p) std::fprintf(stderr, "ADB_GRAD_OUT=%s: writing gradients, timings of this run do not count\n", p);
        return p;
    }();
    return path;
}

inline void grad_out_failed(const char* path)
{
    std::error_code ec;
    const bool removed = std::filesystem::is_regular_file(path, ec) && std::remove(path) == 0;
    static bool said = false;
    if (said) return;
    said = true;
    std::fprintf(stderr, "ADB_GRAD_OUT=%s: cannot open, this gradient is not saved%s\n", path,
                 removed ? ", old file removed" : "");
}

template <class V>
const V& note(const V& g)
{
    if (const char* path = grad_out_path()) {
        if (std::FILE* f = std::fopen(path, "w")) {
            for (std::size_t i = 0; i < g.size(); ++i) std::fprintf(f, "%.17g\n", static_cast<double>(g[i]));
            std::fclose(f);
        } else {
            grad_out_failed(path);
        }
    }
    return g;
}
}  // namespace adbg
