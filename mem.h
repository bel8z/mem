//==================================================================================================
// mem.h
//
// Provides a foundational layer with memory and dynamic buffers management for C applications
// on Windows, based on the virtual memory facilities provided by the platform
//
// This is an header-only library: include this header whenever you need the interface, definining
// the MEM_IMPLEMENTATION macro ONLY once to compile the implementation
//
//==================================================================================================
//
// The MIT License (MIT)
//
// Copyright (c) 2023 bassfault
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
// associated documentation files (the "Software"), to deal in the Software without restriction,
// including without limitation the rights to use, copy, modify, merge, publish, distribute,
// sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
// NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE
//
//==================================================================================================
// Interface
//==================================================================================================

#if !defined(MEM_API)

// Since MEM_API must be defined, it acts as an include guard too
#    if defined(__cplusplus)
#        define MEM_ALIGNOF alignof
#        define MEM_API extern "C"
#    else
#        define MEM_ALIGNOF _Alignof
#        define MEM_API
#    endif

//=== Dependencies ===//

// This library depends on the virtual memory API provided by the OS, but we try to minimize the
// dependencies on the C runtime for the core functionality.
// For this reason, the user may define the MEM_SET and MEM_COPY macros (either both of none!) to
// custom versions of the memset and memcpy functions; otherwise we try to use the intrinsic
// versions that the compiler may offer, falling back to the OS API (which in turn may fall back
// to the C standard library).
//      MEM_SET(ptr, val, len)
//      MEM_COPY(dst, src, len)
//
// Almost the same happens for assertions, with the MEM_ASSERT macros; in this case though the
// chosen fallback is the C standard <assert.h> header, because a) it is a debug faciliy,
// and b) the implementation offered by Windows seems reasonable.
// Note that we might switch to intrinsic traps in the future.
//      MEM_ASSERT(x)
//
// All of the above applies for the implementation only, so those macros must be defined ONCE, along
// the MEM_IMPLEMENTATION one.
// The interface relies on C standard types, which are pure definitions (so no runtime
// dependencies), and is not configurable (sorry folks, it's 2023 and we should agree on something).

#    include <stdbool.h>
#    include <stddef.h>
#    include <stdint.h>

//=== Macros ===//

// Utilities for explicit allocation sizes
#    define MEM_KB(count) ((size_t)(count) * (size_t)(1024))
#    define MEM_MB(count) MEM_KB(MEM_KB(count))
#    define MEM_GB(count) MEM_KB(MEM_MB(count))
#    define MEM_TB(count) MEM_KB(MEM_GB(count))
#    define MEM_PB(count) MEM_KB(MEM_TB(count))

//=== Main API ===//

// Block of memory.
// Mainly used for compact function definitions, as the {pointer, length} pair is used a lot.
// Zero-initialize for a null block.
typedef struct MemBlock
{
    uint8_t *ptr;
    size_t len;
} MemBlock;

// Memory arena aka linear aka bump allocator
//
// All allocation functions require a pointer this type.
// The type is opaque not only for encapsulation, but also to make sure that all the memory used
// is reserved by a call to memReserve
//
// Memory is allocated and freed in a linear, FIFO fashion. This means that a free operation may
// fail if the given block does not match the last allocation, but also that a user may attempt
// to resize a given allocation in place. This allows for building higher level operations, for
// example a reallocation function similar to stdlib's realloc may be defined like this:
//
// \code{.c}
// MemBlock customRealloc(MemArena *mem, MemBlock block, size_t new_len, size_t alignment)
// {
//     if (!memResize(mem, &block, new_len))
//     {
//         memFree(mem, &block);
//         block = memAlloc(mem, new_len, alignment);
//     }
//
//     return block;
// }
// \endcode
typedef struct MemArena MemArena;

