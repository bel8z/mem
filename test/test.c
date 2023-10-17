#include <assert.h>
#include <stdio.h>

#define MEM_ASSERT assert
#define MEM_IMPLEMENTATION
#include "../mem.h"

typedef struct Buf
{
    uint32_t *ptr;
    size_t len;
    size_t cap;
} Buf;

int
main(void)
{
    bool result;

    MemArena *mem = memReserve(&(MemReserveOptions){
        .total_size = MEM_GB(1),
    });

    MemBlock block = memAlloc(mem, 1024, 8);
    assert(block.ptr);

    result = memFree(mem, &block);
    assert(result);
    assert(!block.ptr);

    return 0;
}
