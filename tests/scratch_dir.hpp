// A fresh directory under $TMPDIR (or .) that belongs to this process only, so
// concurrent runs of a test binary sharing one TMPDIR keep their own files;
// removed with its contents at scope exit.

#pragma once

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>

#include <stdlib.h>  // mkdtemp

namespace testing_support {

struct ScratchDir {
    std::string path;
    // prefix names the directory: $TMPDIR/<prefix>.XXXXXX
    explicit ScratchDir(const std::string& prefix)
    {
        const char* tmp = std::getenv("TMPDIR");
        std::string tmpl = std::string(tmp && *tmp ? tmp : ".") + "/" + prefix + ".XXXXXX";
        if (!mkdtemp(tmpl.data())) throw std::runtime_error("mkdtemp failed for " + tmpl);
        path = tmpl;
    }
    ~ScratchDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    ScratchDir(const ScratchDir&) = delete;
    ScratchDir& operator=(const ScratchDir&) = delete;
};

}  // namespace testing_support