typedef struct MemArenaInfo
{
    // Total allocation size, including space required for the allocator data structure.
    // If initialized to 0, this value is deduced by /param available_size
    size_t total_size;

    // Actual maximum size that should be available for allocations. The total reserved size can be
    // greater, depending on /param total_size (some space is reserved for the allocator data
    // structure)
    size_t available_size;

    // Set this flag to avoid protection of unused memory (may improve performance is memFree and
    // memResize are called often).
    bool unsafe;
} MemArenaInfo;

MEM_API MemArena *memReserve(MemArenaInfo const *info);

MEM_API void memRelease(MemArena *mem);

MEM_API void memClear(MemArena *mem);

// Allocate a block of memory with the given size and alignment
MEM_API MemBlock memAlloc(MemArena *mem, size_t len, size_t alignment);

// Try to resize the given memory block in place
MEM_API bool memResize(MemArena *mem, MemBlock *block, size_t new_len);

// Try to free the given memory block; since allocations are handed out in a linear fashion,
// this operation may not succeed. In this case the block is leaked until the entire allocation
// is cleared.
MEM_API bool memFree(MemArena *mem, MemBlock *block);

// Query the memory still available in the arena
MEM_API size_t memAvailable(MemArena *mem);

//=== Array allocation utilities ===//

// TODO (Matteo): Cleanup array allocation API

MEM_API void *memReallocEx(MemArena *mem, size_t item_size, size_t item_align, //
                           void *old_ptr, size_t old_count, size_t new_count);

#    define memArrayFree(mem, T, array, count) memArrayRealloc(mem, T, array, count, 0)
#    define memArrayAlloc(mem, T, count) memArrayRealloc(mem, T, NULL, 0, count)
#    define memArrayRealloc(mem, T, old_ptr, old_count, new_count) \
        (T *)memReallocEx(mem, sizeof(T), MEM_ALIGNOF(T), old_ptr, old_count, new_count)

#endif // MEM_API

//=============================================================================================
// Implementation
//=============================================================================================

#if defined(MEM_IMPLEMENTATION)

//=== Dependencies ===//

#    if defined(__has_builtin)
#        define MEM_HAS_BUILTIN __has_builtin
#    else
#        define MEM_HAS_BUILTIN (fn) false
#    endif

// NOTE (Matteo): Indirection required to expand macro arguments
#    define MEM_STR(x) MEM_STR_INNER(x)
#    define MEM_STR_INNER(x) #x

// Windows
// TODO (Matteo): Support other platforms
#    if !defined(NOMINMAX)
#        define NOMINMAX 1
#    endif

#    if !defined(VC_EXTRALEAN)
#        define VC_EXTRALEAN 1
#    endif

#    if !defined(WIN32_LEAN_AND_MEAN)
#        define WIN32_LEAN_AND_MEAN 1
#    endif

#    if defined(_MSC_VER)
#        pragma warning(push)
#        pragma warning(disable : 5105)
#    endif

#    include <Windows.h>

#    if defined(_MSC_VER)
#        pragma warning(pop)
#    endif

// Assertions
#    if !defined(MEM_ASSERT)
#        include <assert.h>
#        define MEM_ASSERT assert
#    endif

// MEM_SET, MEM_ZERO
#    if defined(MEM_SET)
#        define MEM_ZERO(ptr, len) MEM_SET(ptr, 0, len)
#    elif MEM_HAS_BUILTIN(__builtin_memset)
#        define MEM_SET __builtin_memset
#        define MEM_ZERO(ptr, len) MEM_SET(ptr, 0, len)
#    else
#        define MEM_SET(ptr, val, len) FillMemory(ptr, len, val)
#        define MEM_ZERO ZeroMemory
#    endif

// MEM_COPY: memcpy
#    if !defined(MEM_COPY)
#        if MEM_HAS_BUILTIN(__builtin_memcpy)
#            define MEM_COPY __builtin_memcpy
#        else
#            define MEM_COPY CopyMemory
#        endif
#    endif

//=== Data definitions ===//

enum
{
    // Misc
    MEM_PAGE_SIZE = 4096,

