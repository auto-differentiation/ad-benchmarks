// Extra benchmark columns registered from outside the bench source (e.g. from
// an ADB_EXTRA_DIR, see the top-level CMakeLists.txt). A source file linked
// into the bench defines, at namespace scope,
//
//   const harness::RegisterExtraColumn<Inputs> c{{"name", "version", setup}};
//
// with Inputs the bench's input type and setup(inputs) the column's untimed
// setup, returning its timed body. An optional fourth member, static_prealloc,
// returns the heap the column's library reserves before main() (e.g. a global
// tape), which the memory pass adds to peak_heap_reserved_bytes. The bench accepts the registered names as
// columns and runs them through harness::run_columns like its own; the inputs
// outlive the body.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace harness {

template <class Inputs>
struct ExtraColumn {
    std::string name;
    std::string version;  // the CSV library_version
    std::function<std::function<void()>(const Inputs&)> setup;
    std::function<std::uint64_t()> static_prealloc;  // empty: none
};

// Columns registered for Inputs, in registration order. A function-local
// static, so registration from any translation unit's static initialisation
// is safe.
template <class Inputs>
std::vector<ExtraColumn<Inputs>>& extra_columns()
{
    static std::vector<ExtraColumn<Inputs>> columns;
    return columns;
}

// nullptr if no column of that name is registered for Inputs.
template <class Inputs>
const ExtraColumn<Inputs>* find_extra_column(const std::string& name)
{
    for (const ExtraColumn<Inputs>& c : extra_columns<Inputs>())
        if (c.name == name) return &c;
    return nullptr;
}

// The static preallocation of the extra column of that name: 0 if it declares
// none or no such column is registered.
template <class Inputs>
std::uint64_t extra_static_prealloc(const std::string& name)
{
    const ExtraColumn<Inputs>* c = find_extra_column<Inputs>(name);
    return c && c->static_prealloc ? c->static_prealloc() : 0;
}

template <class Inputs>
struct RegisterExtraColumn {
    explicit RegisterExtraColumn(ExtraColumn<Inputs> c) { extra_columns<Inputs>().push_back(std::move(c)); }
};

}  // namespace harness
