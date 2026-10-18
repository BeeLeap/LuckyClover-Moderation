// LeviLamina requires native mods to route C++ allocations through its
// unified allocator. This header defines the global new/delete operators and
// must be included by exactly one translation unit in the DLL.
#define LL_MEMORY_OPERATORS
#include "ll/api/memory/MemoryOperators.h" // IWYU pragma: keep

