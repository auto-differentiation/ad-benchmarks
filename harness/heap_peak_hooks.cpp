// Allocation hooks for harness/heap_peak.hpp, built as the shared library
// adb_heap_peak and loaded with LD_PRELOAD for the untimed memory pass only.
//
// The malloc family is interposed and forwarded to glibc's own entry points
// (__libc_malloc, ...), which avoids the dlsym bootstrap problem. operator
// new / delete in libstdc++ (aligned forms included) go through malloc,
// aligned_alloc and free, so they are counted too. Bytes in use are tracked
// at the usable size of each block (malloc_usable_size), which is the same
// for the allocation and its release.

#include <malloc.h>

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>

extern "C" {
void* __libc_malloc(std::size_t);
void* __libc_calloc(std::size_t, std::size_t);
void* __libc_realloc(void*, std::size_t);
void* __libc_memalign(std::size_t, std::size_t);
void __libc_free(void*);
}

namespace {

std::atomic<std::uint64_t> current{0};
std::atomic<std::uint64_t> peak{0};

void add(void* p)
{
    if (!p) return;
    const std::uint64_t now = current.fetch_add(malloc_usable_size(p), std::memory_order_relaxed) +
                              malloc_usable_size(p);
    std::uint64_t old = peak.load(std::memory_order_relaxed);
    while (now > old && !peak.compare_exchange_weak(old, now, std::memory_order_relaxed)) {
    }
}

void remove(void* p)
{
    if (p) current.fetch_sub(malloc_usable_size(p), std::memory_order_relaxed);
}

}  // namespace

extern "C" {

__attribute__((visibility("default"))) std::uint64_t adb_heap_peak_reset()
{
    const std::uint64_t now = current.load(std::memory_order_relaxed);
    peak.store(now, std::memory_order_relaxed);
    return now;
}

__attribute__((visibility("default"))) std::uint64_t adb_heap_peak_get()
{
    return peak.load(std::memory_order_relaxed);
}

void* malloc(std::size_t n)
{
    void* p = __libc_malloc(n);
    add(p);
    return p;
}

void* calloc(std::size_t n, std::size_t size)
{
    void* p = __libc_calloc(n, size);
    add(p);
    return p;
}

void* realloc(void* old, std::size_t n)
{
    const std::size_t old_size = old ? malloc_usable_size(old) : 0;
    void* p = __libc_realloc(old, n);
    if (p || n == 0) current.fetch_sub(old_size, std::memory_order_relaxed);
    add(p);
    return p;
}

void free(void* p)
{
    remove(p);
    __libc_free(p);
}

void* memalign(std::size_t alignment, std::size_t n)
{
    void* p = __libc_memalign(alignment, n);
    add(p);
    return p;
}

void* aligned_alloc(std::size_t alignment, std::size_t n)
{
    void* p = __libc_memalign(alignment, n);
    add(p);
    return p;
}

int posix_memalign(void** out, std::size_t alignment, std::size_t n)
{
    if (alignment < sizeof(void*) || (alignment & (alignment - 1)) != 0) return EINVAL;
    void* p = __libc_memalign(alignment, n);
    if (!p && n != 0) return ENOMEM;
    add(p);
    *out = p;
    return 0;
}

}  // extern "C"
