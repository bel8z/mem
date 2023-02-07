#include "stdalign.h"
#include "stdbool.h"
#include "stddef.h"
#include "stdint.h"

//=== Interface ===//

#if !defined(ASSERT)
#    include "assert.h"
#    define ASSERT(cond, msg) assert((cond) && (msg))
#endif

#define UNUSED(var) (void)(var)

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

#define IS_POW2(v) ((v) && 0 == ((v) & ((v)-1)))

#define Span(T)     \
    struct          \
    {               \
        T *ptr;     \
        size_t len; \
    }

#define Buf(T)      \
    struct          \
    {               \
        Span(T);    \
        size_t cap; \
    }

typedef struct MemArena
{
    Buf(uint8_t);
    size_t commit;
} MemArena;

/// Round an address down to the previous (or current) aligned address.
/// The alignment must be a power of 2 and greater than 0.
static inline size_t
memAlignBackward(size_t addr, size_t alignment)
{
    ASSERT(IS_POW2(alignment), "Alignment must be a power of 2");
    // 000010000 // example alignment
    // 000001111 // subtract 1
    // 111110000 // binary not
    return addr & ~(alignment - 1);
}
/// Round an address up to the next (or current) aligned address.
/// The alignment must be a power of 2 and greater than 0.
/// Asserts that rounding up the address does not cause integer overflow.
static inline size_t
memAlignForward(size_t addr, size_t alignment)
{
    return memAlignBackward(addr + (alignment - 1), alignment);
}

MemArena memReserve(size_t capacity);
MemArena *memBootstrap(size_t total_size);
bool memRelease(MemArena *mem);

void memClear(MemArena *mem);
void memDecommitExcess(MemArena *mem);

void *memReallocEx(MemArena *mem, void *old_ptr, size_t old_count, //
                   size_t new_count, size_t data_size, size_t data_align);

#define memAllocEx(arena, count, data_size, data_align) \
    memReallocEx(arena, 0, 0, count, data_size, data_align)

#define memAlloc(T, arena, count) memAllocEx(arena, count, sizeof(T), alignof(T))

//=== Implementation ===//

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

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

static void
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

void
memClear(MemArena *mem)
{
    mem->len = 0;
}

static bool
isLastAlloc(MemArena const *mem, uint8_t const *ptr, size_t size)
{
    uint8_t const *arena_end = mem->ptr + mem->len;
    uint8_t const *alloc_end = ptr + size;
    return (arena_end == alloc_end);
}

void *
memReallocEx(MemArena *mem, void *old_ptr, size_t old_count, //
             size_t new_count, size_t data_size, size_t data_align)
{
    ASSERT(old_ptr == 0 || old_count != 0, "Inconsistent old memory state");
    ASSERT(new_count != 0 || old_ptr != 0, "Incoherent new memory request");

    size_t old_size = old_count * data_size;
    size_t new_size = new_count * data_size;

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
    size_t offset = memAlignForward(mem->len, data_align);

    if (offset <= mem->cap && mem->cap - offset >= new_size)
    {
        new_ptr = mem->ptr + offset;
        mem->len = offset + new_size;
        memCommit(mem);
    }

    return new_ptr;
}

//=== Test ===//

int
main(void)
{
    MemArena *mem = memBootstrap(1024 * 1024 * 1024);
    ASSERT(mem, "Reserve failed");

    Span(uint32_t) ints = {.len = 128};
    ints.ptr = memAlloc(uint32_t, mem, ints.len);

    ASSERT(ints.ptr, "Alloc failed");

    return 0;
}
