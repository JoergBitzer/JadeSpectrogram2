// Counts heap allocations of the current thread: operator new and malloc/calloc/realloc
// (JUCE's HeapBlock, used by AudioBuffer and MidiBuffer, calls malloc directly).
#pragma once
#include <cstdlib>
#include <new>
extern "C" void* __libc_malloc(size_t); extern "C" void* __libc_calloc(size_t, size_t);
extern "C" void* __libc_realloc(void*, size_t); extern "C" void __libc_free(void*);
inline thread_local bool t_count = false;
inline long g_allocs = 0;
extern "C" void* malloc(size_t n) { if (t_count) ++g_allocs; return __libc_malloc(n); }
extern "C" void* calloc(size_t a, size_t b) { if (t_count) ++g_allocs; return __libc_calloc(a, b); }
extern "C" void* realloc(void* p, size_t n) { if (t_count) ++g_allocs; return __libc_realloc(p, n); }
extern "C" void free(void* p) { __libc_free(p); }
void* operator new(std::size_t n) { if (t_count) ++g_allocs; void* p = __libc_malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return p; }
void* operator new[](std::size_t n) { if (t_count) ++g_allocs; void* p = __libc_malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return p; }
void operator delete(void* p) noexcept { __libc_free(p); } void operator delete[](void* p) noexcept { __libc_free(p); }
void operator delete(void* p, std::size_t) noexcept { __libc_free(p); } void operator delete[](void* p, std::size_t) noexcept { __libc_free(p); }
