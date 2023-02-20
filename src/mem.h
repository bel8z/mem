//==================================================================================================
//
// Basic memory management and dynamic buffer utilities for C
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

#if !defined(MEM_DECL)

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MEM_ALIGN_OF _Alignof

#define MemSpan(T)  \
    struct          \
    {               \
        T *ptr;     \
        size_t len; \
    }

#define MemBuf(T)   \
    struct          \
    {               \
        MemSpan(T); \
        size_t cap; \
    }

#pragma warning(push)
#pragma warning(disable : 4201)
typedef struct MemArena
{
    MemBuf(uint8_t);
    size_t commit;
} MemArena;
#pragma warning(pop)

MemArena memReserve(size_t capacity);
MemArena *memBootstrap(size_t total_size);
bool memRelease(MemArena *mem);

void memClear(MemArena *mem);
void memDecommitExcess(MemArena *mem);

#define memAlloc(T, arena, count) memAllocEx(arena, count * sizeof(T), MEM_ALIGN_OF(T))
#define memAllocEx(arena, size, alignment) memReallocEx(arena, 0, 0, size, alignment)

/// (Re)allocate memory from the given arena. The returned pointer can be equal to the 'old' one if
/// the reallocation is performed in-place, and is NULL if allocation fails.
void *memReallocEx(MemArena *mem, void *old_ptr, size_t old_size, size_t new_size,
                   size_t alignment);

/// Round an address down to the previous (or current) aligned address.
/// The alignment must be a power of 2 and greater than 0.
size_t memAlignBackward(size_t addr, size_t alignment);

/// Round an address up to the next (or current) aligned address.
/// The alignment must be a power of 2 and greater than 0.
size_t memAlignForward(size_t addr, size_t alignment);

/// Move 'count' bytes of memory between the given location (basically memmove without
/// depending explicitly on the stdlib)
void memMove(void const *from, void *to, size_t count);

/// Ensure the given generic buffer have the required capacity, by reallocating it if necessary
/// Returns 'true' if the operation succeeds.
/// The 'mem' parameter can be left to NULL; in that case no allocation is performed, but the
/// operation may still succeed if the buffer has enough capacity
/// This is the foundational API for growing buffers
bool memBufRealloc(void **buf_ptr, size_t *cap_ptr, size_t req_cap, size_t item_size,
                   MemArena *mem);

/// Ensure the given total amount of capacity; return 'true' on success.
#define memBufEnsure(buf, total, mem) \
    memBufRealloc((void **)(&(buf)->ptr), &(buf)->cap, total, sizeof(*(buf)->ptr), mem)

/// Reserve the given amount of capacity; return 'true' on success.
#define memBufReserve(buf, amount, mem) memBufEnsure(buf, (buf)->len + amount, mem)

/// Reserve the given amount of capacity; return 'true' on success.
#define memBufReserveOne(buf, mem) memBufEnsure(buf, (buf)->len + 1, mem)

/// Insert the given element at the given position in the buffer, if the capacity allows for it
#define memBufInsert(buf, item, at, mem)                                                         \
    (at <= (buf)->len && memBufReserveOne(buf, mem)                                              \
         ? (memBufMove(buf, at, (at) + 1, ++(buf)->len - 1 + (at)), (buf)->ptr[at] = item, true) \
         : false)

/// Utility macro for moving 'count' items from one location to another inside the buffer
#define memBufMove(buf, from, to, count) \
    memMove((buf)->ptr + (from), (buf)->ptr + (to), sizeof(*(buf)->ptr) * (count))

#define MEM_DECL
#endif

//==================================================================================================
// Implementation
//==================================================================================================

#if defined(MEM_IMPL)

#include <string.h>

#if !defined(MEM_ALIGN_MAX)
#define MEM_ALIGN_MAX 16
#endif

#if !defined(MEM_ASSERT)
#include "assert.h"
#define MEM_ASSERT assert
#endif

#if !defined(MEM_ASSERT_MSG)
#define MEM_ASSERT_MSG(cond, msg) MEM_ASSERT((cond) && (msg))
#endif

static void memCommit(MemArena *mem);

static uint32_t
ceilPowerOf2_32(uint32_t v)
{
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
    return v;
}

static uint64_t
ceilPowerOf2_64(uint64_t v)
{
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    v++;
    return v;
}

#define ceilPowerOf2(x) _Generic(x, uint32_t : ceilPowerOf2_32, default : ceilPowerOf2_64)(x)

size_t
memAlignBackward(size_t addr, size_t alignment)
{
    MEM_ASSERT_MSG(alignment > 0, "Alignment must be > 0");
    MEM_ASSERT_MSG(!(alignment & (alignment - 1)), "Alignment must be a power of 2");
    // 000010000 // example alignment
    // 000001111 // subtract 1
    // 111110000 // binary not
    return addr & ~(alignment - 1);
}

