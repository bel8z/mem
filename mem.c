#include "mem.h"

//=== Assertions ===//

#if !defined(MEM_ASSERT)
#    include <assert.h>
#    define MEM_ASSERT assert
#endif

//=== Type checks ===//

_Static_assert(sizeof(size_t) == sizeof(uintptr_t), "Pointer size mismatch");
_Static_assert(MEM_ALIGNOF(size_t) == MEM_ALIGNOF(uintptr_t), "Pointer alignment mismatch");

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

//=== Internal utilities ===//

static inline uint32_t
boolMask(bool value)
{
    return ~((uint32_t)value - 1);
}

static inline bool
lastAlloc(MemArena *mem, MemBlock const *block)
{
    if (!block || !block->ptr) return false;

    uint8_t *mem_end = mem->ptr + mem->len;
    uint8_t *block_end = block->ptr + block->len;
    return (mem_end == block_end);
}

//=== Windows implementation ===//

#if defined(WIN32)

#    if !defined(NOMINMAX)
#        define NOMINMAX 1
#    endif

#    if !defined(VC_EXTRALEAN)
#        define VC_EXTRALEAN 1
#    endif

#    if !defined(WIN32_LEAN_AND_MEAN)
#        define WIN32_LEAN_AND_MEAN 1
#    endif

#    pragma warning(push)
#    pragma warning(disable : 5105)
#    include <Windows.h>
#    pragma warning(pop)

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
#    pragma warning(suppress : 6250)
        // NOTE (Matteo): This warns about using MEM_RELEASE without MEM_DECOMMIT, which is exactly
        // what we want
        BOOL result = VirtualFree(block.ptr, block.len, MEM_DECOMMIT);
        MEM_ASSERT(result);
    }
}

static inline void
adjustCommited(MemArena *mem)
{
    size_t min_commit = memAlignForward(mem->len, MEM_PAGE_SIZE);

    if (min_commit < mem->commit)
    {
        // NOTE (Matteo): Unused memory is decommitted only for safety reasons, in order to trigger
        // an error if is accessed.
        if (mem->flags & MEM_FLAG_UNSAFE) min_commit = mem->commit;
        // NOTE (Matteo): Freshly committed memory is cleared to zero by default; for consistency
        // all memory that is not decommitted (e.g. due to mismatched alignment) is cleared too.
        decommit((MemBlock){.ptr = mem->ptr + min_commit, .len = mem->commit - min_commit});
        FillMemory(mem->ptr + mem->len, min_commit - mem->len, 0);
    }
    else if (min_commit > mem->commit)
    {
        commit((MemBlock){.ptr = mem->ptr + mem->commit, .len = min_commit - mem->commit});
    }

    mem->commit = min_commit;
}

void
memRelease(MemArena *mem)
{
    MEM_ASSERT(mem);
    decommit((MemBlock){.ptr = mem->ptr, .len = mem->commit});
    BOOL result = VirtualFree(mem, 0, MEM_RELEASE);
    MEM_ASSERT(result);
}

MemArena *
memReserve(MemReserveOptions const *opts)
{
    // NOTE (Matteo): A single page is committed to store the allocator data structure. This is
    // a bit wasteful, but allows memory protection to work for all subsequent allocations
    MemBlock block = {.len = MEM_PAGE_SIZE};

    // NOTE (Matteo): If the total allocation size is not provided, it is deduced from the required
    // available size, plus the space required to store the allocator data structure
    size_t total_size = opts->total_size;
    size_t avail_size = opts->available_size;

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
    mem->flags |= (MEM_FLAG_UNSAFE & boolMask(opts->unsafe));

    return mem;
}

#endif // WIN32

//=== Common implementation ===//

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
        .ptr = (uint8_t *)memAlignForward((size_t)(mem->ptr + mem->len), alignment),
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

size_t
memAlignForward(size_t address, size_t alignment)
{
    return memAlignBackward(address + (alignment - 1), alignment);
}

size_t
memAlignBackward(size_t address, size_t alignment)
{
    // 000010000 : example alignment
    // 000001111 : subtract 1
    // 111110000 : binary not
    size_t trailing_ones = alignment - 1;

    // NOTE (Matteo): Alignment must be a power of 2
    MEM_ASSERT((alignment & trailing_ones) == 0);

    return address & ~trailing_ones;
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

//=== EOF ===//