    // Option flags
    MEM_FLAG_UNSAFE = 0x02,
};

struct MemArena
{
    uint8_t *ptr;
    size_t len, cap, commit;
    uint32_t flags;
};

//=== Type checks ===//

_Static_assert(sizeof(MemArena) <= MEM_PAGE_SIZE, "Allocator does not fit page size");
_Static_assert(sizeof(size_t) == sizeof(uintptr_t), "Pointer size mismatch");
_Static_assert(MEM_ALIGNOF(size_t) == MEM_ALIGNOF(uintptr_t), "Pointer alignment mismatch");

//=== Internal utilities ===//

static inline uint32_t
boolMask(bool value)
{
    return ~((uint32_t)value - 1);
}

static inline size_t
alignBackward(size_t address, size_t alignment)
{
    // 000010000 : example alignment
    // 000001111 : subtract 1
    // 111110000 : binary not
    size_t trailing_ones = alignment - 1;

    // NOTE (Matteo): Alignment must be a power of 2
    MEM_ASSERT((alignment & trailing_ones) == 0);

    return address & ~trailing_ones;
}

static inline size_t
alignForward(size_t address, size_t alignment)
{
    return alignBackward(address + (alignment - 1), alignment);
}

static inline bool
lastAlloc(MemArena *mem, MemBlock const *block)
{
    if (!block || !block->ptr) return false;

    uint8_t *mem_end = mem->ptr + mem->len;
    uint8_t *block_end = block->ptr + block->len;
    return (mem_end == block_end);
}

static inline void
commit(MemBlock block)
{
    void *result = VirtualAlloc(block.ptr, block.len, MEM_COMMIT, PAGE_READWRITE);
    MEM_ASSERT(result);
}

static inline void
decommit(MemBlock block)
{
    // NOTE (Matteo): Avoid syscalls for no-ops
    if (block.len)
    {
#    if defined(_MSC_VER)
#        pragma warning(suppress : 6250)
#    endif
        // NOTE (Matteo): This warns about using MEM_RELEASE without MEM_DECOMMIT, which is exactly
        // what we want
        BOOL result = VirtualFree(block.ptr, block.len, MEM_DECOMMIT);
        MEM_ASSERT(result);
    }
}

static inline void
adjustCommited(MemArena *mem)
{
    size_t min_commit = alignForward(mem->len, MEM_PAGE_SIZE);

    if (min_commit < mem->commit)
    {
        // NOTE (Matteo): Unused memory is decommitted only for safety reasons, in order to trigger
        // an error if is accessed.
        if (mem->flags & MEM_FLAG_UNSAFE) min_commit = mem->commit;
        // NOTE (Matteo): Freshly committed memory is cleared to zero by default; for consistency
        // all memory that is not decommitted (e.g. due to mismatched alignment) is cleared too.
        decommit((MemBlock){.ptr = mem->ptr + min_commit, .len = mem->commit - min_commit});
        MEM_ZERO(mem->ptr + mem->len, min_commit - mem->len);
    }
    else if (min_commit > mem->commit)
    {
        commit((MemBlock){.ptr = mem->ptr + mem->commit, .len = min_commit - mem->commit});
    }

    mem->commit = min_commit;
}

//=== Interface functions ===//

void
memRelease(MemArena *mem)
{
    MEM_ASSERT(mem);
    decommit((MemBlock){.ptr = mem->ptr, .len = mem->commit});
    BOOL result = VirtualFree(mem, 0, MEM_RELEASE);
    MEM_ASSERT(result);
}