size_t
memAlignForward(size_t addr, size_t alignment)
{
    return memAlignBackward(addr + (alignment - 1), alignment);
}

void
memMove(void const *from, void *to, size_t count)
{
    memmove(to, from, count);
}

void
memClear(MemArena *mem)
{
    mem->len = 0;
}

static bool
isLastAlloc(MemArena const *mem, uint8_t const *ptr, size_t size)
{
    if (!ptr) return false;
    uint8_t const *arena_end = mem->ptr + mem->len;
    uint8_t const *alloc_end = ptr + size;
    return (arena_end == alloc_end);
}

void *
memReallocEx(MemArena *mem, void *old_ptr, size_t old_size, size_t new_size, size_t alignment)
{
    MEM_ASSERT(mem);
    MEM_ASSERT_MSG(old_ptr == 0 || old_size != 0, "Inconsistent old memory state");
    MEM_ASSERT_MSG(new_size != 0 || old_ptr != 0, "Incoherent new memory request");

    if (isLastAlloc(mem, old_ptr, old_size))
    {
        if (new_size <= old_size)
        {
            mem->len -= (old_size - new_size);
        }
        else if (mem->cap - mem->len >= new_size - old_size)
        {
            mem->len += (new_size - old_size);
            memCommit(mem);
        }
        else
        {
            old_ptr = 0; // Out of memory
        }

        return old_ptr;
    }

    void *new_ptr = 0;
    size_t offset = memAlignForward(mem->len, alignment);

    if (offset <= mem->cap && mem->cap - offset >= new_size)
    {
        new_ptr = mem->ptr + offset;
        mem->len = offset + new_size;
        memCommit(mem);
        // make sure to preserve the data if the memory is reallocated
        if (old_ptr) memcpy(new_ptr, old_ptr, new_size);
    }

    return new_ptr;
}

bool
memBufRealloc(void **buf_ptr, size_t *cap_ptr, size_t req_cap, size_t item_size, MemArena *mem)
{
    size_t cur_cap = *cap_ptr;
    if (req_cap <= cur_cap) return true;
    if (!mem) return false;

    size_t new_capacity = ceilPowerOf2(req_cap);
    MEM_ASSERT(new_capacity);

    // TODO (Matteo): Improve?
    // Try to deduce a correct alignment from the item size withour requiring it (this allows
    // for a less cluttered API)
    size_t item_align = (item_size > MEM_ALIGN_MAX) ? MEM_ALIGN_MAX : ceilPowerOf2(item_size);
    MEM_ASSERT(item_align <= MEM_ALIGN_MAX);

    void *new_buf =
        memReallocEx(mem, *buf_ptr, cur_cap * item_size, new_capacity * item_size, item_align);
    if (!new_buf) return false;

    *cap_ptr = new_capacity;
    *buf_ptr = new_buf;
    return true;
}

// NOTE (Matteo): Windows only for now, sorry

#pragma warning(push)
#pragma warning(disable : 5105)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#pragma warning(pop)

#define PAGE_SIZE 4096

MemArena
memReserve(size_t capacity)
{
    MemArena mem = {
        .ptr = VirtualAlloc(0, capacity, MEM_RESERVE, PAGE_NOACCESS),
    };

    if (mem.ptr) mem.cap = capacity;

    return mem;
}

MemArena *
memBootstrap(size_t total_size)
{
    size_t commit_size = memAlignForward(sizeof(MemArena), PAGE_SIZE);
    void *block = VirtualAlloc(0, total_size, MEM_RESERVE, PAGE_NOACCESS);
    MemArena *arena = VirtualAlloc(block, commit_size, MEM_COMMIT, PAGE_READWRITE);

    if (arena)
    {
        arena->ptr = (uint8_t *)(arena + 1);
        arena->cap = total_size - sizeof(MemArena);
        arena->len = 0;
        arena->commit = commit_size - sizeof(MemArena);
    }

    return arena;
}

void
memCommit(MemArena *mem)
{
    size_t next_commit = memAlignForward(mem->len, PAGE_SIZE);
    void *ptr = mem->ptr + mem->commit;

    if (next_commit > mem->commit &&
        VirtualAlloc(ptr, next_commit - mem->commit, MEM_COMMIT, PAGE_READWRITE))
    {
        mem->commit = next_commit;
    }
}

void
memDecommitExcess(MemArena *mem)
{
    size_t min_commit = memAlignForward(mem->len, PAGE_SIZE);
    void *ptr = mem->ptr + min_commit;

    if (min_commit < mem->commit && VirtualFree(ptr, mem->commit - min_commit, MEM_DECOMMIT))
    {
        mem->commit = min_commit;
    }
}

bool
memRelease(MemArena *mem)
{
    return VirtualFree(mem->ptr, 0, MEM_RELEASE);
}

#endif

//==================================================================================================
