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
//=============================================================================================

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

// Standard types are used in the API
// Define MEM_ASSERT to custom assert function if needed, otherwise the implementation uses
// the standard library provided <assert.h>
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

typedef struct MemReserveOptions
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
} MemReserveOptions;

MEM_API MemArena *memReserve(MemReserveOptions const *opts);

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

//=== Misc utilities ===//

MEM_API size_t memAlignForward(size_t address, size_t alignment);
MEM_API size_t memAlignBackward(size_t address, size_t alignment);

#endif // MEM_API