MemArena *
memReserve(MemArenaInfo const *info)
{
    // NOTE (Matteo): A single page is committed to store the allocator data structure. This is
    // a bit wasteful, but allows memory protection to work for all subsequent allocations
    MemBlock block = {.len = MEM_PAGE_SIZE};

    // NOTE (Matteo): If the total allocation size is not provided, it is deduced from the required
    // available size, plus the space required to store the allocator data structure
    size_t total_size = info->total_size;
    size_t avail_size = info->available_size;

    if (!total_size)
    {
        MEM_ASSERT(avail_size);
        total_size = avail_size + block.len;
    }

    if (!avail_size)
    {
        MEM_ASSERT(total_size);
        avail_size = total_size - block.len;
    }

    // NOTE (Matteo): Reserve a block of virtual memory from the OS and keep it protected, except
    // for the space used to store the allocator data structure
    block.ptr = VirtualAlloc(NULL, total_size, MEM_RESERVE, PAGE_NOACCESS);
    if (!block.ptr) return NULL;

    MEM_ASSERT(sizeof(MemArena) <= MEM_PAGE_SIZE);
    block.len = MEM_PAGE_SIZE;
    commit(block);

    MemArena *mem = (MemArena *)block.ptr;
    mem->ptr = block.ptr + MEM_PAGE_SIZE;
    mem->cap = avail_size;
    mem->len = 0;
    mem->commit = 0;
    mem->flags |= (MEM_FLAG_UNSAFE & boolMask(info->unsafe));

    return mem;
}

void
memClear(MemArena *mem)
{
    MEM_ASSERT(mem);
    mem->len = 0;
    adjustCommited(mem);
}

MemBlock
memAlloc(MemArena *mem, size_t len, size_t alignment)
{
    MEM_ASSERT(mem);

    MemBlock block = {
        .ptr = (uint8_t *)alignForward((size_t)(mem->ptr + mem->len), alignment),
    };

    size_t next_len = len + (block.ptr - mem->ptr);
    if (next_len > mem->cap || len == 0)
    {
        block.ptr = NULL;
    }
    else
    {
        block.len = len;
        mem->len = next_len;
        adjustCommited(mem);
        // NOTE (Matteo): Memory must be always cleared to 0
        MEM_ASSERT(block.ptr[0] == 0);
    }

    return block;
}

bool
memFree(MemArena *mem, MemBlock *block)
{
    MEM_ASSERT(mem);

    if (!lastAlloc(mem, block)) return false;

    MEM_ASSERT(block->len && block->len < mem->cap);

    mem->len -= block->len;
    adjustCommited(mem);

    block->ptr = NULL;
    block->len = 0;
    return true;
}

bool
memResize(MemArena *mem, MemBlock *block, size_t new_len)
{
    MEM_ASSERT(mem);

    if (!lastAlloc(mem, block)) return false;

    if (new_len < block->len)
    {
        mem->len -= (block->len - new_len);
        adjustCommited(mem);
    }
    else
    {
        size_t request = new_len - block->len;
        if (request > memAvailable(mem)) return false;
        mem->len += request;
        adjustCommited(mem);
    }

    if (!new_len)
    {
        // NOTE (Matteo): Empty blocks are not accessible
        block->ptr = NULL;
    }
    else
    {
        // NOTE (Matteo): Memory must be always cleared to 0
        MEM_ASSERT(block->ptr[0] == 0);
    }

    block->len = new_len;
    return true;
}

size_t
memAvailable(MemArena *mem)
{
    MEM_ASSERT(mem);
    return mem->cap - mem->len;
}

void *
memReallocEx(MemArena *mem,     //
             size_t item_size,  //
             size_t item_align, //
             void *old_ptr,     //
             size_t old_count,  //
             size_t new_count)
{
    MemBlock block = {.ptr = old_ptr, .len = old_count * item_size};
    size_t new_len = new_count * item_size;

    if (!memResize(mem, &block, new_len))
    {
        memFree(mem, &block);
        block = memAlloc(mem, new_len, item_align);
    }

    MEM_ASSERT(block.len || !block.ptr);

    return block.ptr;
}

#endif // MEM_IMPLEMENTATION

//==================================================================================================

// TODO (Matteo):
// * Dynamic buffer API and example usage
// * Fork/join allocators
// * Naming review

//==================================================================================================
